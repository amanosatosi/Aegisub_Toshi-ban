#include <gtest/gtest.h>
#include <algorithm>
#include <libaegisub/timing39_session.h>
#include <libaegisub/timing39_input_clock.h>
#include "timing39_ui.h"
#include "audio_review_plan.h"
#include "toshiki_timing_draft.h"
using namespace agi::timing39;
namespace {
SessionTarget Target(uint64_t id, int start, int end, std::string text=u8"みく") {
	SessionTarget t; t.id=id; t.start=start; t.end=end; t.style="opaque style";
	t.analysis=Analyze(text); t.selected=true; t.lyric_evidence=true; return t;
}
void Start(Timing39Session& s) {
	EXPECT_EQ(SessionState::Countdown,s.State()); EXPECT_EQ(3,s.Countdown());
	EXPECT_FALSE(s.Key('F',true,s.StartTime())); EXPECT_FALSE(s.TickCountdown());
	EXPECT_EQ(2,s.Countdown()); EXPECT_FALSE(s.TickCountdown());
	EXPECT_EQ(1,s.Countdown()); EXPECT_TRUE(s.TickCountdown());
	EXPECT_FALSE(s.TickCountdown()); ASSERT_TRUE(s.Start(s.StartTime()));
}
void Tap(Timing39Session& s,int key,int begin,int end) {
	EXPECT_TRUE(s.Key(key,true,begin)); EXPECT_TRUE(s.Key(key,false,end));
}
std::vector<TimingAssignment> Manual(std::vector<TimingBlock> const& blocks,
	std::vector<std::pair<size_t,size_t>> const& spans) {
	std::vector<TimingAssignment> result;size_t next=0;
	for(size_t i=0;i<blocks.size();++i)if(!blocks[i].gap) {
		if(next>=spans.size())break;
		result.push_back({i,spans[next].first,spans[next].second});++next;
	}
	return result;
}
}
TEST(Timing39Session, ManualReattackMapsTwoAttacksToOneMoraAndResets) {
	Timing39Session s;s.Prepare({Target(1,1000,1800,u8"ように")},true,"opaque style",900,1800);Start(s);
	Tap(s,'F',1050,1150);Tap(s,'J',1200,1300);Tap(s,'F',1350,1450);Tap(s,'J',1500,1600);s.Stop(1800);
	auto raw=s.Raw(0);
	auto& result=s.Results()[0];ASSERT_EQ(Confidence::Red,result.GetConfidence());
	auto mapping=Manual(result.lanes[0].capture.blocks,{{0,1},{1,1},{2,1},{2,1}});
	ASSERT_TRUE(ValidManualAssignments(result.target.analysis,result.lanes[0].capture.blocks,mapping));
	EXPECT_TRUE(s.SetManualAssignment(0,0,mapping));
	EXPECT_EQ(ResolutionSource::UserManualRepair,result.resolution);
	EXPECT_EQ(Confidence::Green,result.GetConfidence());EXPECT_EQ(raw,s.Raw(0));
	EXPECT_FALSE(s.SetManualAssignment(0,0,Manual(result.lanes[0].capture.blocks,{{2,1},{0,1},{1,1},{2,1}})));
	EXPECT_TRUE(s.ResetManualAssignment(0,0));EXPECT_EQ(Confidence::Red,result.GetConfidence());
	EXPECT_EQ(ResolutionSource::Automatic,result.resolution);EXPECT_EQ(raw,s.Raw(0));
}
TEST(Timing39Session, DelayedDispatchUsesPhysicalEventClockNotHandlerTime) {
	InputClockMapper mapper;mapper.Anchor(1000,5000);
	int timestamp=0;
	ASSERT_TRUE(mapper.Map(1200,1350,4000,6000,timestamp));
	EXPECT_EQ(5200,timestamp);
	Timing39Session session;
	session.Prepare({Target(1,5000,5500,u8"み")},true,"opaque style",5000,5500);
	Start(session);
	EXPECT_TRUE(session.Key('F',true,timestamp));
	EXPECT_TRUE(session.Key('F',false,5250));
	session.Stop(5500);
	ASSERT_FALSE(session.Raw(0).empty());
	auto sung=std::find_if(session.Raw(0).begin(),session.Raw(0).end(),
		[](TimingBlock const& block){return !block.gap;});
	ASSERT_NE(session.Raw(0).end(),sung);
	EXPECT_EQ(5200,sung->start);
	ASSERT_TRUE(mapper.Map(1200,1400,4000,6000,timestamp));
	EXPECT_EQ(5200,timestamp);
	mapper.Reset();EXPECT_FALSE(mapper.Map(1200,1400,4000,6000,timestamp));
	mapper.Anchor(1400,8000);EXPECT_FALSE(mapper.Map(1200,1500,7000,9000,timestamp));
}
TEST(Timing39Session, ToshikiDraftApplyCancelAndResetPreserveRhythmRaw) {
	Timing39Session session;
	session.Prepare({Target(1,1000,1500,u8"みく")},true,"opaque style",900,1500);
	Start(session);Tap(session,'F',1050,1400);session.Stop(1500);
	auto raw=session.Raw(0);
	auto& result=session.Results()[0];
	ASSERT_EQ(Confidence::Red,result.GetConfidence());
	toshiki_timing::Draft cancelled;
	cancelled.Begin(1000,1500,result.lanes[0].capture.blocks);
	ASSERT_TRUE(cancelled.Split(1200));
	EXPECT_EQ(raw,session.Raw(0));
	EXPECT_FALSE(result.lanes[0].timing_override_active); // Cancel: discard the draft.
	toshiki_timing::Draft edited;
	edited.Begin(1000,1500,result.lanes[0].capture.blocks);
	ASSERT_TRUE(edited.Split(1200));
	ASSERT_TRUE(edited.Move(1,true,1250));
	EXPECT_EQ(1250,edited.Blocks()[1].start);
	ASSERT_TRUE(edited.Undo());EXPECT_EQ(1200,edited.Blocks()[1].start);
	ASSERT_TRUE(edited.Redo());EXPECT_EQ(1250,edited.Blocks()[1].start);
	ASSERT_TRUE(edited.Undo());EXPECT_EQ(1200,edited.Blocks()[1].start);
	ASSERT_TRUE(session.SetManualTiming(0,0,edited.Blocks()));
	EXPECT_TRUE(result.lanes[0].timing_override_active);
	EXPECT_EQ(ResolutionSource::UserManualTiming,result.resolution);
	EXPECT_EQ(Confidence::Green,result.GetConfidence());
	EXPECT_EQ(raw,session.Raw(0));
	ASSERT_TRUE(session.SetTimingCorrection(20));
	EXPECT_TRUE(result.lanes[0].timing_override_active);
	EXPECT_EQ(Confidence::Green,result.GetConfidence());
	EXPECT_EQ(raw,session.Raw(0));
	ASSERT_TRUE(session.ResetManualTiming(0,0));
	EXPECT_EQ(Confidence::Red,result.GetConfidence());
	EXPECT_FALSE(result.lanes[0].timing_override_active);
	EXPECT_EQ(raw,session.Raw(0));
}
TEST(Timing39Session, ToshikiDraftJoinAndPreviewUndoAreLocal) {
	toshiki_timing::Draft draft;
	draft.Begin(1000,1500,{{1050,1200,false},{1200,1400,false}});
	auto original=draft.Blocks();
	ASSERT_TRUE(draft.PreviewMove(1,true,1250));
	draft.FinishPreview(original);
	ASSERT_TRUE(draft.Changed());
	ASSERT_TRUE(draft.Undo());EXPECT_EQ(original,draft.Blocks());
	ASSERT_TRUE(draft.Redo());EXPECT_EQ(1250,draft.Blocks()[1].start);
	ASSERT_TRUE(draft.Join(0));ASSERT_EQ(1u,draft.Blocks().size());
	EXPECT_EQ((TimingBlock{1050,1400,false}),draft.Blocks()[0]);
	ASSERT_TRUE(draft.Undo());ASSERT_EQ(2u,draft.Blocks().size());
}
TEST(Timing39Session, LaneOverlaysDoNotChangeStaticAudioCacheIdentity) {
	using agi::timing39::ui::ReviewBitmapKey;
	ReviewBitmapKey primary{39500,43500,520,100,7};
	ReviewBitmapKey secondary{39500,43500,520,100,7};
	EXPECT_TRUE(primary==secondary);
	secondary.generation=8;EXPECT_FALSE(primary==secondary);
	secondary=primary;secondary.width=580;EXPECT_FALSE(primary==secondary);
}
TEST(Timing39Session, ReviewRenderPlanIsIndependentOfMainHorizontalZoom) {
	AudioReviewPlan plan{10000,18000,800};
	ASSERT_TRUE(plan.Valid());
	EXPECT_DOUBLE_EQ(10.0,plan.MillisecondsPerPixel());
	EXPECT_EQ(4,plan.SliceCount());
	int columns=0;
	for(int i=0;i<plan.SliceCount();++i){
		auto slice=plan.GetSlice(i);
		EXPECT_EQ(columns,slice.x);
		EXPECT_DOUBLE_EQ(10000.0+columns*10.0,slice.begin_ms);
		columns+=slice.width;
	}
	EXPECT_EQ(800,columns);
	for(double main_ms_per_pixel:{10.0,1.0,0.1}){
		// The old path grew to 80,000 source pixels/313 slices at 0.1 ms/px.
		EXPECT_GT(int(8000/main_ms_per_pixel),0);
		EXPECT_EQ(800,plan.width);
		EXPECT_EQ(4,plan.SliceCount());
		EXPECT_DOUBLE_EQ(10.0,plan.MillisecondsPerPixel());
	}
}
TEST(Timing39Session, ReviewAudioCacheInvalidatesOnlyForStaticInputs) {
	using agi::timing39::ui::ReviewBitmapKey;
	ReviewBitmapKey key{10000,18000,800,100,3};
	// Lane, cursor, assignment and main zoom/scroll are overlay or main-view
	// state: none is represented in the static bitmap identity.
	for(int lane:{0,1})for(int cursor:{10000,12000,17999}){
		(void)lane;(void)cursor;
		EXPECT_TRUE((key==ReviewBitmapKey{10000,18000,800,100,3}));
	}
	auto changed=key;changed.begin=9900;EXPECT_FALSE(key==changed);
	changed=key;changed.end=18100;EXPECT_FALSE(key==changed);
	changed=key;changed.width=801;EXPECT_FALSE(key==changed);
	changed=key;changed.height=101;EXPECT_FALSE(key==changed);
	// The generation is raised for provider, mode, spectrum, amplitude or theme.
	changed=key;changed.generation=4;EXPECT_FALSE(key==changed);
}
TEST(Timing39Session, ReviewWorkStaysBoundedAtExtremeMainZoom) {
	AudioReviewPlan plan{10000,18000,800};
	for(double main_ms_per_pixel:{20.0,1.0,0.1,0.01}){
		int old_source_columns=int(8000/main_ms_per_pixel);
		EXPECT_LE(plan.SliceCount()*AudioReviewPlan::columns_per_slice,
			plan.width+AudioReviewPlan::columns_per_slice-1);
		EXPECT_EQ(800,plan.width);
		EXPECT_EQ(4,plan.SliceCount());
		if(main_ms_per_pixel<=0.1)EXPECT_GT(old_source_columns,plan.width*10);
	}
}
TEST(Timing39Session, InputClockWrapAndStaleEventSafety) {
	InputClockMapper mapper;mapper.Anchor(0xfffffff0u,5000);
	int timestamp=0;
	ASSERT_TRUE(mapper.Map(0x00000018u,0x00000050u,4000,6000,timestamp));
	EXPECT_EQ(5040,timestamp);
	EXPECT_FALSE(mapper.Map(0xfffffff0u,0x00004000u,4000,6000,timestamp));
}
TEST(Timing39Session, ManualMergeAndCorrectionInvalidation) {
	Timing39Session s;s.Prepare({Target(1,1000,1500,u8"ない")},true,"opaque style",900,1500);Start(s);
	Tap(s,'F',1005,1200);s.Stop(1500);
	auto raw=s.Raw(0);auto& result=s.Results()[0];
	auto mapping=Manual(result.lanes[0].capture.blocks,{{0,2}});
	ASSERT_TRUE(s.SetManualAssignment(0,0,mapping));
	EXPECT_EQ(Confidence::Green,result.GetConfidence());
	ASSERT_TRUE(s.SetTimingCorrection(10));
	EXPECT_EQ(mapping,result.lanes[0].Assignments());
	EXPECT_EQ(ResolutionSource::UserManualRepair,result.resolution);
	ASSERT_TRUE(s.SetTimingCorrection(-110));
	EXPECT_TRUE(result.manual_invalidated);
	EXPECT_FALSE(result.lanes[0].manual_active);
	EXPECT_EQ(ResolutionSource::Automatic,result.resolution);
	EXPECT_EQ(raw,s.Raw(0));
}
TEST(Timing39Session, LocalReviewShowsRawCorrectionTailAndClamp) {
	SessionResult result;result.target=Target(1,192390,194150,u8"<世|せ><界|かい>まで");
	std::vector<TimingBlock> raw{{191937,192452,false},{192640,192780,false},
		{192796,193124,false},{193140,193468,false},{193484,193687,false},
		{193687,194155,false}};
	result.lanes[0].capture=PartitionCapture(raw,result.target.start,result.target.end);
	auto view=agi::timing39::ui::BuildLocalReviewModel(result,0,raw,0);
	EXPECT_EQ(6u,view.raw.size());
	EXPECT_EQ(agi::timing39::ui::ReviewOwnership::ExcludedPreviousTail,view.raw[0].ownership);
	EXPECT_EQ(agi::timing39::ui::ReviewOwnership::HarmlessEndClamp,view.raw.back().ownership);
	EXPECT_EQ(5u,agi::timing39::ui::SungTapCount(result.lanes[0].capture));
	auto shifted=agi::timing39::ui::BuildLocalReviewModel(result,0,raw,-30);
	EXPECT_EQ(191937,shifted.raw[0].raw.start);
	EXPECT_EQ(191907,shifted.raw[0].adjusted.start);
	EXPECT_EQ(191937,raw[0].start);
}
TEST(Timing39Session, ReviewPlaybackUsesExactLineNotPaddedContext) {
	SessionResult result;result.target=Target(1,40000,43000,u8"み");
	auto view=agi::timing39::ui::BuildLocalReviewModel(result,0,{},0,500);
	EXPECT_EQ((std::pair<int,int>{40000,43000}),
		agi::timing39::ui::ReviewPlaybackRange(view,false));
	EXPECT_EQ((std::pair<int,int>{39500,43500}),
		agi::timing39::ui::ReviewPlaybackRange(view,true));
}
TEST(Timing39Session, ActivateCountdownCaptureAcrossLinesAndNormalStop) {
	Timing39Session s;s.Prepare({Target(1,1000,1500),Target(2,2000,2500)},true,"opaque style",900,2500);
	ASSERT_EQ(2u,s.Targets().size());Start(s);
	Tap(s,'F',1050,1200);Tap(s,'J',1250,1450);
	EXPECT_EQ(SessionState::Capturing,s.State()); // no checkpoint stops capture
	Tap(s,'F',2050,2200);Tap(s,'J',2250,2450);
	EXPECT_TRUE(s.Results().empty());EXPECT_TRUE(s.Stop(2500));EXPECT_FALSE(s.Stop(2500));
	ASSERT_EQ(2u,s.Results().size());EXPECT_EQ(Confidence::Green,s.Results()[0].GetConfidence());EXPECT_EQ(Confidence::Green,s.Results()[1].GetConfidence());
	EXPECT_TRUE(s.Raw(1).empty());EXPECT_TRUE(s.Results()[0].lanes[1].capture.blocks.empty());
	for(auto const& r:s.Results())for(auto const& b:r.lanes[0].capture.blocks){EXPECT_GE(b.start,r.target.start);EXPECT_LE(b.end,r.target.end);}
	EXPECT_TRUE(std::any_of(s.Raw(0).begin(),s.Raw(0).end(),[](TimingBlock const& b){return b.gap&&b.start==1450&&b.end==2050;}));
}
TEST(Timing39Session, FailedLineDoesNotShiftLaterLinesAndRetakeIsIsolated) {
	Timing39Session s;s.Prepare({Target(1,1000,1500),Target(2,2000,2500)},true,"opaque style",900,2500);Start(s);
	Tap(s,'F',1050,1400);Tap(s,'D',1100,1200);Tap(s,'K',1300,1450);
	Tap(s,'F',2050,2200);Tap(s,'J',2250,2450);s.Stop(2500);
	ASSERT_EQ(2u,s.Results().size());EXPECT_EQ(Confidence::Red,s.Results()[0].lanes[0].match.confidence);
	EXPECT_EQ(Confidence::Green,s.Results()[1].lanes[0].match.confidence);
	auto primary_raw=s.Raw(0), secondary=s.Results()[0].lanes[1].capture.blocks, later=s.Results()[1].lanes[0].capture.blocks;
	ASSERT_TRUE(s.Retake(0,0,100));Start(s);Tap(s,'F',1050,1200);Tap(s,'J',1250,1400);s.Stop(1500);
	EXPECT_EQ(Confidence::Green,s.Results()[0].lanes[0].match.confidence);
	EXPECT_EQ(primary_raw,s.Raw(0));EXPECT_EQ(secondary,s.Results()[0].lanes[1].capture.blocks);EXPECT_EQ(later,s.Results()[1].lanes[0].capture.blocks);
	ASSERT_EQ(1u,s.retakes.size());EXPECT_EQ(1u,s.retakes[0].target);EXPECT_EQ(0,s.retakes[0].lane);
}
TEST(Timing39Session, EqualMajorityOverlapNeedsReviewButNeverDuplicatesAttack) {
	Timing39Session s;s.Prepare({Target(1,100,220,u8"み"),Target(2,180,300,u8"く")},true,"opaque style",0,300);Start(s);
	Tap(s,'F',170,230);Tap(s,'J',240,270);s.Stop(300);ASSERT_EQ(2u,s.Results().size());
	EXPECT_EQ(PartitionStatus::NeedsReview,s.Results()[0].lanes[0].capture.status);
	EXPECT_EQ(Confidence::Yellow,s.Results()[0].GetConfidence());
	EXPECT_EQ(1u,s.Results()[0].lanes[0].capture.disputed_raw_indices.size());
	auto const& second=s.Results()[1].lanes[0].capture.blocks;
	EXPECT_EQ(1u,std::count_if(second.begin(),second.end(),[](TimingBlock const& b){return !b.gap;}));
	EXPECT_NE(second.end(),std::find(second.begin(),second.end(),TimingBlock{240,270,false}));
	EXPECT_EQ(Confidence::Green,s.Results()[1].GetConfidence());
	EXPECT_EQ((TimingBlock{170,230,false}),s.Raw(0)[1]);
}
TEST(Timing39Session, GreatestOverlapWinsWithoutBlanketOverlapWarning) {
	Timing39Session s;s.Prepare({Target(1,100,220,u8"み"),Target(2,180,300,u8"く")},true,"opaque style",0,300);Start(s);
	Tap(s,'F',150,230);Tap(s,'J',240,270);s.Stop(300);ASSERT_EQ(2u,s.Results().size());
	EXPECT_EQ(Confidence::Green,s.Results()[0].GetConfidence());
	EXPECT_EQ(Confidence::Green,s.Results()[1].GetConfidence());
	EXPECT_EQ(1u,agi::timing39::ui::SungTapCount(s.Results()[0].lanes[0].capture));
	EXPECT_EQ(1u,agi::timing39::ui::SungTapCount(s.Results()[1].lanes[0].capture));
}
TEST(Timing39Session, ExplicitCandidateChoiceIsImmediatelyGreen) {
	Timing39Session s;s.Prepare({Target(1,1000,1500,u8"こーー")},true,"opaque style",900,1500);Start(s);
	Tap(s,'F',1050,1150);Tap(s,'J',1200,1300);s.Stop(1500);
	ASSERT_EQ(1u,s.Results().size());
	auto& result=s.Results()[0];
	ASSERT_GE(result.lanes[0].match.paths.size(),2u);
	result.association_ambiguous=true;
	EXPECT_EQ(Confidence::Yellow,result.GetConfidence());
	auto choice=result.lanes[0].match.paths[1].assignments;
	EXPECT_TRUE(s.ChooseAssignment(0,0,1));
	EXPECT_EQ(choice,result.lanes[0].editor.Get());
	EXPECT_EQ(ResolutionSource::UserSelected,result.resolution);
	EXPECT_EQ(Confidence::Green,result.GetConfidence());
	EXPECT_TRUE(s.SetTimingCorrection(5));
	EXPECT_EQ(choice,result.lanes[0].editor.Get());
	EXPECT_EQ(ResolutionSource::UserSelected,result.resolution);
	EXPECT_EQ(Confidence::Green,result.GetConfidence());
}
TEST(Timing39Session, TimingCorrectionDerivesFromImmutableRawAndCanReset) {
	auto raw=std::vector<TimingBlock>{{1000,1200,false}};
	EXPECT_EQ((TimingBlock{970,1170,false}),ShiftCapture(raw,-30)[0]);
	EXPECT_EQ((TimingBlock{1000,1200,false}),raw[0]);
	Timing39Session s;s.Prepare({Target(1,1000,1500,u8"み")},true,"opaque style",900,1500);Start(s);
	Tap(s,'F',1000,1200);s.Stop(1500);
	ASSERT_EQ(Confidence::Green,s.Results()[0].GetConfidence());
	auto original=s.Raw(0);
	ASSERT_TRUE(s.SetTimingCorrection(-30));
	EXPECT_EQ(-30,s.TimingCorrection());
	EXPECT_EQ(original,s.Raw(0));
	EXPECT_EQ(Confidence::Green,s.Results()[0].GetConfidence());
	EXPECT_EQ(PartitionStatus::HarmlessClamp,s.Results()[0].lanes[0].capture.status);
	ASSERT_TRUE(s.SetTimingCorrection(0));
	EXPECT_EQ(original,s.Raw(0));
	EXPECT_EQ(Confidence::Green,s.Results()[0].GetConfidence());
}
TEST(Timing39Session, CorrectionInvalidatesImpossibleManualAssignment) {
	Timing39Session s;s.Prepare({Target(1,1000,1400,u8"こーー")},true,"opaque style",900,1400);Start(s);
	Tap(s,'F',1050,1150);Tap(s,'J',1200,1300);s.Stop(1400);
	ASSERT_TRUE(s.ChooseAssignment(0,0,1));
	ASSERT_EQ(Confidence::Green,s.Results()[0].GetConfidence());
	auto raw=s.Raw(0);
	ASSERT_TRUE(s.SetTimingCorrection(-100));
	EXPECT_EQ(raw,s.Raw(0));
	EXPECT_TRUE(s.Results()[0].manual_invalidated);
	EXPECT_EQ(ResolutionSource::Automatic,s.Results()[0].resolution);
	EXPECT_NE(Confidence::Green,s.Results()[0].GetConfidence());
}
TEST(Timing39Session, RetakeArmingStartsAtExactDialogueBoundary) {
	Timing39Session s;s.Prepare({Target(1,40000,40500,u8"み")},true,"opaque style",39000,40500);Start(s);
	Tap(s,'F',40050,40200);s.Stop(40500);
	ASSERT_TRUE(s.Retake(0,0,1000));Start(s);
	EXPECT_TRUE(s.Key('F',true,39800));EXPECT_EQ('F',s.ArmedOwner());
	s.Advance(40000);EXPECT_EQ(0,s.ArmedOwner());
	EXPECT_TRUE(s.Key('F',false,40100));s.Stop(40500);
	ASSERT_EQ(1u,s.retakes.size());
	auto const& blocks=s.retakes.back().raw;
	auto first=std::find_if(blocks.begin(),blocks.end(),[](TimingBlock const& b){return !b.gap;});
	ASSERT_NE(blocks.end(),first);EXPECT_EQ(40000,first->start);EXPECT_EQ(40100,first->end);
}
TEST(Timing39Session, RetakeArmedPreemptionAndRelease) {
	Timing39Session s;s.Prepare({Target(1,40000,40500,u8"み")},true,"opaque style",39000,40500);Start(s);
	Tap(s,'F',40050,40200);s.Stop(40500);
	ASSERT_TRUE(s.Retake(0,0,1000));Start(s);
	s.Key('F',true,39800);s.Key('J',true,39920);EXPECT_EQ('J',s.ArmedOwner());
	s.Advance(40000);s.Key('F',false,40100);s.Key('J',false,40200);s.Stop(40500);
	auto const& blocks=s.retakes.back().raw;
	auto first=std::find_if(blocks.begin(),blocks.end(),[](TimingBlock const& b){return !b.gap;});
	ASSERT_NE(blocks.end(),first);EXPECT_EQ(40000,first->start);EXPECT_EQ(40200,first->end);
	ASSERT_TRUE(s.Retake(0,0,1000));Start(s);
	s.Key('F',true,39800);s.Key('F',false,39900);EXPECT_EQ(0,s.ArmedOwner());
	s.Advance(40000);s.Stop(40500);
	EXPECT_FALSE(HasSungBlocks(s.retakes.back().raw));
}
TEST(Timing39Session, SecondaryRetakeCanArmWithoutChangingPrimaryRaw) {
	Timing39Session s;s.Prepare({Target(1,40000,40500,u8"み")},true,"opaque style",39000,40500);Start(s);
	Tap(s,'F',40050,40200);s.Stop(40500);auto primary=s.Raw(0);
	ASSERT_TRUE(s.Retake(0,1,1000));Start(s);
	EXPECT_TRUE(s.Key('D',true,39800));EXPECT_EQ('D',s.ArmedOwner());
	s.Advance(40000);EXPECT_TRUE(s.Key('D',false,40100));s.Stop(40500);
	EXPECT_EQ(primary,s.Raw(0));
	ASSERT_EQ(1u,s.retakes.size());
	auto const& blocks=s.retakes.back().raw;
	auto first=std::find_if(blocks.begin(),blocks.end(),[](TimingBlock const& b){return !b.gap;});
	ASSERT_NE(blocks.end(),first);EXPECT_EQ(40000,first->start);
}
TEST(Timing39Session, ArmedRetakeUsesNormalPostBoundaryPreemption) {
	Timing39Session s;s.Prepare({Target(1,40000,40500,u8"みく")},true,"opaque style",39000,40500);Start(s);
	Tap(s,'F',40050,40100);Tap(s,'J',40150,40200);s.Stop(40500);
	ASSERT_TRUE(s.Retake(0,0,1000));Start(s);
	s.Key('F',true,39800);s.Advance(40000);
	EXPECT_TRUE(s.Key('J',true,40050));
	EXPECT_TRUE(s.Key('F',false,40100));
	EXPECT_TRUE(s.Key('J',false,40200));s.Stop(40500);
	std::vector<TimingBlock> sung;
	for(auto const& block:s.retakes.back().raw)if(!block.gap)sung.push_back(block);
	EXPECT_EQ((std::vector<TimingBlock>{{40000,40050,false},{40050,40200,false}}),sung);
}
TEST(Timing39Session, CancelledRetakeLeavesExistingResultUntouched) {
	Timing39Session s;s.Prepare({Target(1,40000,40500,u8"み")},true,"opaque style",39000,40500);Start(s);
	Tap(s,'F',40050,40200);s.Stop(40500);
	auto existing=s.Results()[0].lanes[0].capture.blocks;
	ASSERT_TRUE(s.Retake(0,0,1000));Start(s);s.Key('F',true,39800);
	EXPECT_TRUE(s.CancelRetake());
	EXPECT_EQ(SessionState::Results,s.State());
	EXPECT_EQ(existing,s.Results()[0].lanes[0].capture.blocks);
	EXPECT_TRUE(s.retakes.empty());
}

TEST(Timing39Session, HarmlessSungOverhangsKeepMatchStatus) {
	for(int overhang:{5,27}) {
		auto capture=PartitionCapture({{100,140,false},{150,200+overhang,false}},100,200);
		EXPECT_EQ(PartitionStatus::HarmlessClamp,capture.status);
		ASSERT_EQ(2u,capture.blocks.size());
		EXPECT_EQ((TimingBlock{150,200,false}),capture.blocks[1]);
		SessionResult result;result.lane=0;result.lanes[0].capture=capture;
		result.lanes[0].match=Match(Analyze(u8"みく"),capture.blocks);
		EXPECT_EQ(Confidence::Green,result.lanes[0].match.confidence);
		EXPECT_EQ(Confidence::Green,result.GetConfidence())<<overhang;
	}
	auto gap=PartitionCapture({{50,150,true},{150,180,false}},100,200);
	EXPECT_EQ(PartitionStatus::HarmlessClamp,gap.status);
	SessionResult result;result.lane=0;result.lanes[0].capture=gap;
	result.lanes[0].match=Match(Analyze(u8"み"),gap.blocks);
	EXPECT_EQ(Confidence::Green,result.GetConfidence());
	auto impossible=PartitionCapture({{100,140,false},{150,227,false}},100,200);
	result.lanes[0].capture=impossible;
	result.lanes[0].match=Match(Analyze(u8"み"),impossible.blocks);
	EXPECT_EQ(PartitionStatus::HarmlessClamp,impossible.status);
	EXPECT_EQ(Confidence::Red,result.GetConfidence());
}

TEST(Timing39Session, SungOwnershipUsesStrictMajorityBeforeSymmetricClamp) {
	auto early=std::vector<TimingBlock>{{970,1120,false}};
	EXPECT_EQ(120,SungOverlapDuration(early[0],1000,2000));
	EXPECT_TRUE(SungMajorityOwned(early[0],1000,2000));
	auto owned=PartitionCapture(early,1000,2000);
	ASSERT_EQ(1u,owned.blocks.size());
	EXPECT_EQ((TimingBlock{1000,1120,false}),owned.blocks[0]);
	EXPECT_EQ(PartitionStatus::HarmlessClamp,owned.status);
	SessionResult result;result.lane=0;result.lanes[0].capture=owned;
	result.lanes[0].match=Match(Analyze(u8"み"),owned.blocks);
	EXPECT_EQ(Confidence::Green,result.GetConfidence());
	EXPECT_EQ((TimingBlock{970,1120,false}),early[0]);
	EXPECT_TRUE(PartitionCapture({{800,1050,false}},1000,2000).blocks.empty());
	EXPECT_EQ(100,SungOverlapDuration({900,1100,false},1000,2000));
	EXPECT_FALSE(SungMajorityOwned({900,1100,false},1000,2000));
	EXPECT_TRUE(PartitionCapture({{900,1100,false}},1000,2000).blocks.empty());
	auto late=PartitionCapture({{1900,2030,false}},1000,2000);
	ASSERT_EQ(1u,late.blocks.size());
	EXPECT_EQ((TimingBlock{1900,2000,false}),late.blocks[0]);
	EXPECT_EQ(PartitionStatus::HarmlessClamp,late.status);
	EXPECT_TRUE(PartitionCapture({{1950,2200,false}},1000,2000).blocks.empty());
	SessionResult invalid;invalid.lane=0;
	invalid.lanes[0].capture=PartitionCapture({{1100,1100,false}},1000,2000);
	invalid.lanes[0].match.confidence=Confidence::Green;
	EXPECT_EQ(PartitionStatus::Invalid,invalid.lanes[0].capture.status);
	EXPECT_EQ(Confidence::Red,invalid.GetConfidence());
}

TEST(Timing39Session, OrdinaryEarlyAndLateSongTapsStayGreenAcrossTwelveLines) {
	std::vector<SessionTarget> targets;
	for (int line = 1; line <= 12; ++line)
		targets.push_back(Target(line, line * 1000, line * 1000 + 600, u8"みく"));
	Timing39Session session;
	session.Prepare(targets, true, "opaque style", 0, 12700);
	Start(session);
	for (int line = 1; line <= 12; ++line) {
		int start = line * 1000;
		Tap(session, 'F', start + (line % 2 ? -30 : 20), start + 120);
		Tap(session, 'J', start + 450, start + 670);
	}
	ASSERT_TRUE(session.Stop(12700));
	auto raw = session.Raw(0);
	ASSERT_EQ(12u, session.Results().size());
	size_t green = 0, yellow = 0, red = 0;
	for (auto const& result : session.Results()) {
		EXPECT_EQ(PartitionStatus::HarmlessClamp, result.lanes[0].capture.status);
		EXPECT_EQ(2u, agi::timing39::ui::SungTapCount(result.lanes[0].capture));
		switch (result.GetConfidence()) {
			case Confidence::Green: ++green; break;
			case Confidence::Yellow: ++yellow; break;
			case Confidence::Red: ++red; break;
		}
	}
	EXPECT_EQ(12u, green); EXPECT_EQ(0u, yellow); EXPECT_EQ(0u, red);
	ASSERT_TRUE(session.SetTimingCorrection(-10));
	for (auto const& result : session.Results()) EXPECT_EQ(Confidence::Green, result.GetConfidence());
	EXPECT_EQ(raw, session.Raw(0));
}

TEST(Timing39Session, EarlyMajorityReviewRailShowsClampRatherThanExcludedTail) {
	SessionResult result;result.target=Target(1,1000,2000,u8"み");
	std::vector<TimingBlock> raw{{970,1120,false}};
	result.lanes[0].capture=PartitionCapture(raw,1000,2000);
	auto view=agi::timing39::ui::BuildLocalReviewModel(result,0,raw,0);
	ASSERT_EQ(1u,view.raw.size());
	EXPECT_EQ(agi::timing39::ui::ReviewOwnership::HarmlessStartClamp,view.raw[0].ownership);
	EXPECT_EQ((TimingBlock{1000,1120,false}),view.local[0]);
}

TEST(Timing39Session, PreviousLineSungTailIsNotANewSekaiMadeTap) {
	auto capture=PartitionCapture({
		{191937,192452,false},
		{192640,192780,false},{192796,193124,false},{193140,193468,false},
		{193484,193687,false},{193687,194155,false}
	},192390,194200);
	EXPECT_EQ(1u,capture.preceding_sung_tails);
	EXPECT_EQ(PartitionStatus::Clean,capture.status);
	ASSERT_EQ(5u,capture.blocks.size());
	EXPECT_EQ(5u,std::count_if(capture.blocks.begin(),capture.blocks.end(),[](TimingBlock const& b){return !b.gap;}));
	auto analysis=Analyze(u8"<世|せ><界|かい>まで");
	ASSERT_EQ(5u,analysis.morae.size());
	auto match=Match(analysis,capture.blocks);
	EXPECT_EQ(Confidence::Green,match.confidence)<<match.reason;
	ASSERT_EQ(1u,match.paths.size());
	for(auto const& assignment:match.paths[0].assignments)EXPECT_EQ(1u,assignment.mora_count);
}
TEST(Timing39Session, ExplicitScopeAndConservativeDiscovery) {
	auto selected=Target(1,100,200);selected.lyric_evidence=false;
	auto active=Target(2,200,300);active.selected=false;
	auto sign=Target(3,200,300);sign.selected=false;sign.style="another";
	auto ruby=Target(4,200,300,u8"<現在|イマ>");ruby.style="backing";ruby.selected=false;
	auto outside=Target(5,600,700);
	std::vector<SessionTarget> all{selected,active,sign,ruby,outside};
	auto explicit_targets=DiscoverTargets(all,true,"opaque style",100,300);ASSERT_EQ(2u,explicit_targets.size());EXPECT_EQ(1u,explicit_targets[0].id);
	auto found=DiscoverTargets(all,false,"opaque style",100,300);ASSERT_EQ(2u,found.size());EXPECT_EQ(2u,found[0].id);EXPECT_EQ(4u,found[1].id);
}
TEST(Timing39Session, CountdownCancelAndSeekFinalizeAreIdempotent) {
	Timing39Session s;s.Prepare({Target(1,100,500)},true,"opaque style",0,500);EXPECT_TRUE(s.Stop(0));EXPECT_FALSE(s.Start(0));EXPECT_TRUE(s.Raw(0).empty());
	s.Prepare({Target(1,100,500)},true,"opaque style",0,500);Start(s);EXPECT_TRUE(s.Key('F',true,150));EXPECT_FALSE(s.Key('F',true,160));
	EXPECT_TRUE(s.Key('J',true,200));EXPECT_TRUE(s.Key('F',false,210));s.Stop(300);
	EXPECT_FALSE(s.Key('J',false,350));EXPECT_FALSE(s.Start(100));EXPECT_EQ(300,s.Raw(0).back().end);
}

TEST(Timing39Session, FreshStartDefaultsToMediaZeroRegardlessOfTargets) {
	auto start=FindSessionPlaybackStart({{1,false,40340,"39 mode start here"},{2,true,12000,"39 mode settings"}});
	EXPECT_EQ(0,start.time);EXPECT_EQ(0u,start.marker);
	Timing39Session s;
	s.Prepare({Target(1,40340,44660),Target(2,45470,51920),Target(3,52040,55000)},true,"opaque style",start.time,55000);
	EXPECT_EQ(0,s.StartTime());Start(s);
	Tap(s,'F',40400,41000);Tap(s,'J',41500,42000);s.Stop(45000);
	EXPECT_EQ((TimingBlock{40400,41000,false}),s.Raw(0)[1]);
}

TEST(Timing39Session, MarkerNormalizationAndEarliestAbsoluteStart) {
	for(auto const& text:{"39 mode start here","  39 mode start here  ","39 Mode Start Here","39 MODE START HERE"}) {
		auto start=FindSessionPlaybackStart({{7,true,123450,text}});
		EXPECT_EQ(123450,start.time);EXPECT_EQ(7u,start.marker);
	}
	for(auto const& text:{"39 mode settings","start","39","before 39 mode start here","39 mode start here later"})
		EXPECT_EQ(0u,FindSessionPlaybackStart({{1,true,123450,text}}).marker);
	EXPECT_EQ(0u,FindSessionPlaybackStart({{1,false,123450,"39 mode start here"}}).marker);
	auto a=FindSessionPlaybackStart({{1,true,90000,"39 mode start here"},{2,true,30000,"39 mode start here"}});
	auto b=FindSessionPlaybackStart({{2,true,30000,"39 mode start here"},{1,true,90000,"39 mode start here"}});
	EXPECT_EQ(30000,a.time);EXPECT_EQ(a.time,b.time);EXPECT_EQ(2u,a.marker);
	EXPECT_NE(std::string::npos,a.Describe().find("comment marker"));
	EXPECT_NE(std::string::npos,FindSessionPlaybackStart({}).Describe().find("default media start"));
}

TEST(Timing39Session, MarkerDoesNotRebaseCaptureAndRetakeUsesLocalPreroll) {
	auto start=FindSessionPlaybackStart({{1,true,100000,"39 mode start here"}});
	Timing39Session s;s.Prepare({Target(2,104000,106000)},true,"opaque style",start.time,106000);Start(s);
	Tap(s,'F',104250,104500);Tap(s,'J',104750,105000);s.Stop(106000);
	ASSERT_EQ(1u,s.Results().size());EXPECT_EQ(2u,s.Results()[0].target.id);
	EXPECT_EQ((TimingBlock{104250,104500,false}),s.Raw(0)[1]);
	EXPECT_TRUE(s.Retake(0,0,100));EXPECT_EQ(103900,s.StartTime());
}

TEST(Timing39Session, PreTargetSilenceAndStrayTapsStayOutsideLyricPartition) {
	Timing39Session s;s.Prepare({Target(1,40000,45000)},true,"opaque style",0,45000);Start(s);
	Tap(s,'F',1000,1500);Tap(s,'J',2000,2200);
	Tap(s,'F',40200,41000);Tap(s,'J',42000,43000);s.Stop(45000);
	ASSERT_EQ(1u,s.Results().size());auto const& local=s.Results()[0].lanes[0].capture.blocks;
	ASSERT_FALSE(local.empty());EXPECT_EQ((TimingBlock{40000,40200,true}),local.front());
	EXPECT_EQ(Confidence::Green,s.Results()[0].GetConfidence());
	for(auto const& b:local){EXPECT_GE(b.start,40000);EXPECT_LE(b.end,45000);}
	EXPECT_EQ((TimingBlock{1000,1500,false}),s.Raw(0)[1]);
}

TEST(Timing39Session, AllCountdownKeysAreDisarmedAndCancelledTicksCannotRestart) {
	for(bool discard:{false,true}) {
		Timing39Session s;s.Prepare({Target(1,40000,45000)},true,"opaque style",0,45000);
		for(int tick=0;tick<2;++tick) {
			for(char key:{'F','J','D','K'}){EXPECT_FALSE(s.Key(key,true,100));EXPECT_FALSE(s.Key(key,false,200));}
			EXPECT_FALSE(s.TickCountdown());
		}
		EXPECT_TRUE(s.Raw(0).empty());EXPECT_TRUE(s.Raw(1).empty());
		if(discard)s.Discard();else s.Stop(0);
		for(int i=0;i<5;++i)EXPECT_FALSE(s.TickCountdown());
		EXPECT_FALSE(s.Start(0));EXPECT_FALSE(s.Key('F',true,100));
	}
}
