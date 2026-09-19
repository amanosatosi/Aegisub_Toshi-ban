// Copyright (c) 2026, JibunSenyou contributors. ISC license.
#include <libaegisub/timing39.h>

#include <boost/locale/encoding_utf.hpp>
#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <stdexcept>

namespace agi { namespace timing39 {
namespace {
std::u32string Decode(std::string const& s) { return boost::locale::conv::utf_to_utf<char32_t>(s, boost::locale::conv::stop); }
std::string Encode(std::u32string const& s) { return boost::locale::conv::utf_to_utf<char>(s); }
char32_t Hira(char32_t c) { return c >= U'ァ' && c <= U'ヶ' ? c - 0x60 : c; }
bool Separator(char32_t c) {
	return c == U' ' || c == U'\t' || c == U'\n' || c == U'\r' || c == 0x3000 ||
		(c >= 0x3001 && c <= 0x303f) || c == U'！' || c == U'？' || c == U'…' ||
		(c < 128 && std::ispunct(static_cast<unsigned char>(c)) && c != '-' && c != '\'');
}
bool Kana(char32_t c) { return (c >= U'ぁ' && c <= U'ゖ') || c == U'ー'; }

struct Lexeme { std::string source, reading; WordKind kind; bool separate_ending = false; };
// A deliberately small, auditable vocabulary. Unknown material is never
// assigned an invented part of speech. Extend with regression cases, not costs.
std::vector<Lexeme> const& Lexicon() {
	static std::vector<Lexeme> const words = {
		{u8"幻想",u8"げんそう",WordKind::Noun}, {u8"映し出す",u8"うつしだす",WordKind::Verb},
		{u8"今",u8"いま",WordKind::Noun}, {u8"瞬間",u8"しゅんかん",WordKind::Noun},
		{u8"いつだって",u8"いつだって",WordKind::Expression}, {u8"明日",u8"あす",WordKind::Noun},
		{u8"明日",u8"あした",WordKind::Noun}, {u8"作る",u8"つくる",WordKind::Verb},
		{u8"彷徨う",u8"さまよう",WordKind::Verb,true}, {u8"迷う",u8"まよう",WordKind::Verb,true},
		{u8"思う",u8"おもう",WordKind::Verb,true}, {u8"歌う",u8"うたう",WordKind::Verb,true},
		{u8"願う",u8"ねがう",WordKind::Verb,true}, {u8"笑う",u8"わらう",WordKind::Verb,true},
		{u8"違う",u8"ちがう",WordKind::Verb,true}, {u8"向かう",u8"むかう",WordKind::Verb,true},
		{u8"出会う",u8"であう",WordKind::Verb,true}, {u8"いう",u8"いう",WordKind::Verb,true},
		{u8"しまう",u8"しまう",WordKind::Auxiliary,true},
		{u8"自分",u8"じぶん",WordKind::Noun}, {u8"価値",u8"かち",WordKind::Noun},
		{u8"目",u8"め",WordKind::Noun}, {u8"疑って",u8"うたがって",WordKind::Verb},
		{u8"僕",u8"ぼく",WordKind::Noun}, {u8"始まった",u8"はじまった",WordKind::Verb},
		{u8"栄光",u8"えいこう",WordKind::Noun}, {u8"ゴール",u8"ごーる",WordKind::Noun},
		{u8"見たい",u8"みたい",WordKind::Verb}, {u8"いっせーのー",u8"いっせーのー",WordKind::Expression},
		{u8"鳴り響いた",u8"なりひびいた",WordKind::Verb}, {u8"スタート",u8"すたーと",WordKind::Noun},
		{u8"合図",u8"あいず",WordKind::Noun}, {u8"未来",u8"みらい",WordKind::Noun},
		{u8"宇宙",u8"うちゅう",WordKind::Noun}, {u8"空",u8"そら",WordKind::Noun},
		{u8"地球",u8"ちきゅう",WordKind::Noun}, {u8"星",u8"ほし",WordKind::Noun},
		{u8"現在",u8"げんざい",WordKind::Noun}, {u8"ゼロ",u8"ぜろ",WordKind::Noun},
		{u8"待って",u8"まって",WordKind::Verb}, {u8"待った",u8"まった",WordKind::Verb},
		{u8"信じて",u8"しんじて",WordKind::Verb}, {u8"信じる",u8"しんじる",WordKind::Verb},
		{u8"少女",u8"しょうじょ",WordKind::Noun}, {u8"今日",u8"きょう",WordKind::Noun},
		{u8"メロディー",u8"めろでぃー",WordKind::Noun}, {u8"君",u8"きみ",WordKind::Noun},
		{u8"病",u8"やまい",WordKind::Noun}, {u8"光",u8"ひかり",WordKind::Noun},
		{u8"愛",u8"あい",WordKind::Noun}, {u8"声",u8"こえ",WordKind::Noun},
		{u8"心",u8"こころ",WordKind::Noun}, {u8"夢",u8"ゆめ",WordKind::Noun},
		{u8"ドア",u8"どあ",WordKind::Noun}, {u8"みく",u8"みく",WordKind::Noun}
	};
	return words;
}

std::map<std::string, std::string> const& RomanTable() {
	static auto const table = [] {
		std::map<std::string, std::string> t;
		auto row = [&](std::string head, std::u32string kana) {
			std::string vowels = "aiueo";
			for (size_t i = 0; i < kana.size(); ++i) t[head + vowels[i]] = Encode({kana[i]});
		};
		row("",U"あいうえお"); row("k",U"かきくけこ"); row("g",U"がぎぐげご");
		row("s",U"さしすせそ"); row("z",U"ざじずぜぞ"); row("t",U"たちつてと");
		row("d",U"だぢづでど"); row("n",U"なにぬねの"); row("h",U"はひふへほ");
		row("b",U"ばびぶべぼ"); row("p",U"ぱぴぷぺぽ"); row("m",U"まみむめも");
		row("r",U"らりるれろ"); row("x",U"ぁぃぅぇぉ"); row("l",U"ぁぃぅぇぉ");
		for (auto pair : std::vector<std::pair<std::string,std::string>>{
			{"shi",u8"し"},{"chi",u8"ち"},{"tsu",u8"つ"},{"fu",u8"ふ"},{"ji",u8"じ"},
			{"ya",u8"や"},{"yu",u8"ゆ"},{"yo",u8"よ"},{"wa",u8"わ"},{"wo",u8"を"},
			{"wi",u8"うぃ"},{"we",u8"うぇ"},{"xtsu",u8"っ"},{"ltsu",u8"っ"},
			{"xtu",u8"っ"},{"ltu",u8"っ"},{"ti",u8"ち"},{"di",u8"ぢ"},
			{"thi",u8"てぃ"},{"dhi",u8"でぃ"},{"twu",u8"とぅ"},{"dwu",u8"どぅ"},
			{"she",u8"しぇ"},{"je",u8"じぇ"},{"che",u8"ちぇ"},{"who",u8"うぉ"}}) t.insert(pair);
		for (auto pair : std::vector<std::pair<std::string,std::string>>{
			{"ky",u8"き"},{"gy",u8"ぎ"},{"sh",u8"し"},{"sy",u8"し"},{"zy",u8"じ"},
			{"j",u8"じ"},{"jy",u8"じ"},{"ch",u8"ち"},{"ty",u8"ち"},{"cy",u8"ち"},
			{"dy",u8"ぢ"},{"ny",u8"に"},{"hy",u8"ひ"},{"by",u8"び"},{"py",u8"ぴ"},
			{"my",u8"み"},{"ry",u8"り"},{"xy",""},{"ly",""}}) {
			t[pair.first+"a"]=pair.second+u8"ゃ"; t[pair.first+"u"]=pair.second+u8"ゅ"; t[pair.first+"o"]=pair.second+u8"ょ";
		}
		for (auto pair : std::vector<std::pair<std::string,std::string>>{{"f",u8"ふ"},{"v",u8"ゔ"},{"kw",u8"く"},{"qw",u8"く"},{"gw",u8"ぐ"}}) {
			auto vowels = U"ぁぃぅぇぉ";
			for (size_t i = 0; i < 5; ++i) t[pair.first+"aiueo"[i]]=pair.second+Encode({vowels[i]});
		}
		t["fu"]=u8"ふ"; t["vu"]=u8"ゔ";
		return t;
	}();
	return table;
}

bool Cluster(char32_t first, char32_t second) {
	static std::set<std::u32string> const clusters = [] {
		std::set<std::u32string> result;
		for (auto c : std::u32string(U"きぎしじちぢにひびぴみり"))
			for (auto small : std::u32string(U"ゃゅょ")) result.insert({c,small});
		for (auto const& s : {U"ふぁ",U"ふぃ",U"ふぇ",U"ふぉ",U"ふゅ",U"てぃ",U"でぃ",U"とぅ",U"どぅ",
			U"てゅ",U"でゅ",U"しぇ",U"じぇ",U"ちぇ",U"うぃ",U"うぇ",U"うぉ",U"ゔぁ",U"ゔぃ",U"ゔぇ",U"ゔぉ",U"ゔゅ",
			U"くぁ",U"くぃ",U"くぇ",U"くぉ",U"ぐぁ",U"ぐぃ",U"ぐぇ",U"ぐぉ",U"つぁ",U"つぃ",U"つぇ",U"つぉ"}) result.insert(s);
		return result;
	}();
	return clusters.count({first,second}) != 0;
}
char Vowel(std::string const& mora) {
	auto chars = Decode(mora);
	if (chars.empty()) return 0;
	auto c = chars.back();
	for (auto const& row : std::vector<std::pair<char,std::u32string>>{
		{'a',U"あぁかがさざただなはばぱまやゃらわゎ"}, {'i',U"いぃきぎしじちぢにひびぴみりゐ"},
		{'u',U"うぅくぐすずつづぬふぶぷむゆゅるゔ"}, {'e',U"えぇけげせぜてでねへべぺめれゑ"},
		{'o',U"おぉこごそぞとどのほぼぽもよょろを"}}) if (row.second.find(c)!=std::u32string::npos) return row.first;
	return 0;
}

struct Reader {
	Analysis a;
	void Span(std::string display, std::string reading, size_t begin, size_t end, bool explicit_reading) {
		SourceSpan span{begin,end,display,reading,a.reading.characters.size(),0,explicit_reading};
		auto chars = Decode(reading);
		size_t logical = a.logical_text.size();
		for (size_t i = 0, byte = 0; i < chars.size(); ++i) {
			size_t len = Encode({chars[i]}).size();
			char32_t cp = Hira(chars[i]);
			// Canonically compose common decomposed dakuten/handakuten without
			// rewriting the original text or its byte coordinates.
			if ((cp == 0x3099 || cp == 0x309a) && !a.reading.characters.empty()) {
				auto& prev = a.reading.characters.back();
				std::u32string unvoiced = U"かきくけこさしすせそたちつてとはひふへほ";
				if (cp == 0x3099 && (unvoiced.find(prev.kana)!=std::u32string::npos || prev.kana==U'う')) {
					prev.kana = prev.kana==U'う' ? U'ゔ' : prev.kana+1; prev.logical_end=logical+byte+len;
				} else if (cp == 0x309a && std::u32string(U"はひふへほ").find(prev.kana)!=std::u32string::npos) {
					prev.kana += 2; prev.logical_end=logical+byte+len;
				} else a.error="Unsupported combining kana mark";
			} else a.reading.characters.push_back({cp,a.spans.size(),logical+byte,logical+byte+len});
			byte += len;
		}
		a.logical_text += reading;
		span.reading_end = a.reading.characters.size(); a.spans.push_back(std::move(span));
	}
	// Parse karaoke-free ASS. Tags remain byte-for-byte in the surface, but
	// consume no reading position. Ruby spans do not imply lexical boundaries.
	void Read(std::string const& source) {
		a.source = source;
		for (size_t p=0; p<source.size();) {
			if (source[p]=='{') {
				auto end=source.find('}',p); if(end==std::string::npos) { a.error="Unclosed ASS override"; return; }
				a.surface+=source.substr(p,end+1-p); p=end+1; continue;
			}
			if (source[p]=='<' ) {
				auto end=source.find('>',p), pipe=source.find('|',p);
				if(end==std::string::npos || pipe==std::string::npos || pipe>=end || pipe==p+1 || pipe+1==end ||
					source.find('<',p+1)<end || source.find('|',pipe+1)<end) { a.error="Malformed Mangetsu reading span"; return; }
				auto display=source.substr(p+1,pipe-p-1), reading=source.substr(pipe+1,end-pipe-1);
				// The renderer rejects non-karaoke tags inside ruby. The adapter
				// removed karaoke tags already, so any remaining block is unsafe.
				if (display.find('{')!=std::string::npos || reading.find('{')!=std::string::npos) { a.error="Unsupported override inside Mangetsu group"; return; }
				a.surface+=source.substr(p,end+1-p); Span(display,reading,p,end+1,true); p=end+1; continue;
			}
			if (source[p]=='\\' && p+1<source.size() && (source[p+1]=='N'||source[p+1]=='n'||source[p+1]=='h')) {
				a.surface+=source.substr(p,2); Span(source.substr(p,2)," ",p,p+2,false);
				// Logical ASS retains the two-byte escape.
				a.logical_text.back()='\\'; a.logical_text+=source[p+1]; a.reading.characters.back().logical_end++;
				p+=2; continue;
			}
			if (static_cast<unsigned char>(source[p])<128 && std::isalpha(static_cast<unsigned char>(source[p]))) {
				size_t end=p+1; while(end<source.size() && (std::isalpha(static_cast<unsigned char>(source[end]))||source[end]=='\''||source[end]=='-')) ++end;
				auto raw=source.substr(p,end-p); auto r=ConvertRomaji(raw);
				if(!r.valid) { a.error=r.error; return; }
				a.surface+="<"+raw+"|"+r.kana+">"; Span(raw,r.kana,p,end,false); p=end; continue;
			}
			// Dictionary lookup is only for visible text without explicit ruby.
			Lexeme const* best=nullptr;
			for(auto const& l:Lexicon()) if(source.compare(p,l.source.size(),l.source)==0 && (!best||l.source.size()>best->source.size())) best=&l;
			auto lead=static_cast<unsigned char>(source[p]);
			size_t bytes=lead<128?1:lead<224?2:lead<240?3:4;
			auto cp=Decode(source.substr(p,bytes))[0];
			if(!Kana(Hira(cp)) && !Separator(cp) && best) {
				a.surface+="<"+best->source+"|"+best->reading+">";
				Span(best->source,best->reading,p,p+best->source.size(),false); p+=best->source.size(); continue;
			}
			auto raw=Encode({cp}); a.surface+=raw; Span(raw,raw,p,p+raw.size(),false); p+=raw.size();
		}
	}
};

void Language(Analysis& a) {
	std::u32string r; for(auto const& c:a.reading.characters) r+=c.kana;
	struct Entry { std::u32string reading; Lexeme const* lexeme; };
	std::vector<Entry> lex;
	for(auto const& w:Lexicon()) lex.push_back({Decode(w.reading),&w});
	auto lookup=[&](size_t p)->Entry const* {
		Entry const* best=nullptr;
		for(auto const& e:lex) if(r.compare(p,e.reading.size(),e.reading)==0 && (!best||e.reading.size()>best->reading.size())) best=&e;
		return best;
	};
	for(size_t p=0;p<r.size();) {
		if(Separator(r[p])) { ++p; continue; }
		auto e=lookup(p);
		if(e) {
			a.words.push_back({p,p+e->reading.size(),Encode(e->reading),e->lexeme->source,e->lexeme->kind,true}); p+=e->reading.size(); continue;
		}
		// Particles are recognized in context, never from vowel identity.
		bool previous=!a.words.empty() && a.words.back().end==p && a.words.back().certain;
		bool particle=false;
		for(auto const& s:{U"けど",U"から",U"ので",U"の",U"を",U"が",U"と",U"に",U"で",U"は",U"わ",U"へ",U"も",U"さ"}) {
			std::u32string part(s); if(r.compare(p,part.size(),part)!=0) continue;
			bool next = p+part.size()==r.size() || lookup(p+part.size())!=nullptr;
			if(previous && (next || part==U"を" || part==U"けど" || part==U"から")) {
				a.words.push_back({p,p+part.size(),Encode(part),Encode(part),WordKind::Particle,true}); p+=part.size(); particle=true; break;
			}
		}
		if(particle) continue;
		// Coalesce unknown characters, without protecting their boundaries.
		if(!a.words.empty() && a.words.back().kind==WordKind::Unknown && a.words.back().end==p) {
			a.words.back().end++; a.words.back().reading+=Encode({r[p]});
		} else a.words.push_back({p,p+1,Encode({r[p]}),"UNKNOWN",WordKind::Unknown,false});
		++p;
	}
	a.language_certain = std::all_of(a.words.begin(),a.words.end(),[](SpokenToken const& w){return w.certain;});
}

void TokenizeAndGate(Analysis& a) {
	auto const& r=a.reading.characters;
	for(size_t i=0;i<r.size();) {
		if(Separator(r[i].kana)) {++i; continue;}
		if(!Kana(r[i].kana)) {a.error="Reading requires kana or strict romaji; add explicit <display|reading> for unknown text"; return;}
		size_t end=i+1;
		if(end<r.size() && Cluster(r[i].kana,r[end].kana)) ++end;
		std::u32string text; for(size_t j=i;j<end;++j) text+=r[j].kana;
		size_t word=unknown;
		for(size_t j=0;j<a.words.size();++j) if(a.words[j].begin<=i && a.words[j].end>=end) {word=j;break;}
		a.morae.push_back({Encode(text),i,end,r[i].logical_begin,r[end-1].logical_end,r[i].span,word}); i=end;
	}
	a.boundaries.resize(a.morae.size()+1,{true,"line checkpoint"});
	a.graph.resize(a.morae.size());
	for(size_t i=0;i<a.morae.size();++i) {
		a.graph[i].push_back({i,1,{},"base mora"});
		if(!i) continue;
		auto const& left=a.morae[i-1]; auto const& right=a.morae[i]; auto& b=a.boundaries[i];
		b={true,"unsupported phonological relationship"};
		if(left.reading_end!=right.reading_begin) { b.reason="punctuation / spacing"; continue; }
		bool same=left.lexeme!=unknown && left.lexeme==right.lexeme && a.words[left.lexeme].certain;
		if(left.lexeme!=right.lexeme && left.lexeme!=unknown && right.lexeme!=unknown && a.words[left.lexeme].certain && a.words[right.lexeme].certain) {
			b.reason=(a.words[left.lexeme].kind==WordKind::Particle||a.words[right.lexeme].kind==WordKind::Particle) ? "high-confidence particle boundary" : "separate spoken lexical units"; continue;
		}
		if(same) {
			auto const& w=a.words[left.lexeme];
			bool ending=false; for(auto const& l:Lexicon()) if(l.source==w.lemma && l.reading==w.reading && l.separate_ending) ending=true;
			if(ending && right.reading_end==w.end) {b.reason="known verb/auxiliary ending: retain final mora";continue;}
		}
		Join type; std::string reason;
		char v=Vowel(left.text);
		if(right.text==u8"ー" && (v || left.text==u8"ー")) {type=Join::LongMark;reason="long mark continuation";}
		else if(right.text==u8"っ" && v) {type=Join::Sokuon;reason="supported preceding-mora sokuon grouping (separate also legal)";}
		else if(right.text==u8"ん" && v) {type=Join::Nasal;reason="moraic nasal continuation";}
		else if(v && (right.text==u8"あ"||right.text==u8"い"||right.text==u8"う"||right.text==u8"え"||right.text==u8"お")) {
			char next=Vowel(right.text);
			bool long_like=v==next || (v=='o'&&next=='u') || (v=='e'&&next=='i');
			// General vowel adjacency requires a recognized shared lexeme.
			// Unknown text retains long-vowel ambiguity, not arbitrary vowel joins.
			if(!long_like && !same) continue;
			type=long_like?Join::WrittenLongVowel:Join::Vowel; reason=long_like?"written long-vowel-like sequence":"vowel adjacency within a known spoken lexeme";
		} else continue;
		b={false,reason};
		a.graph[i-1].push_back({i-1,2,{type,same,left.span==right.span,!same},reason});
	}
}
} // namespace

RomajiResult ConvertRomaji(std::string const& text) {
	RomajiResult out;
	std::string s=text; for(char& c:s) if(static_cast<unsigned char>(c)<128) c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	auto append=[&](std::string const& kana,size_t from,size_t to) {
		out.kana+=kana; for(size_t i=0;i<Decode(kana).size();++i) out.offsets.emplace_back(from,to);
	};
	for(size_t p=0;p<s.size();) {
		if(s[p]=='-') {append(u8"ー",p,p+1);++p;continue;}
		if(s[p]==' ' || s[p]=='\t') {append(" ",p,p+1);++p;continue;}
		if(s[p]=='n') {
			if(p+1==s.size()) {append(u8"ん",p,p+1);++p;continue;}
			if(s[p+1]=='\'') {append(u8"ん",p,p+2);p+=2;continue;}
			if(s[p+1]=='n') {
				size_t len=(p+2<s.size() && std::string("aiueoy").find(s[p+2])!=std::string::npos) ? 1 : 2;
				append(u8"ん",p,p+len);p+=len;continue;
			}
			if(std::string("aiueoy").find(s[p+1])==std::string::npos) {append(u8"ん",p,p+1);++p;continue;}
		}
		if(p+1<s.size() && s[p]==s[p+1] && std::string("bcdfghjkpqrstvwxyz").find(s[p])!=std::string::npos) {
			append(u8"っ",p,p+1);++p;continue;
		}
		size_t length=0; std::string kana;
		for(auto const& pair:RomanTable()) if(pair.first.size()>length && s.compare(p,pair.first.size(),pair.first)==0) {length=pair.first.size();kana=pair.second;}
		if(!length) {out.kana.clear();out.offsets.clear();out.error="Invalid strict romaji at byte "+std::to_string(p)+": "+text;return out;}
		append(kana,p,p+length);p+=length;
	}
	out.valid=!out.kana.empty(); if(!out.valid) out.error="Empty reading";
	return out;
}

Analysis Analyze(std::string const& source) {
	Reader reader;
	try {
		reader.Read(source);
		if(!reader.a.error.empty()) return reader.a;
		// Explicit romaji is also a strict encoding. Preserve source offsets.
		for(size_t s=0;s<reader.a.spans.size();++s) {
			auto const& span=reader.a.spans[s];
			if(span.explicit_reading && std::any_of(span.original_reading.begin(),span.original_reading.end(),[](unsigned char c){return c<128&&std::isalpha(c);})) {
				auto conv=ConvertRomaji(span.original_reading); if(!conv.valid) {reader.a.error=conv.error;return reader.a;}
				auto chars=Decode(conv.kana); std::vector<ReadingCharacter> replacement;
				size_t start=reader.a.reading.characters[span.reading_begin].logical_begin;
				for(size_t i=0;i<chars.size();++i) replacement.push_back({chars[i],s,start+conv.offsets[i].first,start+conv.offsets[i].second});
				auto& all=reader.a.reading.characters;
				auto first=all.erase(all.begin()+span.reading_begin,all.begin()+span.reading_end);all.insert(first,replacement.begin(),replacement.end());
				ptrdiff_t delta=static_cast<ptrdiff_t>(chars.size())-static_cast<ptrdiff_t>(span.reading_end-span.reading_begin);
				reader.a.spans[s].reading_end+=delta;
				for(size_t j=s+1;j<reader.a.spans.size();++j) {reader.a.spans[j].reading_begin+=delta;reader.a.spans[j].reading_end+=delta;}
			}
		}
		std::u32string normalized; for(auto const& c:reader.a.reading.characters) normalized+=c.kana;
		reader.a.reading.normalized=Encode(normalized);
		Language(reader.a); TokenizeAndGate(reader.a);
		if(reader.a.morae.empty() && reader.a.error.empty()) reader.a.error="No sung morae";
	} catch(std::exception const&) {reader.a.error="Invalid UTF-8 reading";}
	return reader.a;
}
} }
