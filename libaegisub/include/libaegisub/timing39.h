// Copyright (c) 2026, JibunSenyou contributors. ISC license.
#pragma once

#include <array>
#include <cstddef>
#include <limits>
#include <string>
#include <vector>

namespace agi { namespace timing39 {
constexpr size_t unknown = std::numeric_limits<size_t>::max();
enum class Confidence { Green, Yellow, Red };
enum class WordKind { Unknown, Noun, Verb, Adjective, Particle, Auxiliary, Expression };
enum class Join { Single, LongMark, Sokuon, Nasal, WrittenLongVowel, Vowel };

struct SourceSpan {
	size_t begin = 0, end = 0; // UTF-8 byte offsets in Analysis::span_source
	std::string display, original_reading;
	size_t reading_begin = 0, reading_end = 0; // normalized codepoint offsets
	bool explicit_reading = false;
};
struct ReadingCharacter {
	char32_t kana;
	size_t span, logical_begin, logical_end; // bytes in AssKaraoke logical surface
};
struct ReadingModel {
	std::string normalized;
	std::vector<ReadingCharacter> characters;
};
struct SpokenToken {
	size_t begin, end; // normalized codepoint offsets, independent of ruby spans
	std::string reading, lemma;
	WordKind kind = WordKind::Unknown;
	bool certain = false;
	bool source_supported = false;
};
struct BaseMora {
	std::string text;
	size_t reading_begin, reading_end, logical_begin, logical_end, span, lexeme;
};
struct BoundaryAnalysis {
	bool fixed = true;
	std::string reason;
};
struct CandidateFeatures {
	Join type = Join::Single;
	bool same_lexeme = false, same_reading_span = false, uncertain_language = false;
	bool source_supported = false;
};
struct TimingGroupCandidate {
	size_t first, count;
	CandidateFeatures features;
	std::string reason;
};
struct Analysis {
	std::string source, span_source, surface, logical_text, error;
	std::vector<SourceSpan> spans;
	ReadingModel reading;
	std::vector<SpokenToken> words;
	// Conservative structural/morphological alternatives, never substituted readings.
	std::vector<std::string> language_notes;
	std::vector<BaseMora> morae;
	std::vector<BoundaryAnalysis> boundaries; // boundary BEFORE mora i
	std::vector<std::vector<TimingGroupCandidate>> graph;
	bool language_certain = false;
};
struct RomajiResult {
	bool valid = false;
	std::string kana, error;
	// One original input byte interval for each output codepoint.
	std::vector<std::pair<size_t, size_t>> offsets;
};
RomajiResult ConvertRomaji(std::string const& text);
Analysis Analyze(std::string const& source);

struct TimingBlock {
	int start = 0, end = 0; // absolute media milliseconds, never wall time
	bool gap = false;
	bool operator==(TimingBlock const& rhs) const { return start == rhs.start && end == rhs.end && gap == rhs.gap; }
};
class TimingLane {
	std::array<int, 2> keys;
	std::array<bool, 2> held{{false, false}};
	int owner = -1, checkpoint_start = 0, checkpoint_end = 0, gap_start = 0;
	bool enabled = false;
	std::vector<TimingBlock> blocks;
public:
	TimingLane(int first, int second) : keys{{first, second}} { }
	void Begin(int start, int end);
	bool KeyDown(int key, int ms);
	bool KeyUp(int key, int ms);
	void Finish(int ms); // close owner, preserve capture, clear held state
	void Clear();
	bool Owns(int key) const;
	bool Active() const { return owner >= 0; }
	std::vector<TimingBlock> const& Blocks() const { return blocks; }
	std::vector<TimingBlock> Preview(int ms) const;
};
struct TimingCaptureSession {
	std::array<TimingLane, 2> lanes{{TimingLane('F', 'J'), TimingLane('D', 'K')}};
	void Finish(int ms) { for (auto& lane : lanes) lane.Finish(ms); }
};
struct TimingAssignment {
	size_t timing_block, first_mora, mora_count;
	bool operator==(TimingAssignment const& rhs) const {
		return timing_block == rhs.timing_block && first_mora == rhs.first_mora && mora_count == rhs.mora_count;
	}
};
struct Score {
	double prior = 0, language = 0, duration = 0;
	double Total() const { return prior + language + duration; }
};
struct MatchPath {
	std::vector<TimingAssignment> assignments;
	std::vector<Score> scores;
	double cost = 0;
};
// These are provisional, inspectable priors, not linguistic rules. Illegal
// candidates never reach this scorer regardless of any weight setting.
struct ScoringModel {
	std::array<double, 6> join_prior{{0, 0.25, 0.65, 0.85, 0.6, 1.0}};
	double unknown_language = 0.25, different_span = 0.05, duration_fit = 0.65;
	double source_support = -0.05;
	double ambiguity_margin = 0.75;
	size_t alternatives = 8;
};
struct MatchResult {
	Confidence confidence = Confidence::Red;
	bool language_certain = false;
	double margin = 0;
	std::string reason;
	std::vector<MatchPath> paths;
	std::vector<size_t> uncertain_boundaries;
};
MatchResult Match(Analysis const&, std::vector<TimingBlock> const&, ScoringModel const& = {});
bool ValidAssignments(Analysis const&, std::vector<TimingBlock> const&, std::vector<TimingAssignment> const&);

struct Correction {
	std::vector<TimingAssignment> predicted, corrected;
	size_t boundary = unknown;
	std::string reason;
};
class AssignmentEditor {
	std::vector<std::vector<TimingAssignment>> history;
	size_t cursor = 0;
public:
	std::vector<Correction> corrections;
	void Reset(std::vector<TimingAssignment> assignments);
	std::vector<TimingAssignment> const& Get() const;
	// Transfers a mora between neighboring blocks only through legal edges.
	bool Move(Analysis const&, size_t divider, int delta);
	bool Choose(Analysis const&, std::vector<TimingAssignment> assignments);
	bool Undo();
	bool Redo();
};

struct StyleEvidence {
	std::string style; // opaque identity, never interpreted as a role
	std::vector<int> existing, primary, secondary; // absolute boundary timestamps
};
struct StyleMatch {
	std::string primary, secondary;
	bool ambiguous = true;
	double cost = 0, margin = 0;
};
StyleMatch InferStyles(std::vector<StyleEvidence> const&, double minimum_margin_ms = 30);
std::string Inspect(Analysis const&, std::vector<TimingBlock> const&, MatchResult const&, AssignmentEditor const* = nullptr);
} }
