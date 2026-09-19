#include <main.h>
#include "timing39_karaoke.h"
#include "timing39_session_setup.h"
#include "ass_dialogue.h"
#include "ass_karaoke.h"
#include <iostream>
using namespace agi::timing39;
TEST(Timing39Karaoke, SessionSetupExcludesCommentsAndSeparatesStartFromScope) {
	AssDialogue marker,a,b,other;
	marker.Comment=true;marker.Start=123450;marker.Text=" {\\i1}39 Mode Start Here{\\i0} ";
	a.Start=140000;a.End=145000;a.Text=u8"みく";
	b.Start=150000;b.End=155000;b.Text=u8"みく";
	other.Start=160000;other.End=165000;other.Text=u8"みく";
	auto setup=BuildSessionSetup({&marker,&a,&b,&other},{&marker,&a,&b},&a,200000);
	EXPECT_EQ(123450,setup.playback_start.time);EXPECT_EQ(1u,setup.playback_start.marker);
	EXPECT_TRUE(setup.explicit_scope);ASSERT_EQ(2u,setup.targets.size());
	EXPECT_EQ(2u,setup.targets[0].id);EXPECT_EQ(3u,setup.targets[1].id);
	EXPECT_EQ(155000,setup.playback_end);
	Timing39Session session;session.Prepare(setup.targets,true,setup.active_style,setup.playback_start.time,setup.playback_end);
	session.TickCountdown();session.TickCountdown();session.TickCountdown();session.Start(123450);session.Stop(155000);
	ASSERT_EQ(2u,session.Results().size());for(auto const& r:session.Results())EXPECT_NE(1u,r.target.id);
	setup=BuildSessionSetup({&marker,&a,&b},{&marker,&a},&a,200000);
	EXPECT_FALSE(setup.explicit_scope); // the selected comment does not inflate lyric scope
	a.Start=40340;a.End=44660;b.Start=45470;b.End=51920;
	setup=BuildSessionSetup({&marker,&a,&b},{&a,&b},&a,200000);
	EXPECT_EQ(123450,setup.playback_start.time);EXPECT_EQ(200000,setup.playback_end);
	marker.Comment=false;
	setup=BuildSessionSetup({&marker,&a,&b},{&a,&b},&a,200000);
	EXPECT_EQ(0,setup.playback_start.time);EXPECT_EQ(0u,setup.playback_start.marker);
}

TEST(Timing39Karaoke, SessionSilenceDoesNotSerializeAsFortySecondsOfKaraoke) {
	AssDialogue d;d.Start=40000;d.End=45000;d.Text=u8"みく";
	auto local=PartitionCapture({{0,40200,true},{40200,41000,false},{41000,42000,true},{42000,43000,false},{43000,45000,true}},40000,45000);
	auto a=AnalyzeDialogue(d);auto result=Match(a,local.blocks);ASSERT_FALSE(result.paths.empty());
	std::string out,error;ASSERT_TRUE(Serialize(d,a,local.blocks,result.paths[0].assignments,out,error))<<error;
	EXPECT_EQ(u8"{\\k20}{\\k80}み{\\k100}{\\k100}く{\\k200}",out);
	d.Text=out;AssKaraoke reopened(&d,false,false);
	EXPECT_EQ(40200,(reopened.begin()+1)->start_time);
}
TEST(Timing39Karaoke, GoldenRubyAndGaps) {
	AssDialogue d;d.Start=1000;d.End=1600;d.Text=u8"<現在|イマ>";
	auto a=AnalyzeDialogue(d);std::vector<TimingBlock> b{{1000,1100,true},{1100,1250,false},{1250,1350,true},{1350,1500,false},{1500,1600,true}};
	auto r=Match(a,b);ASSERT_FALSE(r.paths.empty());std::string out,error;
	ASSERT_TRUE(Serialize(d,a,b,r.paths[0].assignments,out,error))<<error;
	EXPECT_EQ(u8"<現在|{\\k10}{\\k15}イ{\\k10}{\\k15}マ>{\\k10}",out);
	d.Text=out;AssKaraoke reopened(&d,false,false);ASSERT_EQ(5u,reopened.size());EXPECT_TRUE(reopened.IsEmptySyllable(0));EXPECT_EQ(1100,(reopened.begin()+1)->start_time);EXPECT_EQ(1350,(reopened.begin()+3)->start_time);EXPECT_EQ(out,reopened.GetText());
}
TEST(Timing39Karaoke, TagFamiliesAndDisplayArePreserved) {
	for(std::string tag:{"\\k","\\kf","\\ko","\\kO"}) {
		AssDialogue d;d.Start=0;d.End=300;d.Text="{\\i1}<未|{"+tag+"10}み><来|{"+tag+"20}らい>{\\i0}";
		auto a=AnalyzeDialogue(d);std::vector<TimingBlock> b{{0,100,false},{100,200,false},{200,300,false}};auto r=Match(a,b);ASSERT_FALSE(r.paths.empty());std::string out,error;
		ASSERT_TRUE(Serialize(d,a,b,r.paths[0].assignments,out,error))<<error;
		EXPECT_EQ("{\\i1}<未|{"+tag+"10}み><来|{"+tag+"10}ら{"+tag+"10}い>{\\i0}",out);
	}
}
TEST(Timing39Karaoke, AbsoluteRoundingDoesNotAccumulate) {
	// Dialogue checkpoints are already rounded by agi::Time::operator int;
	// capture endpoints retain media millisecond precision between them.
	AssDialogue d;d.Start=1000;d.End=1300;d.Text=u8"みくみ";auto a=AnalyzeDialogue(d);
	std::vector<TimingBlock> b{{1000,1104,false},{1104,1208,false},{1208,1300,false}};auto r=Match(a,b);std::string out,error;
	ASSERT_TRUE(Serialize(d,a,b,r.paths[0].assignments,out,error))<<error;EXPECT_EQ(u8"{\\k10}み{\\k11}く{\\k9}み",out);
	d.Text=out;AssKaraoke k(&d,false,false);EXPECT_EQ(1300,(k.end()-1)->start_time+(k.end()-1)->duration);
}
TEST(Timing39Karaoke, RedNeverWritesAndUnknownNeedsReading) {
	AssDialogue d;d.Start=0;d.End=100;d.Text=u8"みく";auto a=AnalyzeDialogue(d);std::string out="unchanged",error;
	EXPECT_FALSE(Serialize(d,a,{{0,100,false}},{{0,0,2}},out,error));EXPECT_EQ("unchanged",out);
	d.Text=u8"魑魅魍魎";EXPECT_FALSE(AnalyzeDialogue(d).error.empty());
	d.Text=u8"<宇宙|そら>";a=AnalyzeDialogue(d);EXPECT_EQ(u8"そら",a.reading.normalized);
}

TEST(Timing39Karaoke, RealLyricSerializationAndWorkedExamples) {
	for(auto const& example:std::vector<std::pair<std::string,size_t>>{
		{u8"<自分の価値に目を疑って|じぶんのかちにめをうたがって>",14},
		{u8"<僕は始まった栄光のゴールを見たいのさ|ぼくわはじまったえいこうのごーるをみたいのさ>",21},
		{u8"<いっせーのーで鳴り響いたスタートの合図|いっせーのーでなりひびいたすたーとのあいず>",18}}) {
		AssDialogue d;d.Start=0;d.End=int(example.second)*100;d.Text=example.first;
		auto a=AnalyzeDialogue(d);std::vector<TimingBlock> blocks;
		for(size_t i=0;i<example.second;++i)blocks.push_back({int(i)*100,int(i+1)*100,false});
		auto result=Match(a,blocks);ASSERT_FALSE(result.paths.empty())<<result.reason;
		std::string out,error;ASSERT_TRUE(Serialize(d,a,blocks,result.paths[0].assignments,out,error))<<error;
		AssDialogue output(d);output.Text=out;AssKaraoke parsed(&output,false,false);
		EXPECT_EQ(example.second,parsed.size());EXPECT_EQ(out,parsed.GetText());
		std::cout<<"39 MODE WORKED EXAMPLE (synthetic 100 ms blocks, not measured performance)\n"<<Inspect(a,blocks,result)<<"SERIALIZED: "<<out<<"\n";
	}
}

TEST(Timing39Karaoke, MixedTagsEscapesAndLeadingPunctuation) {
	AssDialogue d;d.Start=0;d.End=400;d.Text=u8"{\\i1}{\\kf20}み{\\ko20}く{\\i0}";
	auto a=AnalyzeDialogue(d);std::vector<TimingBlock> b{{0,150,false},{150,350,false}};auto r=Match(a,b);std::string out,error;
	ASSERT_TRUE(Serialize(d,a,b,r.paths[0].assignments,out,error))<<error;
	EXPECT_EQ(u8"{\\kf15}{\\i1}み{\\ko20}く{\\i0}{\\ko5}",out);
	d.Text=u8"「み」\\Nく";a=AnalyzeDialogue(d);r=Match(a,b);ASSERT_FALSE(r.paths.empty());
	ASSERT_TRUE(Serialize(d,a,b,r.paths[0].assignments,out,error))<<error;EXPECT_NE(std::string::npos,out.find(u8"「み」\\N"));
}
