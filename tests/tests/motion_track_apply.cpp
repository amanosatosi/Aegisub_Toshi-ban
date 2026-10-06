#include <main.h>
#include "ass_file.h"
#include "ass_style.h"
#include "motion_tracking/motion_track_apply.h"
#include "motion_tracking/motion_track_commit.h"
#include <libaegisub/vfr.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

using namespace motion_tracking;
namespace {
MotionTrackResult Track(std::vector<double> const& x) {
	MotionTrackResult track;
	track.source_width = 640; track.source_height = 480; track.fps = 25;
	for (size_t i = 0; i < x.size(); ++i)
		track.frames.push_back({int(i),x[i],100,x[i],100,1,1,0,1,MotionTrackState::Tracked});
	return track;
}
std::vector<double> Linear(int count, double step = 5) {
	std::vector<double> x;
	for (int i = 0; i < count; ++i) x.push_back(100+i*step);
	return x;
}
AssDialogue Line(int frames, std::string text = "{\\pos(200,300)}Sign") {
	AssDialogue line; line.Start = 0; line.End = frames*40; line.Text = std::move(text); return line;
}
MotionApplication Apply(AssDialogue const& line, MotionTrackResult const& track,
	MotionApplyOptions o = {}, int reference = 0, MotionTrackResult const* clip = nullptr) {
	AssStyle style;
	style.angle = 7;
	return BuildMotionApplication(line,[&](std::string const&) { return &style; },track,clip,reference,
		agi::vfr::Framerate(25.0),640,480,o);
}
size_t Count(std::string const& text, std::string const& needle) {
	size_t count = 0;
	for (size_t p = 0; (p = text.find(needle,p)) != std::string::npos; p += needle.size()) ++count;
	return count;
}

// Independent renderer-contract oracle: relative scalar targets see the state
// at evaluation time; non-overlapping relative pos intents add displacement
// from their own start boundaries. It evaluates the emitted text, not planner
// values, so direction, payload, interval and composition errors are visible.
struct State { double x=0,y=0,scale=100,rotation=7,clip_x=0,clip_y=0,clips=100; bool positioned=false; };
double Value(AssOverrideTag const& t, size_t i, double current, bool nonnegative, bool y=false) {
	if (i >= t.Params.size() || t.Params[i].omitted) return current;
	auto token = t.Params[i].Get<std::string>();
	bool relative = !token.empty() && token[0] == '~';
	if (relative) token.erase(0,1);
	else relative = nonnegative && !token.empty() && (token[0] == '+' || token[0] == '-');
	double number = std::stod(token);
	return relative ? current + (y ? -number : number) : number;
}
void Evaluate(AssDialogueBlockOverride const& b, State& s, double time, double duration, double w=1, bool nested=false) {
	for (auto const& t : b.Tags) {
		if (t.Name == "\\t") {
			double a=t.Params[0].Get<double>(0), z=t.Params[1].Get<double>(duration);
			if (z==0) z=duration;
			double p=z==a ? (time>=z?1:0) : std::clamp((time-a)/(z-a),0.0,1.0);
			Evaluate(*t.Params[3].Get<AssDialogueBlockOverride*>(),s,time,duration,std::pow(p,t.Params[2].Get<double>(1)),true);
		}
		else if (t.Name == "\\r") { s.scale=s.clips=100; s.rotation=7; s.clip_x=s.clip_y=0; }
		else if (t.Name == "\\pos") {
			bool relative = t.Params[0].Get<std::string>().find('~') != std::string::npos;
			if (relative || !s.positioned) {
				s.x += (Value(t,0,s.x,false)-s.x)*w;
				s.y += (Value(t,1,s.y,false,true)-s.y)*w;
				s.positioned = true;
			}
		}
		else {
			auto scalar = [&](double& v,bool nonnegative) {
				double target=Value(t,0,v,nonnegative);
				v += (std::max(nonnegative ? 0.0 : -1e100,target)-v)*w;
			};
			if (t.Name=="\\scale") scalar(s.scale,true);
			else if (t.Name=="\\frz" || t.Name=="\\fr") scalar(s.rotation,false);
			else if (t.Name=="\\clips") scalar(s.clips,false);
			else if (t.Name=="\\clippos") {
				s.clip_x += (Value(t,0,s.clip_x,false)-s.clip_x)*w;
				s.clip_y += (Value(t,1,s.clip_y,false,true)-s.clip_y)*w;
			}
		}
	}
}
std::vector<State> RenderStates(AssDialogueBase const& event, double time) {
	AssDialogue line(event);
	State state;
	std::vector<State> states;
	for (auto const& block : line.ParseTags()) {
		if (auto b=dynamic_cast<AssDialogueBlockOverride const*>(block.get())) Evaluate(*b,state,time,int(event.End)-int(event.Start));
		else if (block->GetType()==AssBlockType::PLAIN || block->GetType()==AssBlockType::DRAWING) states.push_back(state);
	}
	if (states.empty()) states.push_back(state);
	return states;
}
State Render(AssDialogueBase const& event, double time) { return RenderStates(event,time).back(); }
void One(MotionApplication const& output, AssDialogue const& source) {
	ASSERT_EQ(1u,output.events.size());
	EXPECT_EQ(source.Id,output.events[0].Id);
	EXPECT_EQ(source.Start.GetMilliseconds(),output.events[0].Start.GetMilliseconds());
	EXPECT_EQ(source.End.GetMilliseconds(),output.events[0].End.GetMilliseconds());
}
}

TEST(MotionApply, LinearXYStaysOneCompactEvent) {
	auto track=Track(Linear(40));
	for (size_t i=0;i<track.frames.size();++i) track.frames[i].y+=i*2;
	auto source=Line(40); auto output=Apply(source,track); One(output,source);
	EXPECT_EQ(1u,Count(output.events[0].Text.get(),"\\t("));
	for (int i=0;i<40;++i) {
		auto s=Render(output.events[0],i*40);
		EXPECT_NEAR(200+i*5,s.x,0.00001); EXPECT_NEAR(300+i*2,s.y,0.00001);
	}
	EXPECT_EQ(source.Text.get(),RemoveMotionLayers(output.events[0].Text.get()));
}
TEST(MotionApply, StationaryDoesNotEmitTrackingOrRewriteOrdinarySubtitles) {
	for (auto const& text : {"Plain","{\\pos(200,300)\\fscx120\\bord4\\frz-10\\k20\\fad(20,30)}Sign","{\\an7}Automatic"}) {
		auto source=Line(8,text); auto output=Apply(source,Track(std::vector<double>(8,100)));
		One(output,source); EXPECT_EQ(source.Text,output.events[0].Text);
	}
}
TEST(MotionApply, MoveStopMoveNeedsTwoTransforms) {
	auto track=Track({100,105,110,115,120,120,120,120,120,116,112,108});
	auto source=Line(12); auto output=Apply(source,track); One(output,source);
	EXPECT_EQ(2u,Count(output.events[0].Text.get(),"\\t("));
	for (int i=0;i<12;++i) EXPECT_NEAR(200+track.frames[i].x-100,Render(output.events[0],i*40).x,0.00001);
}
TEST(MotionApply, StopMoveAndMoveStopKeepCompactHolds) {
	for (auto const& values : {std::vector<double>{100,100,100,105,110,115},
		std::vector<double>{100,105,110,115,115,115}}) {
		auto output=Apply(Line(6),Track(values));
		EXPECT_EQ(1u,output.events.size()); EXPECT_EQ(1u,Count(output.events[0].Text.get(),"\\t("));
	}
}
TEST(MotionApply, NonlinearSerializedFitStaysWithinPixelTolerance) {
	auto track=Track(Linear(100));
	for (int i=0;i<100;++i) { track.frames[i].x=100+30*std::sin(i*0.08); track.frames[i].y=100+20*std::cos(i*0.11); }
	auto source=Line(100); auto output=Apply(source,track); One(output,source);
	EXPECT_LT(Count(output.events[0].Text.get(),"\\t("),99u);
	for (int i=0;i<99;++i) for (int ms=0;ms<40;ms+=5) {
		double p=ms/40.0;
		double x=200+(1-p)*track.frames[i].x+p*track.frames[i+1].x-track.frames[0].x;
		double y=300+(1-p)*track.frames[i].y+p*track.frames[i+1].y-track.frames[0].y;
		auto s=Render(output.events[0],i*40+ms);
		EXPECT_LE(std::hypot(s.x-x,s.y-y),0.35001);
	}
}
TEST(MotionApply, ForcedFrameIntervalsStillOneEvent) {
	MotionApplyOptions o; o.encoding=MotionEncoding::FrameByFrame;
	auto source=Line(12); auto output=Apply(source,Track(Linear(12)),o); One(output,source);
	EXPECT_EQ(11u,Count(output.events[0].Text.get(),"\\t("));
	for (int i=0;i<12;++i) EXPECT_NEAR(200+i*5,Render(output.events[0],i*40).x,0.00001);
}
TEST(MotionApply, MainReferencePreservesAuthoredPlacementAndScaleRotation) {
	auto track=Track(Linear(8));
	for (int i=0;i<8;++i) {
		track.frames[i].scale_x=track.frames[i].scale_y=1+i*0.1;
		track.frames[i].rotation_deg=i*4; track.frames[i].y+=i*3;
	}
	auto source=Line(8,"{\\pos(900,700)\\scale150\\frz-30\\fscx80\\fscy120\\bord4}Sign");
	auto output=Apply(source,track,{},3); One(output,source);
	auto s=Render(output.events[0],120);
	EXPECT_NEAR(900,s.x,0.00001); EXPECT_NEAR(700,s.y,0.00001);
	EXPECT_NEAR(150,s.scale,0.00001); EXPECT_NEAR(-30,s.rotation,0.00001);
	for (int i=0;i<8;++i) {
		s=Render(output.events[0],i*40);
		EXPECT_NEAR(150*(1+i*0.1)/1.3,s.scale,0.20001);
		EXPECT_NEAR(-30-(i-3)*4,s.rotation,0.00001);
	}
	EXPECT_EQ(source.Text.get(),RemoveMotionLayers(output.events[0].Text.get()));
	EXPECT_EQ(1u,Count(output.events[0].Text.get(),"\\fscx"));
}
TEST(MotionApply, NoisyFitPinsReferenceExactly) {
	auto track=Track({100,105.1,110.2,115,120.1,125.2});
	auto output=Apply(Line(6),track,{},2);
	EXPECT_NEAR(200,Render(output.events[0],80).x,0.000001);
}
TEST(MotionApply, RelativeYUsesAuthorFacingDirections) {
	for (double dy : {20.0,-20.0}) {
		auto track=Track({100,100}); track.frames[1].y+=dy;
		auto output=Apply(Line(2),track);
		EXPECT_NE(output.events[0].Text.get().find(dy>0 ? "\\pos(~+0,~-20)" : "\\pos(~+0,~+20)"),std::string::npos);
		EXPECT_NEAR(300+dy,Render(output.events[0],40).y,0.00001);
	}
}
TEST(MotionApply, AutomaticPlacementStaysRelativeAndKeepsMarginsAlignment) {
	auto source=Line(6,"{\\an7}Automatic"); source.Margin={{12,34,56}};
	auto output=Apply(source,Track(Linear(6)),{},2); One(output,source);
	EXPECT_EQ(source.Margin,output.events[0].Margin);
	EXPECT_EQ(std::string::npos,output.events[0].Text.get().find("\\pos(200"));
	EXPECT_NE(std::string::npos,output.events[0].Text.get().find("\\pos(~-10,~+0)"));
	// Mangetsu supplies the layout base; generated displacement at reference is zero.
	EXPECT_NEAR(0,Render(output.events[0],80).x,0.00001);
}
TEST(MotionApply, RotationUnwrapsBoth360And180Boundaries) {
	for (auto angles : {std::vector<double>{350,355,0,5,10},std::vector<double>{170,175,180,-175,-170}}) {
		auto track=Track(std::vector<double>(5,100));
		for (int i=0;i<5;++i) track.frames[i].rotation_deg=angles[i];
		auto source=Line(5,"{\\frz30}Sign"); auto output=Apply(source,track,{},2);
		EXPECT_EQ(1u,Count(output.events[0].Text.get(),"\\t("));
		for (int i=0;i<5;++i) EXPECT_NEAR(30-(i-2)*5,Render(output.events[0],i*40).rotation,0.00001);
	}
}
TEST(MotionApply, ComponentsOptimizeIndependentlyAndCombineMatchingIntervals) {
	auto track=Track(Linear(12));
	for (int i=0;i<12;++i) {
		track.frames[i].rotation_deg=i<4 ? i*3 : i<8 ? 9 : 9+(i-7)*2;
		track.frames[i].scale_x=track.frames[i].scale_y=1+i*0.01;
	}
	auto output=Apply(Line(12),track);
	EXPECT_EQ(3u,Count(output.events[0].Text.get(),"\\t(")); // pos+scale together, two rotation runs
	for (int i=0;i<12;++i) {
		auto s=Render(output.events[0],i*40);
		EXPECT_NEAR(200+i*5,s.x,0.00001); EXPECT_NEAR(100+i,s.scale,0.00001);
		EXPECT_NEAR(7-track.frames[i].rotation_deg,s.rotation,0.00001);
	}
}
TEST(MotionApply, AnimatedObjectScaleComposesWithoutDeletingAuthoredTransforms) {
	auto track=Track(std::vector<double>(6,100));
	for (int i=0;i<6;++i) track.frames[i].scale_x=track.frames[i].scale_y=1+i*0.1;
	auto source=Line(6,"{\\scale150\\t(0,200,2,\\scale200)\\t(0,200,\\frz~+20)\\fad(20,30)\\k20}Sign");
	auto output=Apply(source,track,{},2); One(output,source);
	EXPECT_EQ(source.Text.get(),RemoveMotionLayers(output.events[0].Text.get()));
	for (int ms=0;ms<=200;++ms) {
		auto original=Render(source,ms), actual=Render(output.events[0],ms);
		EXPECT_NEAR(original.scale*(1+ms/400.0)/1.2,actual.scale,0.20001);
		EXPECT_NEAR(original.rotation,actual.rotation,0.00001);
	}
}
TEST(MotionApply, InlineResetsAndPerSpanScaleAreComposed) {
	auto track=Track(std::vector<double>(5,100));
	for (int i=0;i<5;++i) { track.frames[i].scale_x=track.frames[i].scale_y=1+i*0.1; track.frames[i].rotation_deg=i*3; }
	auto source=Line(5,"{\\scale150\\frz20}A{\\r\\scale80\\frz-10}B{\\scale~+20}C");
	auto output=Apply(source,track,{},2);
	EXPECT_EQ(source.Text.get(),RemoveMotionLayers(output.events[0].Text.get()));
	for (int i=0;i<5;++i) {
		auto states=RenderStates(output.events[0],i*40);
		ASSERT_EQ(3u,states.size());
		EXPECT_NEAR(150*(1+i*0.1)/1.2,states[0].scale,0.00001);
		EXPECT_NEAR(80*(1+i*0.1)/1.2,states[1].scale,0.00001);
		EXPECT_NEAR(100*(1+i*0.1)/1.2,states[2].scale,0.00001);
		EXPECT_NEAR(-10-(i-2)*3,states[1].rotation,0.00001);
		EXPECT_NEAR(-10-(i-2)*3,states[2].rotation,0.00001);
	}
}
TEST(MotionApply, FourClipFormsKeepGeometryAndDrawingScale) {
	for (auto const& shape : {"\\clip(10,20,50,60)","\\iclip(10,20,50,60)",
		"\\clip(m 10 20 l 50 20 50 60)","\\iclip(4,m 10 20 l 50 20 50 60)"}) {
		auto track=Track(Linear(5));
		for (int i=0;i<5;++i) { track.frames[i].y+=i*10; track.frames[i].scale_x=track.frames[i].scale_y=1+i*0.1; }
		auto source=Line(5,std::string("{")+shape+"\\clippos(4,6)\\clips150}Mask");
		MotionApplyOptions o; o.object_motion=false;
		auto output=Apply(source,{},o,2,&track); One(output,source);
		EXPECT_TRUE(output.used_clippos);
		EXPECT_EQ(source.Text.get(),RemoveMotionLayers(output.events[0].Text.get()));
		EXPECT_EQ(1u,Count(output.events[0].Text.get(),shape));
		for (int i=0;i<5;++i) {
			auto s=Render(output.events[0],i*40);
			EXPECT_NEAR(4+(i-2)*5,s.clip_x,0.00001);
			EXPECT_NEAR(6+(i-2)*10,s.clip_y,0.00001);
			EXPECT_NEAR(150*(1+i*0.1)/1.2,s.clips,0.20001);
		}
	}
}
TEST(MotionApply, ClipRelativeYBothDirectionsAndAnimatedClipScale) {
	for (double dy : {20.0,-20.0}) {
		auto track=Track({100,100}); track.frames[1].y+=dy;
		MotionApplyOptions o; o.object_motion=false;
		auto output=Apply(Line(2,"{\\clip(0,0,20,20)}Mask"),{},o,0,&track);
		EXPECT_NE(std::string::npos,output.events[0].Text.get().find(dy>0 ? "\\clippos(~+0,~-20)" : "\\clippos(~+0,~+20)"));
		EXPECT_NEAR(dy,Render(output.events[0],40).clip_y,0.00001);
	}
	auto track=Track(std::vector<double>(6,100));
	for (int i=0;i<6;++i) track.frames[i].scale_x=track.frames[i].scale_y=1+i*0.1;
	MotionApplyOptions o; o.object_motion=false;
	auto source=Line(6,"{\\iclip(2,m 0 0 l 20 0 20 20)\\clips120\\t(0,200,\\clips~+30)}Mask");
	auto output=Apply(source,{},o,2,&track);
	for (int ms=0;ms<=200;++ms) EXPECT_NEAR(Render(source,ms).clips*(1+ms/400.0)/1.2,Render(output.events[0],ms).clips,0.20001);
}
TEST(MotionApply, MultiSelectionUsesEachEventsRangeWithoutChangingTimingIdentityOrFields) {
	AssFile file;
	auto a=new AssDialogue(Line(10,"A")), b=new AssDialogue(Line(10,"B")), other=new AssDialogue(Line(10,"Ordinary"));
	a->Start=7; a->End=193; b->Start=201; b->End=397;
	a->Actor="Actor, raw"; b->Effect="Effect"; a->Layer=4; a->Margin={{1,2,3}};
	file.Events.push_back(*a); file.Events.push_back(*other); file.Events.push_back(*b);
	file.SetExtradataValue(*a,"plugin","original");
	auto track=Track(Linear(10)); int aid=a->Id,bid=b->Id;
	auto selected=InstallMotionApplications(file,{{aid,Apply(*a,track,{},5)},{bid,Apply(*b,track,{},5)}},bid);
	ASSERT_EQ(2u,selected.selected.size()); EXPECT_EQ(a,selected.selected[0]); EXPECT_EQ(b,selected.active);
	EXPECT_EQ(3,std::distance(file.Events.begin(),file.Events.end()));
	EXPECT_EQ(7,a->Start.GetMilliseconds()); EXPECT_EQ(397,b->End.GetMilliseconds());
	EXPECT_EQ(aid,a->Id); EXPECT_EQ(bid,b->Id); EXPECT_EQ("Actor, raw",a->Actor.get()); EXPECT_EQ(4,a->Layer);
	EXPECT_EQ("Ordinary",other->Text.get()); EXPECT_EQ("original",file.GetExtradata(a->ExtradataIds.get())[0].value);
	auto restored=RevertMotionFamilies(file,selected.selected,bid);
	EXPECT_EQ(b,restored.active); EXPECT_EQ("A",a->Text.get()); EXPECT_EQ("B",b->Text.get());
}
TEST(MotionApply, ReapplyReplacesOwnedLayerAndRevertPreservesUserEdits) {
	AssFile file; auto source=new AssDialogue(Line(6,"{\\pos(200,300)\\t(0,200,\\blur3)}Sign"));
	file.Events.push_back(*source); int id=source->Id; auto track=Track(Linear(6));
	InstallMotionApplications(file,{{id,Apply(*source,track)}},id);
	auto once=source->Text.get();
	InstallMotionApplications(file,{{id,Apply(*source,track)}},id);
	EXPECT_EQ(once,source->Text.get()); EXPECT_EQ(1u,Count(once,MotionLayerMarker));
	source->Text=source->Text.get()+"{\\i1} edit"; source->Actor="new actor";
	EXPECT_TRUE(CanRevertMotion(file,*source));
	auto restored=RevertMotionFamilies(file,{source},id);
	EXPECT_EQ(source,restored.active); EXPECT_EQ(id,source->Id); EXPECT_EQ("new actor",source->Actor.get());
	EXPECT_EQ("{\\pos(200,300)\\t(0,200,\\blur3)}Sign{\\i1} edit",source->Text.get());
	EXPECT_FALSE(CanRevertMotion(file,*source));
}
TEST(MotionApply, RevertRecognizesOnlyExactOwnershipAtBlockStart) {
	std::string text="{user note [toshiban native motion v2]\\t(0,20,\\blur3)}Sign{[toshiban native motion v2 longer]\\frz3}";
	EXPECT_EQ(text,RemoveMotionLayers(text)); EXPECT_FALSE(HasMotionLayers(text));
	EXPECT_EQ(text,RemoveMotionLayers("{"+std::string(MotionLayerMarker)+"\\pos(~+1,~+0)}"+text));
}
TEST(MotionApply, ApplyValidationIsAtomicAndRejectsSplitting) {
	AssFile file; auto source=new AssDialogue(Line(5)); file.Events.push_back(*source);
	auto application=Apply(*source,Track(Linear(5)));
	EXPECT_THROW(InstallMotionApplications(file,{{source->Id,application},{-1,application}},source->Id),std::invalid_argument);
	EXPECT_EQ("{\\pos(200,300)}Sign",source->Text.get());
	application.events.push_back(application.events[0]);
	EXPECT_THROW(InstallMotionApplications(file,{{source->Id,application}},source->Id),std::invalid_argument);
	EXPECT_EQ(1,std::distance(file.Events.begin(),file.Events.end()));
}
TEST(MotionApply, VfrPreservesSubCentisecondTimingsAndUsesExactMediaTimes) {
	agi::vfr::Framerate tc(std::vector<int>{0,12,38,73,120,177,233,304});
	auto track=Track(std::vector<double>(8,100));
	for (int i=0;i<8;++i) track.frames[i].x=100+tc.TimeAtFrame(i)*0.1;
	auto source=Line(8); source.Start=7; source.End=281;
	AssStyle style;
	auto output=BuildMotionApplication(source,[&](std::string const&) {return &style;},track,nullptr,3,tc,640,480);
	One(output,source);
	EXPECT_EQ(1u,Count(output.events[0].Text.get(),"\\t("));
	for (int i=1;i<=6;++i) EXPECT_NEAR(200+(tc.TimeAtFrame(i)-73)*0.1,Render(output.events[0],tc.TimeAtFrame(i)-int(source.Start)).x,0.00001);
}
TEST(MotionApply, GapsLostSamplesInvalidScaleAndReferenceRejectClearly) {
	auto source=Line(5); auto track=Track(Linear(5)); track.frames.erase(track.frames.begin()+2);
	EXPECT_THROW(Apply(source,track),std::invalid_argument);
	track=Track(Linear(5)); track.frames[2].state=MotionTrackState::Lost;
	EXPECT_THROW(Apply(source,track),std::invalid_argument);
	track=Track(Linear(5)); track.frames[2].scale_x=0;
	EXPECT_THROW(Apply(source,track),std::invalid_argument);
	EXPECT_THROW(Apply(source,Track(Linear(5)),{},20),std::invalid_argument);
	track=Track(Linear(5)); track.frames[2].x=std::numeric_limits<double>::quiet_NaN();
	EXPECT_THROW(Apply(source,track),std::invalid_argument);
}
TEST(MotionApply, AmbiguousCompetingPositionAndUnsupportedClipRotationExplainRejection) {
	auto track=Track(Linear(5));
	EXPECT_THROW(Apply(Line(5,"{\\pos(200,300)\\t(0,100,\\pos(~+40,~+0))}Sign"),track),std::invalid_argument);
	MotionApplyOptions o; o.position_x=o.position_y=false;
	EXPECT_NO_THROW(Apply(Line(5,"{\\move(0,0,50,0)}Sign"),track,o));
	for (int i=0;i<5;++i) track.frames[i].rotation_deg=i*10;
	o.object_motion=false; o.position_x=o.position_y=true;
	EXPECT_THROW(Apply(Line(5,"{\\clip(0,0,20,20)}Mask"),{},o,0,&track),std::invalid_argument);
	o.rotation=false;
	EXPECT_NO_THROW(Apply(Line(5,"{\\clip(0,0,20,20)}Mask"),{},o,0,&track));
	EXPECT_THROW(Apply(Line(5,"{\\clip(0,0,20,20)\\t(0,100,\\clip(1,1,21,21))}Mask"),{},o,0,&track),std::invalid_argument);
}
TEST(MotionSession, GeneratedLayersDoNotInvalidateIdentityOrClipSession) {
	auto source=Line(5,"{\\pos(200,300)\\clip(0,0,20,20)}Sign");
	auto identity=IdentifyMotionSource(source); auto track=Track(Linear(5));
	source.Text=Apply(source,track,{},0,&track).events[0].Text;
	EXPECT_TRUE(ValidateMotionSources({identity},{&source},source.Id,source.Id,true).empty());
	source.End=201;
	EXPECT_FALSE(ValidateMotionSources({identity},{&source},source.Id,source.Id,true).empty());
}
TEST(MotionOptimizer, ExactEndpointsAndBoundsAcrossAllModes) {
	std::vector<MotionSample> samples;
	for (int i=0;i<80;++i) samples.push_back({i,double(i*40),{std::sin(i*0.2)*15}});
	for (auto mode : {MotionEncoding::Automatic,MotionEncoding::ForceOptimized,MotionEncoding::FrameByFrame}) {
		auto fit=OptimizeMotion(samples,{0.35},mode);
		for (auto const& r : fit.regions) for (size_t i=r.first;i<=r.last;++i) {
			double p=r.first==r.last?0:(samples[i].time-samples[r.first].time)/(samples[r.last].time-samples[r.first].time);
			EXPECT_LE(std::abs(samples[i].values[0]-(r.from[0]+p*(r.to[0]-r.from[0]))),0.350001);
		}
		if (mode==MotionEncoding::FrameByFrame) EXPECT_EQ(79u,fit.regions.size());
	}
}
