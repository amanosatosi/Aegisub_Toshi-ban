// Copyright (c) 2026, Aegisub Project. Distributed under the ISC license.
#include "mangetsu_metadata.h"

#include "ass_dialogue.h"
#include "ass_file.h"

#include <string_view>

namespace mangetsu {
namespace {
constexpr std::string_view whitespace = " \t\r\n\f\v";
constexpr char style_effect[] = "mangetsu-style";
constexpr char actor_effect[] = "mangetsu-colorcoding";

// Independent annotations are flat braced tokens. Reject malformed or
// nested annotations rather than finding a marker inside one.
size_t AnnotationEnd(std::string_view effect) {
	auto end = effect.find_first_of("{}", 1);
	return end != effect.npos && effect[end] == '}' ? end : effect.npos;
}
}

const char *MetadataEffect(AssDialogue const& line) {
	if (!line.Comment) return nullptr;
	std::string_view effect = line.Effect.get();
	const char *kind = nullptr;
	while (!effect.empty()) {
		auto first = effect.find_first_not_of(whitespace);
		if (first == effect.npos) break;
		effect.remove_prefix(first);
		if (effect.front() == '{') {
			auto end = AnnotationEnd(effect);
			if (end == effect.npos) return nullptr;
			effect.remove_prefix(end + 1);
			continue;
		}
		auto end = effect.find_first_of(" \t\r\n\f\v{}");
		auto token = effect.substr(0, end);
		if (kind || token.empty()) return nullptr;
		if (token == style_effect) kind = style_effect;
		else if (token == actor_effect && !line.Actor.get().empty()) kind = actor_effect;
		else return nullptr;
		effect.remove_prefix(token.size());
	}
	return kind;
}

bool IsStyleMetadata(AssDialogue const& line) {
	return MetadataEffect(line) == style_effect;
}

bool IsMetadataComment(AssDialogue const& line) {
	return MetadataEffect(line) != nullptr;
}

bool HasInheritanceMarker(AssDialogue const& line) {
	constexpr std::string_view marker = "{uketsugu}";
	std::string_view effect = line.Effect.get();
	size_t depth = 0;
	for (size_t i = 0; i < effect.size(); ++i) {
		if (effect[i] == '{') {
			if (depth == 0 && effect.substr(i, marker.size()) == marker) return true;
			++depth;
		}
		else if (effect[i] == '}' && depth > 0) --depth;
	}
	return false;
}

bool MayAffectMetadata(AssFile const& subs, AssDialogue const& target) {
	for (auto const& line : subs.Events) {
		if (line.Row == target.Row) return line.Comment;
		if (!IsMetadataComment(line)) return false;
	}
	return false;
}
}
