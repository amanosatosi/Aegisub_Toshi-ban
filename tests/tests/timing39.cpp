#include <gtest/gtest.h>
#include <libaegisub/timing39.h>
#include "timing39_ui.h"
#include "audio_display_cache.h"
#include <algorithm>
using namespace agi::timing39;
namespace {
std::vector<TimingBlock> Taps(size_t count,int duration=100) {
	std::vector<TimingBlock> blocks;for(size_t i=0;i<count;++i)blocks.push_back({int(i)*duration,int(i+1)*duration,false});return blocks;
}
std::vector<std::string> Morae(Analysis const& a) {std::vector<std::string> out;for(auto const& m:a.morae)out.push_back(m.text);return out;}
bool Has(Analysis const& a,std::string const& text) {
	for(auto const& edges:a.graph)for(auto const& e:edges) {
		if(e.count==1)continue;std::string s;for(size_t j=0;j<e.count;++j)s+=a.morae[e.first+j].text;if(s==text)return true;
	}return false;
}
}
TEST(Timing39, DeterministicBaseMorae) {
	std::vector<std::pair<std::string,std::vector<std::string>>> cases={
		{u8"みく",{u8"み",u8"く"}}, {u8"きょう",{u8"きょ",u8"う"}},
		{u8"しょうじょ",{u8"しょ",u8"う",u8"じょ"}}, {u8"まって",{u8"ま",u8"っ",u8"て"}},
		{u8"しんじる",{u8"し",u8"ん",u8"じ",u8"る"}}, {u8"メロディー",{u8"め",u8"ろ",u8"でぃ",u8"ー"}},
		{u8"こう せい さい あい",{u8"こ",u8"う",u8"せ",u8"い",u8"さ",u8"い",u8"あ",u8"い"}},
		{u8"ファフィフェフォティディトゥドゥシェジェチェウィウェウォヴァクォ",{u8"ふぁ",u8"ふぃ",u8"ふぇ",u8"ふぉ",u8"てぃ",u8"でぃ",u8"とぅ",u8"どぅ",u8"しぇ",u8"じぇ",u8"ちぇ",u8"うぃ",u8"うぇ",u8"うぉ",u8"ゔぁ",u8"くぉ"}}};
	for(auto const& c:cases) {auto a=Analyze(c.first);EXPECT_TRUE(a.error.empty())<<a.error;EXPECT_EQ(c.second,Morae(a));}
	EXPECT_EQ(Morae(Analyze(u8"がぱ")),Morae(Analyze(u8"か\u3099は\u309a")));
}

TEST(Timing39, RealRubySentenceMustNotCollapseToOneUnknownWord) {
	auto a=Analyze(u8"{\\fad(200,200)}<髪|かみ>の<黒|くろ>に<高|たか><鳴|な>る<胸|むね>を<知|し>る<頃|ころ>");
	ASSERT_TRUE(a.error.empty())<<a.error;
	EXPECT_EQ(u8"かみのくろにたかなるむねをしるころ",a.reading.normalized);
	EXPECT_GE(a.words.size(),9u);
	for(auto const& w:a.words)if(w.kind==WordKind::Unknown)EXPECT_LT(w.end-w.begin,10u);
	for(auto const& particle:{u8"の",u8"に",u8"を"})EXPECT_TRUE(std::any_of(a.words.begin(),a.words.end(),[&](SpokenToken const& w){return w.reading==particle&&w.kind==WordKind::Particle;}));
}
TEST(Timing39, StrictRomajiAllOrNothing) {
	for(auto const& c:std::vector<std::pair<std::string,std::string>>{{"doa",u8"どあ"},{"miku",u8"みく"},{"kyou",u8"きょう"},{"matte",u8"まって"},{"shinjite",u8"しんじて"},{"n",u8"ん"},{"nn",u8"ん"},{"n'ya",u8"んや"},{"konnichiha",u8"こんにちは"},{"thi dhi twu dwu",u8"てぃ でぃ とぅ どぅ"},{"go-ru",u8"ごーる"}}) {
		auto r=ConvertRomaji(c.first);EXPECT_TRUE(r.valid)<<c.first;EXPECT_EQ(c.second,r.kana);
	}
	for(auto const& s:{"door","dream","heart","night","r","hello","miku!"}) {auto r=ConvertRomaji(s);EXPECT_FALSE(r.valid)<<s;EXPECT_TRUE(r.kana.empty());}
	EXPECT_EQ(Morae(Analyze("kyou")),Morae(Analyze(u8"きょう")));
	EXPECT_EQ(u8"みく",Analyze(u8"<ミク|miku>").reading.normalized);
}
TEST(Timing39, ProductiveRubyOkuriganaKeepsUncertaintyLocal) {
	for(auto const& text:{u8"<掴|つか>む",u8"<踊|おど>らなかった",u8"<食|た>べていた",u8"<泳|およ>いだ",u8"<眩|まぶ>しくない"}) {
		auto a=Analyze(text);ASSERT_TRUE(a.error.empty())<<a.error;
		ASSERT_EQ(1u,a.words.size())<<text;
		EXPECT_EQ(a.reading.normalized,a.words[0].reading);
		EXPECT_FALSE(a.language_certain);EXPECT_FALSE(a.language_notes.empty());
	}
	auto a=Analyze(u8"<凪|なぎ>の<彼方|かなた>");ASSERT_EQ(3u,a.words.size());
	EXPECT_EQ(WordKind::Particle,a.words[1].kind);EXPECT_FALSE(a.language_certain);
	a=Analyze(u8"<高|たか><鳴|な>る");ASSERT_EQ(1u,a.words.size());EXPECT_EQ(u8"たかなる",a.words[0].reading);
}
TEST(Timing39, ExplicitReadingAuthorityAndIndependentLexemes) {
	for(auto const& c:std::vector<std::pair<std::string,std::string>>{{u8"<現在|イマ>",u8"いま"},{u8"<未来|あした>",u8"あした"},{u8"<宇宙|そら>",u8"そら"},{u8"<地球|ほし>",u8"ほし"},{u8"<0|ゼロ>",u8"ぜろ"}}) {
		auto a=Analyze(c.first);EXPECT_TRUE(a.error.empty());EXPECT_EQ(c.second,a.reading.normalized);EXPECT_EQ(c.first,a.surface);ASSERT_EQ(1u,a.spans.size());EXPECT_TRUE(a.spans[0].explicit_reading);
	}
	auto a=Analyze(u8"<未|み><来|らい>");ASSERT_EQ(1u,a.words.size());EXPECT_EQ(u8"みらい",a.words[0].reading);EXPECT_EQ(a.morae[0].lexeme,a.morae[2].lexeme);EXPECT_NE(a.morae[0].span,a.morae[2].span);
	a=Analyze(u8"<異世界|いまといいう>");EXPECT_EQ(u8"いまといいう",a.reading.normalized);
	EXPECT_FALSE(Analyze(u8"<病|>").error.empty());EXPECT_FALSE(Analyze("\xff").error.empty());
}
TEST(Timing39, GrammarDeletesEdgesBeforeMatching) {
	auto a=Analyze(u8"<彷徨う|さまよう>");EXPECT_EQ((std::vector<std::string>{u8"さ",u8"ま",u8"よ",u8"う"}),Morae(a));EXPECT_FALSE(Has(a,u8"よう"));EXPECT_EQ(Confidence::Red,Match(a,Taps(3)).confidence);
	a=Analyze(u8"<幻想|げんそう>を<映し出す|うつしだす>けど");EXPECT_FALSE(Has(a,u8"をう"));EXPECT_TRUE(Has(a,u8"そう"));
	a=Analyze(u8"<今|いま>という<瞬間|しゅんかん>がいつだって<明日|あす>を<作る|つくる>から");EXPECT_FALSE(Has(a,u8"とい"));EXPECT_FALSE(Has(a,u8"がい"));
	EXPECT_FALSE(Has(Analyze(u8"こ、う"),u8"こう"));EXPECT_FALSE(Has(Analyze(u8"ぼくわじまたえ"),u8"ぼく"));EXPECT_FALSE(Has(Analyze(u8"ぼくわじまたえ"),u8"たえ"));
}
TEST(Timing39, NegativeNaiIsASoftOptionalTimingGroup) {
	for(auto const& text:{u8"しない",u8"できない",u8"わからない",u8"いらない"}) {
		auto a=Analyze(text);ASSERT_TRUE(a.error.empty())<<text;ASSERT_TRUE(Has(a,u8"ない"))<<text;
		bool soft=false;
		for(auto const& edges:a.graph)for(auto const& e:edges)if(e.count==2&&a.morae[e.first].text==u8"な"&&a.morae[e.first+1].text==u8"い")soft=e.features.strength==CandidateStrength::Soft;
		EXPECT_TRUE(soft)<<text;
	}
	EXPECT_FALSE(Has(Analyze(u8"かないます"),u8"ない"));
	auto a=Analyze(u8"<口|くち>に<出|だ>せやしない　「<忘|わす>れてしまうの？」");
	ASSERT_EQ(17u,a.morae.size());ASSERT_TRUE(Has(a,u8"ない"));
	auto match=Match(a,Taps(16));ASSERT_FALSE(match.paths.empty())<<match.reason;
	bool represents_negative=false;
	for(auto const& path:match.paths)for(auto const& x:path.assignments)
		if(x.mora_count==2&&a.morae[x.first_mora].text==u8"な"&&a.morae[x.first_mora+1].text==u8"い")represents_negative=true;
	EXPECT_TRUE(represents_negative);
}
TEST(Timing39, IndependentCandidatesAndGrammarProtectionCoexist) {
	auto a=Analyze(u8"せんさい");
	EXPECT_TRUE(Has(a,u8"せん"));EXPECT_TRUE(Has(a,u8"さい"));
	auto match=Match(a,Taps(2));ASSERT_FALSE(match.paths.empty());
	EXPECT_EQ(2u,match.paths[0].assignments.size());
	EXPECT_EQ(2u,match.paths[0].assignments[0].mora_count);EXPECT_EQ(2u,match.paths[0].assignments[1].mora_count);
	a=Analyze(u8"ただ<可|か><憐|れん>でいられるように");
	EXPECT_FALSE(Has(a,u8"でい"));
}
TEST(Timing39, JapanesePunctuationNeverBecomesMorae) {
	auto a=Analyze(u8"「<忘|わす>れてしまうの？」");
	EXPECT_EQ((std::vector<std::string>{u8"わ",u8"す",u8"れ",u8"て",u8"し",u8"ま",u8"う",u8"の"}),Morae(a));
}
TEST(Timing39, ExactCountAndNamedGroups) {
	auto a=Analyze(u8"ゴール");auto all=Match(a,Taps(3));ASSERT_EQ(Confidence::Green,all.confidence);for(auto const& x:all.paths[0].assignments)EXPECT_EQ(1u,x.mora_count);
	auto merge=Match(a,Taps(2));ASSERT_FALSE(merge.paths.empty());EXPECT_EQ(2u,merge.paths[0].assignments[0].mora_count);
	EXPECT_TRUE(Has(Analyze(u8"まって"),u8"まっ"));EXPECT_TRUE(Has(Analyze(u8"しんじる"),u8"しん"));EXPECT_TRUE(Has(Analyze(u8"合図"),u8"あい"));
	EXPECT_EQ(Confidence::Red,Match(Analyze(u8"みく"),Taps(1)).confidence);
	EXPECT_EQ(Confidence::Red,Match(a,{}).confidence);EXPECT_EQ(Confidence::Red,Match(a,{{100,90,false}}).confidence);
}
TEST(Timing39, RealLyricsAndConfidence) {
	auto a=Analyze(u8"自分の価値に目を疑って");ASSERT_TRUE(a.error.empty())<<a.error;ASSERT_EQ(14u,a.morae.size());auto r=Match(a,Taps(14));ASSERT_FALSE(r.paths.empty());for(auto const& x:r.paths[0].assignments)EXPECT_EQ(1u,x.mora_count);
	a=Analyze(u8"<僕は始まった栄光のゴールを見たいのさ|ぼくわはじまったえいこうのごーるをみたいのさ>");ASSERT_EQ(22u,a.morae.size());r=Match(a,Taps(21));ASSERT_FALSE(r.paths.empty());EXPECT_EQ(21u,r.paths[0].assignments.size());
	bool ground_truth=false;for(auto const& path:r.paths)for(auto const& x:path.assignments)if(x.first_mora==13&&x.mora_count==2)ground_truth=true;EXPECT_TRUE(ground_truth);EXPECT_EQ(2u,r.paths.front().assignments[13].mora_count);
	a=Analyze(u8"<いっせーのーで鳴り響いたスタートの合図|いっせーのーでなりひびいたすたーとのあいず>");ASSERT_EQ(21u,a.morae.size());r=Match(a,Taps(18));ASSERT_GT(r.paths.size(),1u);EXPECT_EQ(18u,r.paths[0].assignments.size());EXPECT_EQ(Confidence::Yellow,r.confidence);EXPECT_FALSE(r.ambiguous_mora_boundaries.empty());EXPECT_FALSE(agi::timing39::ui::CompactAmbiguity(a,r).empty());EXPECT_FALSE(Has(a,u8"のあ"));
	auto more=Match(a,Taps(21));EXPECT_EQ(Confidence::Green,more.confidence);EXPECT_EQ(21u,a.morae.size());
}
TEST(Timing39, ResultsStatusStylesAreDistinct) {
	using agi::timing39::ui::StatusStyle;
	auto green=StatusStyle(Confidence::Green),yellow=StatusStyle(Confidence::Yellow),red=StatusStyle(Confidence::Red);
	EXPECT_NE(green.accent.red,yellow.accent.red);EXPECT_NE(yellow.accent.red,red.accent.red);
	EXPECT_NE(green.role,yellow.role);EXPECT_NE(yellow.role,red.role);
}
TEST(Timing39, ResultsLyricDisplayPreservesAuthoritativeRubyAndRomaji) {
	using agi::timing39::ui::PrepareLyricDisplay;
	for (auto const& source : {u8"<未|み><来|らい>",u8"<口|くち>に<出|だ>せやしない",u8"<彷|さまよ>って"}) {
		auto prepared=PrepareLyricDisplay(Analyze(source));
		EXPECT_EQ(std::string::npos,prepared.plain.find('<'));
		EXPECT_EQ(std::string::npos,prepared.plain.find('|'));
		EXPECT_EQ(std::string::npos,prepared.plain.find('>'));
		EXPECT_FALSE(prepared.segments.empty());
	}
	auto future=PrepareLyricDisplay(Analyze(u8"<未|み><来|らい>"));
	EXPECT_EQ(u8"未来",future.plain);
	ASSERT_EQ(2u,future.segments.size());
	EXPECT_EQ(u8"み",future.segments[0].ruby);EXPECT_EQ(u8"らい",future.segments[1].ruby);
	auto mouth=PrepareLyricDisplay(Analyze(u8"<口|くち>に<出|だ>せやしない"));
	EXPECT_EQ(u8"口に出せやしない",mouth.plain);
	EXPECT_EQ(u8"くち",mouth.segments[0].ruby);
	auto wander=PrepareLyricDisplay(Analyze(u8"<彷|さまよ>って"));
	EXPECT_EQ(u8"彷って",wander.plain);EXPECT_EQ(u8"さまよ",wander.segments[0].ruby);
	auto romaji=PrepareLyricDisplay(Analyze("Yume ni naru made"));
	EXPECT_EQ("Yume ni naru made",romaji.plain);
	for(auto const& segment:romaji.segments)EXPECT_TRUE(segment.ruby.empty());
}
TEST(Timing39, ResultsCandidateButtonsDescribeOnlyDisputedCuts) {
 auto analysis=Analyze(u8"<いっせーのーで鳴り響いたスタートの合図|いっせーのーでなりひびいたすたーとのあいず>");
 auto match=Match(analysis,Taps(18));
	ASSERT_GE(match.paths.size(),2u);
	for(size_t i=0;i<2;++i) {
		auto label=agi::timing39::ui::CompactPathChoice(analysis,match,i);
		EXPECT_FALSE(label.empty());
		EXPECT_EQ(std::string::npos,label.find('['));
	}
}
TEST(Timing39, TimelineIntervalIndexVisitsOnlyVisibleOverlaps) {
	agi::timing39::ui::TimelineIntervalIndex index;
	index.Reset({{900,5000},{100,200},{300,400},{5100,5200}});EXPECT_EQ(4u,index.Size());
	std::vector<std::pair<int,int>> visible;
	index.Visit(350,950,[&](int start,int end){visible.emplace_back(start,end);});
	EXPECT_EQ((std::vector<std::pair<int,int>>{{300,400},{900,5000}}),visible);
	visible.clear();index.Visit(4500,4600,[&](int start,int end){visible.emplace_back(start,end);});
	EXPECT_EQ((std::vector<std::pair<int,int>>{{900,5000}}),visible);
	index.Reset({{6000,6100}});visible.clear();
	index.Visit(350,950,[&](int start,int end){visible.emplace_back(start,end);});
	EXPECT_TRUE(visible.empty());
	index.Visit(6000,6050,[&](int start,int end){visible.emplace_back(start,end);});
	EXPECT_EQ((std::vector<std::pair<int,int>>{{6000,6100}}),visible);
}
TEST(Timing39, AudioStyleRangesKeepOnlyTransitionsAndFindVisibleStyle) {
	using namespace audio_display_cache;
	std::vector<StyleRange> ranges;
	for (auto range : std::vector<StyleRange>{{0,0},{100,0},{200,1},{300,1},{400,2}})
		AppendStyleTransition(ranges, range);
	EXPECT_EQ((std::vector<StyleRange>{{0,0},{200,1},{400,2}}), ranges);
	EXPECT_EQ(ranges.begin(), FirstStyleAt(ranges, -1));
	EXPECT_EQ(ranges.begin(), FirstStyleAt(ranges, 199));
	EXPECT_EQ(ranges.begin()+1, FirstStyleAt(ranges, 200));
	EXPECT_EQ(ranges.begin()+2, FirstStyleAt(ranges, 999));
}
TEST(Timing39, AudioParticleBudgetNeverGrowsPastLimit) {
	using namespace audio_display_cache;
	size_t active = 0;
	for (int hit=0; hit<1000; ++hit) {
		auto retired = ParticlesToRetire(active);
		EXPECT_LE(retired, active);
		active = active - retired + particles_per_hit;
		EXPECT_LE(active, particle_limit);
	}
}
TEST(Timing39, VisibleCaptureVisitorMatchesPreviewWithoutFullSessionCopy) {
	TimingLane lane('F','J');
	lane.Begin(0,10000);
	for (int i=0;i<100;++i) {
		lane.KeyDown('F',i*100+10);
		lane.KeyUp('F',i*100+60);
	}
	std::vector<TimingBlock> visible;
	lane.VisitPreview(5050,4900,5100,[&](TimingBlock const& block){visible.push_back(block);});
	EXPECT_EQ(lane.Preview(5050,4900,5100), visible);
	EXPECT_LT(visible.size(), lane.Blocks().size());
}
TEST(Timing39, DurationRanksOnlyLegalPaths) {
	auto a=Analyze(u8"こーこー");auto blocks=Taps(3);blocks[0].end=200;blocks[1]={200,300,false};blocks[2]={300,400,false};
	auto r=Match(a,blocks);ASSERT_FALSE(r.paths.empty());EXPECT_EQ(2u,r.paths[0].assignments[0].mora_count);
	EXPECT_EQ(Confidence::Red,Match(Analyze(u8"みく"),{{0,5000,false}}).confidence);
}
TEST(Timing39, OwnershipGapsAndIndependentLanes) {
	TimingCaptureSession session;for(auto& lane:session.lanes)lane.Begin(0,1000);
	auto& p=session.lanes[0];auto& s=session.lanes[1];
	EXPECT_TRUE(p.KeyDown('F',100));EXPECT_FALSE(p.KeyDown('F',110));EXPECT_TRUE(s.KeyDown('D',125));
	EXPECT_TRUE(p.KeyDown('J',200));EXPECT_TRUE(p.KeyUp('F',250));EXPECT_TRUE(p.Active());EXPECT_TRUE(p.KeyUp('J',300));EXPECT_FALSE(p.Active());
	EXPECT_TRUE(s.Active());s.KeyUp('D',350);p.KeyDown('F',400);p.KeyUp('F',500);session.Finish(600);
	EXPECT_EQ((std::vector<TimingBlock>{{0,100,true},{100,200,false},{200,300,false},{300,400,true},{400,500,false},{500,600,true}}),p.Blocks());
	EXPECT_EQ((std::vector<TimingBlock>{{0,125,true},{125,350,false},{350,600,true}}),s.Blocks());
	p.Begin(0,1000);EXPECT_TRUE(p.KeyDown('F',100));p.Finish(150);EXPECT_FALSE(p.Active());EXPECT_FALSE(p.KeyUp('F',200));EXPECT_FALSE(p.KeyDown('J',200));
	p.Begin(0,1000);EXPECT_TRUE(p.KeyDown('F',200));EXPECT_FALSE(p.KeyDown('J',100));EXPECT_FALSE(p.Active());
}
TEST(Timing39, LaneRetakeIsLocalAndCheckpointsRejectOverflow) {
	TimingCaptureSession s;for(auto& lane:s.lanes)lane.Begin(100,500);
	EXPECT_FALSE(s.lanes[0].KeyDown('F',50));EXPECT_FALSE(s.lanes[0].KeyDown('F',500));
	s.lanes[1].KeyDown('K',100);s.lanes[1].KeyUp('K',200);auto saved=s.lanes[1].Blocks();s.lanes[0].Begin(100,500);EXPECT_EQ(saved,s.lanes[1].Blocks());
	s.lanes[0].KeyDown('J',400);s.lanes[0].Finish(600);EXPECT_EQ(500,s.lanes[0].Blocks().back().end);
}
TEST(Timing39, CorrectionPreservesCaptureAndRejectsIllegalBoundaries) {
	auto a=Analyze(u8"こーー");auto blocks=Taps(2);auto saved=blocks;auto r=Match(a,blocks);ASSERT_EQ(2u,r.paths.size());
	AssignmentEditor editor;editor.Reset(r.paths[0].assignments);EXPECT_TRUE(editor.Choose(a,r.paths[1].assignments));EXPECT_TRUE(editor.Undo());EXPECT_TRUE(editor.Redo());
	int delta=editor.Get()[0].mora_count==1?1:-1;EXPECT_TRUE(editor.Move(a,1,delta));EXPECT_EQ(saved,blocks);EXPECT_FALSE(editor.Move(a,0,1));
	a=Analyze(u8"まって");r=Match(a,Taps(2));editor.Reset(r.paths[0].assignments);EXPECT_FALSE(editor.Move(a,1,-1));
	EXPECT_NE(std::string::npos,Inspect(a,Taps(2),r,&editor).find("FIXED"));
}
TEST(Timing39, StyleInferenceUsesEvidenceAndRequiresCorroboration) {
	std::vector<StyleEvidence> evidence;
	for(int offset:{0,2000}) {
		std::vector<int> p{offset+100,offset+200,offset+300},s{offset+600,offset+800,offset+900};
		evidence.push_back({"Arbitrary B",p,p,s});evidence.push_back({"Arbitrary A",s,p,s});
	}
	auto r=InferStyles(evidence);EXPECT_FALSE(r.ambiguous);EXPECT_EQ("Arbitrary B",r.primary);EXPECT_EQ("Arbitrary A",r.secondary);
	for(auto& e:evidence)e.secondary=e.primary;EXPECT_TRUE(InferStyles(evidence).ambiguous);
	EXPECT_TRUE(InferStyles({}).ambiguous);
}
