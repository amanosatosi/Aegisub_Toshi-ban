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
TEST(Timing39Session, CheckpointCrossingPreservesRawAndRequiresReview) {
	Timing39Session s;s.Prepare({Target(1,100,200,u8"み"),Target(2,200,300,u8"く")},true,"opaque style",0,300);Start(s);
	Tap(s,'F',150,250);s.Stop(300);ASSERT_EQ(2u,s.Results().size());
	for(auto const& r:s.Results()){EXPECT_TRUE(r.lanes[0].capture.clipped);EXPECT_EQ(Confidence::Yellow,r.GetConfidence());}
	EXPECT_EQ((TimingBlock{150,250,false}),s.Raw(0)[1]);
	EXPECT_EQ((TimingBlock{150,200,false}),s.Results()[0].lanes[0].capture.blocks[1]);
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
