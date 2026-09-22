#include <gtest/gtest.h>
#include <algorithm>
#include <libaegisub/timing39_session.h>
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
TEST(Timing39Session, MeaningfulSungCheckpointCrossingRequiresReviewOnlyForOwner) {
	Timing39Session s;s.Prepare({Target(1,100,200,u8"み"),Target(2,200,320,u8"く")},true,"opaque style",0,320);Start(s);
	Tap(s,'F',150,280);Tap(s,'J',290,310);s.Stop(320);ASSERT_EQ(2u,s.Results().size());
	EXPECT_EQ(PartitionStatus::AmbiguousSungCrossing,s.Results()[0].lanes[0].capture.status);
	EXPECT_EQ(Confidence::Yellow,s.Results()[0].GetConfidence());
	EXPECT_EQ(1u,s.Results()[1].lanes[0].capture.preceding_sung_tails);
	auto const& second=s.Results()[1].lanes[0].capture.blocks;
	EXPECT_EQ(1u,std::count_if(second.begin(),second.end(),[](TimingBlock const& b){return !b.gap;}));
	EXPECT_NE(second.end(),std::find(second.begin(),second.end(),TimingBlock{290,310,false}));
	EXPECT_EQ(Confidence::Green,s.Results()[1].GetConfidence());
	EXPECT_EQ((TimingBlock{150,280,false}),s.Raw(0)[1]);
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
	EXPECT_EQ(Confidence::Red,s.Results()[0].GetConfidence());
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
	for(int overhang:{5,27,50}) {
		auto capture=PartitionCapture({{100,180,false},{180,200+overhang,false}},100,200);
		EXPECT_EQ(PartitionStatus::HarmlessClamp,capture.status);
		ASSERT_EQ(2u,capture.blocks.size());
		EXPECT_EQ((TimingBlock{180,200,false}),capture.blocks[1]);
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
