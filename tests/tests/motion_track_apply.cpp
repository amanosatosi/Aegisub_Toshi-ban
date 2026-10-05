#include <main.h>

#include "ass_file.h"
#include "ass_info.h"
#include "ass_style.h"
#include "motion_tracking/motion_track_apply.h"
#include "motion_tracking/motion_track_commit.h"
#include "motion_tracking/motion_track_optimizer.h"
#include <libaegisub/vfr.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

using namespace motion_tracking;
namespace {
std::vector<MotionSample> Signals(std::vector<double> const& x) {
	std::vector<MotionSample> samples;
	for (size_t i = 0; i < x.size(); ++i) samples.push_back({int(i),double(i*40),{x[i]}});
	return samples;
}

MotionTrackResult Track(std::vector<double> const& x, double y = 100) {
	MotionTrackResult track;
	track.source_width = 640; track.source_height = 480; track.fps = 25;
	for (size_t i = 0; i < x.size(); ++i)
		track.frames.push_back({int(i),x[i],y,x[i],y,1,1,0,1,MotionTrackState::Tracked});
	return track;
}

std::vector<double> Linear(int count, double start = 100, double step = 5) {
	std::vector<double> v;
	for (int i = 0; i < count; ++i) v.push_back(start+i*step);
	return v;
}

AssDialogue Line(int frames, std::string text = "{\\pos(200,300)}Sign") {
	AssDialogue line;
	line.Start = 0; line.End = frames*40; line.Text = std::move(text);
	return line;
}

MotionApplication Apply(AssDialogue const& line, MotionTrackResult const& main,
	MotionTrackResult const* clip = nullptr, MotionApplyOptions options = {}, int reference = 0) {
	AssStyle style;
	return BuildMotionApplication(line,[&](std::string const&) { return &style; },main,clip,reference,
		agi::vfr::Framerate(25.0),640,480,options);
}

std::vector<std::string> Tags(AssDialogueBase const& event, std::string const& name, bool nested = false) {
	AssDialogue line(event);
	std::vector<std::string> found;
	for (auto const& block : line.ParseTags()) if (auto b = dynamic_cast<AssDialogueBlockOverride*>(block.get())) {
		for (auto const& tag : b->Tags) {
			if (tag.Name == name) found.push_back(std::string(tag));
			if (nested && tag.Name == "\\t") for (auto const& p : tag.Params)
				if (!p.omitted && p.GetType() == VariableDataType::BLOCK)
					for (auto const& effect : p.Get<AssDialogueBlockOverride*>()->Tags)
						if (effect.Name == name) found.push_back(std::string(effect));
		}
	}
	return found;
}

double Numeric(AssDialogueBase const& event, std::string const& name, size_t parameter = 0) {
	auto tags = Tags(event,name);
	if (tags.empty()) return std::numeric_limits<double>::quiet_NaN();
	AssOverrideTag tag(tags.front());
	return parameter < tag.Params.size() ? tag.Params[parameter].Get<double>() : std::numeric_limits<double>::quiet_NaN();
}
}

TEST(MotionOptimizer, StaticCollapses) {
	auto result = OptimizeMotion(Signals({100,100,100,100,100}),{0.35},MotionEncoding::Automatic);
	ASSERT_EQ(1u,result.regions.size());
	EXPECT_TRUE(result.regions[0].stationary);
}

TEST(MotionOptimizer, PerfectLinear) {
	auto result = OptimizeMotion(Signals(Linear(20)),{0.35},MotionEncoding::Automatic);
	ASSERT_EQ(1u,result.regions.size());
	EXPECT_FALSE(result.regions[0].stationary);
	EXPECT_DOUBLE_EQ(100,result.regions[0].from[0]);
	EXPECT_DOUBLE_EQ(195,result.regions[0].to[0]);
}

TEST(MotionOptimizer, NoisyVisuallyLinear) {
	auto result = OptimizeMotion(Signals({100,104.999,110.003,114.998,120.001}),{0.35},MotionEncoding::Automatic);
	ASSERT_EQ(1u,result.regions.size());
	EXPECT_FALSE(result.frame_by_frame);
}

TEST(MotionOptimizer, MovementHoldMovementHasThreeRegions) {
	auto result = OptimizeMotion(Signals({100,105,110,115,120,120,120,120,120,116,112,108}),{0.35},MotionEncoding::Automatic);
	ASSERT_EQ(3u,result.regions.size());
	EXPECT_FALSE(result.regions[0].stationary);
	EXPECT_TRUE(result.regions[1].stationary);
	EXPECT_FALSE(result.regions[2].stationary);
	EXPECT_EQ(4u,result.regions[1].first);
	EXPECT_EQ(8u,result.regions[1].last);
}

TEST(MotionOptimizer, StationaryJitterAndSlowDrift) {
	auto hold = OptimizeMotion(Signals({100.01,99.99,100.03,99.98,100}),{0.35},MotionEncoding::Automatic);
	ASSERT_EQ(1u,hold.regions.size()); EXPECT_TRUE(hold.regions[0].stationary);
	auto drift = OptimizeMotion(Signals(Linear(30,100,0.1)),{0.35},MotionEncoding::Automatic);
	EXPECT_FALSE(drift.regions[0].stationary);
}

TEST(MotionOptimizer, IrregularFallsBackToExactSamples) {
	auto result = OptimizeMotion(Signals({0,100,-20,90,10,130,-50,50,0,100}),{0.35},MotionEncoding::Automatic);
	EXPECT_TRUE(result.frame_by_frame); EXPECT_EQ(10u,result.regions.size());
}

TEST(MotionOptimizer, ForceFrameByFrameBypassesStaticAndLinear) {
	for (auto const& values : {std::vector<double>(8,100),Linear(8)}) {
		auto result = OptimizeMotion(Signals(values),{0.35},MotionEncoding::FrameByFrame);
		EXPECT_TRUE(result.frame_by_frame); EXPECT_EQ(8u,result.regions.size());
	}
}

TEST(MotionOptimizer, ForceOptimizedKeepsPiecewiseAccuracy) {
	auto samples = Signals({0,40,0,40,0,40,0,40,0,40});
	auto result = OptimizeMotion(samples,{0.35},MotionEncoding::ForceOptimized);
	EXPECT_FALSE(result.frame_by_frame); EXPECT_GT(result.regions.size(),1u);
	for (auto const& region : result.regions) for (size_t i = region.first; i <= region.last; ++i) {
		double p = region.last == region.first ? 0 : (samples[i].time-samples[region.first].time)/(samples[region.last].time-samples[region.first].time);
		EXPECT_LE(std::abs(samples[i].values[0]-(region.from[0]+p*(region.to[0]-region.from[0]))),0.525);
	}
}

TEST(MotionOptimizer, IndependentlySuppressesStationaryAxis) {
	auto samples = Signals(Linear(12));
	for (size_t i = 0; i < samples.size(); ++i) samples[i].values.push_back(i%2 ? 50.02 : 49.98);
	auto result = OptimizeMotion(samples,{0.35,0.35},MotionEncoding::Automatic);
	ASSERT_EQ(1u,result.regions.size());
	EXPECT_DOUBLE_EQ(result.regions[0].from[1],result.regions[0].to[1]);
	EXPECT_NE(result.regions[0].from[0],result.regions[0].to[0]);
}

TEST(MotionOptimizer, VfrUsesMediaTimeInsteadOfFrameIndex) {
	std::vector<MotionSample> samples{{0,0,{0}},{1,40,{4}},{2,110,{11}},{3,150,{15}}};
	auto result = OptimizeMotion(samples,{0.35},MotionEncoding::Automatic);
	ASSERT_EQ(1u,result.regions.size()); EXPECT_FALSE(result.frame_by_frame);
}

TEST(MotionOptimizer, RejectsGapsNonfiniteAndInvalidTolerance) {
	auto samples = Signals(Linear(6)); samples[2].frame = 7;
	EXPECT_THROW(OptimizeMotion(samples,{0.35},MotionEncoding::Automatic),std::invalid_argument);
	samples = Signals(Linear(6)); samples[2].values[0] = std::numeric_limits<double>::infinity();
	EXPECT_THROW(OptimizeMotion(samples,{0.35},MotionEncoding::Automatic),std::invalid_argument);
	EXPECT_THROW(OptimizeMotion(Signals(Linear(6)),{0},MotionEncoding::Automatic),std::invalid_argument);
}

TEST(MotionApply, StaticProducesOnePosWithoutUnnecessaryStyleTags) {
	auto output = Apply(Line(5),Track(std::vector<double>(5,100)));
	ASSERT_EQ(1u,output.events.size());
	EXPECT_DOUBLE_EQ(200,Numeric(output.events[0],"\\pos"));
	EXPECT_TRUE(Tags(output.events[0],"\\move").empty());
	EXPECT_TRUE(Tags(output.events[0],"\\fscx").empty());
}

TEST(MotionApply, LinearUsesOneNativeMoveAndMainReference) {
	auto output = Apply(Line(5),Track(Linear(5)),nullptr,{},2);
	ASSERT_EQ(1u,output.events.size());
	EXPECT_EQ("\\move(190,300,210,300,0,160)",Tags(output.events[0],"\\move")[0]);
	EXPECT_TRUE(Tags(output.events[0],"\\pos").empty());
}

TEST(MotionApply, NoisyLinearAndStationaryJitterStayCompact) {
	auto linear = Apply(Line(5),Track({100,104.999,110.003,114.998,120.001}));
	ASSERT_EQ(1u,linear.events.size()); EXPECT_EQ(1u,Tags(linear.events[0],"\\move").size());
	auto hold = Apply(Line(5),Track({100,100.02,99.99,100.01,100}));
	ASSERT_EQ(1u,hold.events.size()); EXPECT_EQ(1u,Tags(hold.events[0],"\\pos").size());
}

TEST(MotionApply, ThreeRegionsSplitOnlyAtLogicalBoundaries) {
	auto line = Line(12);
	line.Style = "Title"; line.Actor = "Actor"; line.Effect = "Effect"; line.Layer = 3; line.Margin = {{10,20,30}};
	auto output = Apply(line,Track({100,105,110,115,120,120,120,120,120,116,112,108}));
	ASSERT_EQ(3u,output.events.size());
	EXPECT_EQ(1u,Tags(output.events[0],"\\move").size());
	EXPECT_EQ(1u,Tags(output.events[1],"\\pos").size());
	EXPECT_EQ(1u,Tags(output.events[2],"\\move").size());
	EXPECT_EQ(0,int(output.events[0].Start)); EXPECT_EQ(140,int(output.events[0].End));
	EXPECT_EQ(300,int(output.events[1].End)); EXPECT_EQ(480,int(output.events[2].End));
	for (size_t i = 0; i < output.events.size(); ++i) {
		auto const& e = output.events[i];
		EXPECT_EQ(line.Style,e.Style); EXPECT_EQ(line.Actor,e.Actor); EXPECT_EQ(line.Effect,e.Effect);
		EXPECT_EQ(line.Layer,e.Layer); EXPECT_EQ(line.Margin,e.Margin);
		if (i) EXPECT_EQ(output.events[i-1].End,e.Start);
	}
}

TEST(MotionApply, FrameByFramePreservesEverySampleAndBoundaries) {
	MotionApplyOptions o; o.encoding = MotionEncoding::FrameByFrame;
	auto output = Apply(Line(5),Track(Linear(5)),nullptr,o);
	ASSERT_EQ(5u,output.events.size());
	for (size_t i = 0; i < output.events.size(); ++i) {
		EXPECT_DOUBLE_EQ(200+i*5,Numeric(output.events[i],"\\pos"));
		if (i) EXPECT_EQ(output.events[i-1].End,output.events[i].Start);
	}
}

TEST(MotionApply, IndependentAxisSwitches) {
	auto track = Track(Linear(6));
	for (auto& frame : track.frames) frame.y += frame.frame*7;
	MotionApplyOptions o; o.position_x = false;
	auto output = Apply(Line(6),track,nullptr,o);
	ASSERT_EQ(1u,output.events.size());
	EXPECT_DOUBLE_EQ(200,Numeric(output.events[0],"\\move",0));
	EXPECT_DOUBLE_EQ(200,Numeric(output.events[0],"\\move",2));
	EXPECT_DOUBLE_EQ(335,Numeric(output.events[0],"\\move",3));
	o.position_x = true; o.position_y = false;
	output = Apply(Line(6),track,nullptr,o);
	EXPECT_DOUBLE_EQ(300,Numeric(output.events[0],"\\move",1));
	EXPECT_DOUBLE_EQ(300,Numeric(output.events[0],"\\move",3));
}

TEST(MotionApply, SourcePixelCoordinatesConvertToScriptResolution) {
	auto track = Track(Linear(5)); track.source_width = 1280; track.source_height = 960;
	auto output = Apply(Line(5),track);
	ASSERT_EQ(1u,output.events.size());
	EXPECT_DOUBLE_EQ(210,Numeric(output.events[0],"\\move",2));
}

TEST(MotionApply, ScaleAndRotationSimplifyIndependently) {
	auto line = Line(8,"{\\pos(100,100)\\fscx100\\fscy100\\frz5\\bord2\\shad3\\blur1}Sign");
	auto track = Track(std::vector<double>(8,100));
	for (auto& frame : track.frames) { frame.scale_x = frame.scale_y = 1+frame.frame*0.01; frame.rotation_deg = frame.frame; }
	auto output = Apply(line,track);
	ASSERT_EQ(1u,output.events.size());
	EXPECT_EQ(2u,Tags(output.events[0],"\\fscx",true).size());
	EXPECT_EQ(2u,Tags(output.events[0],"\\frz",true).size());
	EXPECT_NE(std::string::npos,output.events[0].Text.get().find("\\frz-2"));
	EXPECT_NE(std::string::npos,output.events[0].Text.get().find("\\xbord2.14"));
}

TEST(MotionApply, RotationMovesOffsetAndUsesOppositeAssAngleSign) {
	auto line = Line(2,"{\\pos(110,100)\\frz0}Sign");
	auto track = Track({100,100}); track.frames[1].rotation_deg = 90;
	MotionApplyOptions o; o.encoding = MotionEncoding::FrameByFrame;
	auto output = Apply(line,track,nullptr,o);
	ASSERT_EQ(2u,output.events.size());
	EXPECT_NEAR(100,Numeric(output.events[1],"\\pos",0),0.001);
	EXPECT_NEAR(110,Numeric(output.events[1],"\\pos",1),0.001);
	EXPECT_DOUBLE_EQ(-90,Numeric(output.events[1],"\\frz"));
}

TEST(MotionApply, MovingOriginFallsBackToValidStandardAss) {
	auto output = Apply(Line(5,"{\\pos(200,300)\\org(100,100)}Sign"),Track(Linear(5)));
	ASSERT_EQ(5u,output.events.size());
	EXPECT_DOUBLE_EQ(120,Numeric(output.events[4],"\\org"));
	EXPECT_TRUE(Tags(output.events[4],"\\t").empty());
}

TEST(MotionApply, SeparateClipTrackIsIndependentFromSubtitle) {
	auto main = Track(std::vector<double>(5,100));
	auto clip = Track(Linear(5,100,10));
	auto output = Apply(Line(5,"{\\pos(200,300)\\clip(0,0,30,40)}Sign"),main,&clip);
	ASSERT_EQ(1u,output.events.size());
	EXPECT_EQ(1u,Tags(output.events[0],"\\pos").size());
	EXPECT_TRUE(Tags(output.events[0],"\\move").empty());
	EXPECT_EQ(2u,Tags(output.events[0],"\\clip",true).size());
	EXPECT_NE(std::string::npos,output.events[0].Text.get().find("\\clip(40,0,70,40)"));
}

TEST(MotionApply, RectangularClipTranslationPreservesInverseSemantics) {
	auto main = Track(std::vector<double>(5,100)), clip = Track(Linear(5,100,10));
	auto output = Apply(Line(5,"{\\iclip(0,0,30,40)}Sign"),main,&clip);
	ASSERT_EQ(1u,output.events.size());
	EXPECT_TRUE(Tags(output.events[0],"\\clip",true).empty());
	EXPECT_EQ(2u,Tags(output.events[0],"\\iclip",true).size());
}

TEST(MotionApply, StandardVectorClipTranslationUsesGeometryFallback) {
	auto main = Track(std::vector<double>(5,100)), clip = Track(Linear(5,100,10));
	auto output = Apply(Line(5,"{\\clip(m 0 0 l 30 0 30 40)}Sign"),main,&clip);
	ASSERT_EQ(5u,output.events.size());
	EXPECT_FALSE(output.used_clippos);
	EXPECT_NE(std::string::npos,output.events[4].Text.get().find("m 40 0 l 70 0 70 40"));
	for (auto const& e : output.events) EXPECT_TRUE(Tags(e,"\\clippos",true).empty());
}

TEST(MotionApply, ScaledVectorClipsKeepDrawingUnitsAndClosingCommands) {
	auto main = Track(std::vector<double>(3,100)), clip = Track(Linear(3,100,10));
	auto output = Apply(Line(3,"{\\iclip(2,m 0 0 l 60 0 60 80 c)}Sign"),main,&clip);
	ASSERT_EQ(3u,output.events.size());
	EXPECT_NE(std::string::npos,output.events[2].Text.get().find("\\iclip(2,m 40 0 l 100 0 100 80 c"));
}

TEST(MotionApply, MangetsuClipposKeepsVectorGeometryStable) {
	MotionApplyOptions o; o.mangetsu_clippos = true;
	auto main = Track(std::vector<double>(5,100)), clip = Track(Linear(5,100,10));
	auto line = Line(5,"{\\iclip(m 0 0 l 30 0 30 40)}Sign");
	auto output = Apply(line,main,&clip,o);
	ASSERT_EQ(1u,output.events.size()); EXPECT_TRUE(output.used_clippos);
	EXPECT_EQ(1u,Tags(output.events[0],"\\iclip").size());
	EXPECT_EQ(2u,Tags(output.events[0],"\\clippos",true).size());
	EXPECT_NE(std::string::npos,output.events[0].Text.get().find("\\clippos(40,0)"));
	o.mangetsu_clippos = false;
	EXPECT_EQ(5u,Apply(line,main,&clip,o).events.size());
}

TEST(MotionApply, MangetsuRectangularClipposAndRotationFallback) {
	MotionApplyOptions o; o.mangetsu_clippos = true;
	auto main = Track(std::vector<double>(5,100)), clip = Track(Linear(5));
	auto line = Line(5,"{\\clip(0,0,30,40)}Sign");
	auto output = Apply(line,main,&clip,o);
	ASSERT_EQ(1u,output.events.size()); EXPECT_TRUE(output.used_clippos);
	for (auto& frame : clip.frames) frame.rotation_deg = frame.frame*2;
	output = Apply(line,main,&clip,o);
	ASSERT_EQ(5u,output.events.size()); EXPECT_FALSE(output.used_clippos);
	EXPECT_NE(std::string::npos,Tags(output.events[0],"\\clip")[0].find("m "));
}

TEST(MotionApply, UnrelatedTagsAndDuplicatePositionNormalization) {
	auto line = Line(5,"{\\pos(200,300)\\move(0,0,10,10)\\an7\\fnArial\\1c&H112233&\\i1\\foo42}Hello{\\b1} world");
	auto output = Apply(line,Track(Linear(5)));
	ASSERT_EQ(1u,output.events.size());
	EXPECT_EQ(1u,Tags(output.events[0],"\\move").size());
	EXPECT_TRUE(Tags(output.events[0],"\\pos").empty());
	for (auto const& tag : {"\\an7","\\fnArial","\\1c&H112233&","\\i1","\\foo42","\\b1"})
		EXPECT_NE(std::string::npos,output.events[0].Text.get().find(tag));
	AssDialogue generated(output.events[0]); EXPECT_EQ("Hello world",generated.GetStrippedText());
}

TEST(MotionApply, NamedResetUsesItsNativeStyleAndPreservesInlineStyling) {
	auto line = Line(4,"{\\pos(100,100)\\fscx120}A{\\rOther\\i1}B");
	AssStyle base, other; other.scalex = 150;
	auto track = Track(std::vector<double>(4,100));
	for (auto& frame : track.frames) frame.scale_x = frame.scale_y = 1+frame.frame*0.1;
	auto output = BuildMotionApplication(line,[&](std::string const& name) { return name == "Other" ? &other : &base; },
		track,nullptr,0,agi::vfr::Framerate(25.0),640,480);
	ASSERT_EQ(1u,output.events.size());
	EXPECT_NE(std::string::npos,output.events[0].Text.get().find("\\rOther"));
	EXPECT_NE(std::string::npos,output.events[0].Text.get().find("\\fscx195"));
	EXPECT_NE(std::string::npos,output.events[0].Text.get().find("\\i1"));
}

TEST(MotionApply, ScalarTransformsComposeWithTrackingWithoutContradictions) {
	auto line = Line(5,"{\\pos(100,100)\\fscx100\\t(0,160,\\fscx140\\1c&HFF0000&)}Sign");
	auto track = Track(std::vector<double>(5,100));
	for (auto& frame : track.frames) frame.scale_x = frame.scale_y = 1+frame.frame*0.05;
	MotionApplyOptions o; o.encoding = MotionEncoding::FrameByFrame;
	auto output = Apply(line,track,nullptr,o);
	ASSERT_EQ(5u,output.events.size());
	EXPECT_DOUBLE_EQ(168,Numeric(output.events[4],"\\fscx"));
	EXPECT_EQ(1u,Tags(output.events[4],"\\fscx",true).size());
	EXPECT_EQ(1u,Tags(output.events[4],"\\1c",true).size());
}

TEST(MotionApply, FadeAndUnrelatedTransformsRetainOriginalClockWhenSplit) {
	auto line = Line(5,"{\\pos(200,300)\\fad(80,40)\\t(0,200,\\1c&HFF0000&)}Sign");
	MotionApplyOptions o; o.encoding = MotionEncoding::FrameByFrame;
	auto output = Apply(line,Track(Linear(5)),nullptr,o);
	ASSERT_EQ(5u,output.events.size());
	EXPECT_TRUE(Tags(output.events[2],"\\fad").empty());
	EXPECT_EQ("\\fade(255,0,255,-60,20,100,140)",Tags(output.events[2],"\\fade")[0]);
	EXPECT_NE(std::string::npos,output.events[2].Text.get().find("\\t(-60,140,"));
}

TEST(MotionApply, ExistingMoveIsSampledAtReferenceAndComposed) {
	auto line = Line(5,"{\\move(100,100,140,100,0,160)}Sign");
	auto output = Apply(line,Track(Linear(5)),nullptr,{},2);
	ASSERT_EQ(1u,output.events.size());
	EXPECT_DOUBLE_EQ(90,Numeric(output.events[0],"\\move",0));
	EXPECT_DOUBLE_EQ(150,Numeric(output.events[0],"\\move",2));
}

TEST(MotionApply, VfrSharedBoundariesSurviveAssSerialization) {
	agi::vfr::Framerate tc({0,40,110,150,210,250});
	auto line = Line(5); line.Start = 7; line.End = 230;
	auto track = Track({0,4,11,15,21});
	MotionApplyOptions o; o.encoding = MotionEncoding::FrameByFrame;
	AssStyle style;
	auto output = BuildMotionApplication(line,[&](std::string const&) { return &style; },track,nullptr,2,tc,640,480,o);
	ASSERT_EQ(4u,output.events.size()); // START(7) is frame 1; END(230) is frame 4
	EXPECT_EQ(7,int(output.events.front().Start)); EXPECT_EQ(230,int(output.events.back().End));
	for (size_t i = 1; i < output.events.size(); ++i) {
		EXPECT_EQ(output.events[i-1].End,output.events[i].Start);
		EXPECT_EQ(int(output.events[i].Start)%10,0);
		EXPECT_EQ(int(i)+1,tc.FrameAtTime(output.events[i].Start,agi::vfr::START));
		EXPECT_EQ(int(i),tc.FrameAtTime(output.events[i-1].End,agi::vfr::END));
	}
}

TEST(MotionApply, GapsLostFramesAndOutOfRangeReferenceRejectBeforeMutation) {
	auto track = Track(Linear(5)); track.frames.erase(track.frames.begin()+2);
	EXPECT_THROW(Apply(Line(5),track),std::invalid_argument);
	track = Track(Linear(5)); track.frames[2].state = MotionTrackState::Lost;
	EXPECT_THROW(Apply(Line(5),track),std::invalid_argument);
	EXPECT_THROW(Apply(Line(5),Track(Linear(5)),nullptr,{},20),std::invalid_argument);
}

TEST(MotionApply, UnsupportedAnimatedClipAndKaraokeFailClearly) {
	auto track = Track(Linear(5));
	EXPECT_THROW(Apply(Line(5,"{\\clip(0,0,10,10)\\t(\\clip(10,10,20,20))}Sign"),track,&track),std::invalid_argument);
	MotionApplyOptions o; o.encoding = MotionEncoding::FrameByFrame;
	EXPECT_THROW(Apply(Line(5,"{\\k20}Sign"),track,nullptr,o),std::invalid_argument);
}

TEST(MotionSession, AllowsPositioningStylingButRejectsChangedIdentityTimingContentOrClip) {
	auto line = Line(5,"{\\pos(100,100)\\clip(0,0,10,10)}Sign");
	std::vector<MotionSourceIdentity> sources{IdentifyMotionSource(line)};
	std::vector<AssDialogue const*> selected{&line};
	int active = line.Id;
	line.Text = "{\\pos(200,200)\\fscx120\\1c&H123456&\\clip(0,0,10,10)}Sign";
	EXPECT_TRUE(ValidateMotionSources(sources,selected,active,active,true).empty());
	line.Text = "{\\pos(200,200)\\clip(0,0,20,20)}Sign";
	EXPECT_FALSE(ValidateMotionSources(sources,selected,active,active,true).empty());
	EXPECT_TRUE(ValidateMotionSources(sources,selected,active,active,false).empty());
	line.Text = "Other sign";
	EXPECT_FALSE(ValidateMotionSources(sources,selected,active,active,false).empty());
	line.Text = "Sign"; line.Start = 10;
	EXPECT_FALSE(ValidateMotionSources(sources,selected,active,active,false).empty());
	line.Start = 0;
	EXPECT_FALSE(ValidateMotionSources(sources,selected,active,active+1,false).empty());
	EXPECT_FALSE(ValidateMotionSources(sources,{},active,active,false).empty());
}

TEST(MotionRevert, RestoresEntireFamilyAndOriginalFieldsFromAnyGeneratedEvent) {
	AssFile file;
	auto source = new AssDialogue(Line(5,"{\\pos(200,300)\\i1}Original"));
	source->Actor = "Actor"; source->Effect = "Effect"; source->Layer = 4; source->Margin = {{1,2,3}};
	file.Events.push_back(*source);
	file.SetExtradataValue(*source,"other-plugin","original metadata");
	AssDialogueBase original(*source);
	int source_id = source->Id;
	MotionApplyOptions o; o.encoding = MotionEncoding::FrameByFrame;
	auto installed = InstallMotionApplications(file,{{source_id,Apply(*source,Track(Linear(5)),nullptr,o)}},source_id);
	ASSERT_EQ(5u,installed.selected.size());
	EXPECT_TRUE(CanRevertMotion(file,*installed.selected[3]));
	for (int i = 0; i < 12; ++i) file.CleanExtradata();
	auto restored = RevertMotionFamilies(file,{installed.selected[3]},installed.selected[3]->Id);
	ASSERT_EQ(1u,restored.selected.size()); EXPECT_EQ(1,std::distance(file.Events.begin(),file.Events.end()));
	auto line = restored.active;
	EXPECT_EQ(original.Text,line->Text); EXPECT_EQ(original.Style,line->Style);
	EXPECT_EQ(original.Actor,line->Actor); EXPECT_EQ(original.Effect,line->Effect);
	EXPECT_EQ(original.Margin,line->Margin); EXPECT_EQ(original.Layer,line->Layer);
	EXPECT_EQ(original.Start,line->Start); EXPECT_EQ(original.End,line->End);
	EXPECT_FALSE(CanRevertMotion(file,*line));
	auto extra = file.GetExtradata(line->ExtradataIds.get());
	ASSERT_EQ(1u,extra.size()); EXPECT_EQ("original metadata",extra[0].value);
}

TEST(MotionRevert, MultipleSourceFamiliesAndUnrelatedEventsStaySeparate) {
	AssFile file;
	auto a = new AssDialogue(Line(5,"A")), b = new AssDialogue(Line(5,"B")), unrelated = new AssDialogue(Line(5,"Unrelated"));
	file.Events.push_back(*a); file.Events.push_back(*unrelated); file.Events.push_back(*b);
	int active = b->Id;
	MotionApplyOptions o; o.encoding = MotionEncoding::FrameByFrame;
	auto installed = InstallMotionApplications(file,{{a->Id,Apply(*a,Track(Linear(5)),nullptr,o)},
		{b->Id,Apply(*b,Track(Linear(5)),nullptr,o)}},active);
	ASSERT_EQ(10u,installed.selected.size());
	auto restored = RevertMotionFamilies(file,{installed.selected[1],installed.selected[6]},installed.selected[6]->Id);
	ASSERT_EQ(2u,restored.selected.size());
	EXPECT_EQ("B",restored.active->Text.get());
	EXPECT_EQ(3,std::distance(file.Events.begin(),file.Events.end()));
	EXPECT_EQ("Unrelated",unrelated->Text.get());
}

TEST(MotionRevert, IncompleteFamilyAndDuplicateApplyAreRejected) {
	AssFile file;
	auto source = new AssDialogue(Line(5)); file.Events.push_back(*source);
	MotionApplyOptions o; o.encoding = MotionEncoding::FrameByFrame;
	auto installed = InstallMotionApplications(file,{{source->Id,Apply(*source,Track(Linear(5)),nullptr,o)}},source->Id);
	auto first = installed.selected[0];
	EXPECT_THROW(InstallMotionApplications(file,{{first->Id,Apply(*first,Track(Linear(5)))}},first->Id),std::invalid_argument);
	auto removed = installed.selected.back(); file.Events.erase(file.Events.iterator_to(*removed)); delete removed;
	EXPECT_THROW(RevertMotionFamilies(file,{first},first->Id),std::invalid_argument);
	EXPECT_EQ(4,std::distance(file.Events.begin(),file.Events.end()));
}

TEST(MotionRevert, InvalidLaterPlanDoesNotPartiallyMutateSelection) {
	AssFile file;
	auto source = new AssDialogue(Line(5)); file.Events.push_back(*source);
	auto application = Apply(*source,Track(Linear(5)));
	EXPECT_THROW(InstallMotionApplications(file,{{source->Id,application},{-1,application}},source->Id),std::invalid_argument);
	EXPECT_EQ(1,std::distance(file.Events.begin(),file.Events.end()));
	EXPECT_EQ("{\\pos(200,300)}Sign",source->Text.get());
}

TEST(MotionRevert, ExactOuterMillisecondsAndExtradataSurviveMetadataRoundtrip) {
	AssFile file;
	auto source = new AssDialogue(Line(5)); source->Start = 7; source->End = 193; file.Events.push_back(*source);
	file.SetExtradataValue(*source,"plugin","line\nwith commas, and \\slashes");
	auto installed = InstallMotionApplications(file,{{source->Id,Apply(*source,Track(Linear(5)))}},source->Id);
	auto restored = RevertMotionFamilies(file,installed.selected,installed.active->Id);
	EXPECT_EQ(7,int(restored.active->Start)); EXPECT_EQ(193,int(restored.active->End));
	EXPECT_EQ("line\nwith commas, and \\slashes",file.GetExtradata(restored.active->ExtradataIds.get())[0].value);
}

TEST(MotionExtradata, MissingIdNeverResolvesToAnotherPluginsData) {
	AssFile file;
	auto first = file.AddExtradata("first","a");
	auto second = file.AddExtradata("second","b");
	file.Extradata.erase(file.Extradata.begin());
	EXPECT_TRUE(file.GetExtradata({first}).empty());
	AssDialogue line; line.ExtradataIds = std::vector<uint32_t>{first,second};
	file.DeleteExtradataValue(line,"second");
	EXPECT_TRUE(file.GetExtradata(line.ExtradataIds.get()).empty());
}
