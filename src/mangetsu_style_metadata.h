// Copyright (c) 2026, Aegisub Project. Distributed under the ISC license.
#pragma once
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

class AssDialogue;
class AssFile;
class AssStyle;

namespace mangetsu {
struct StyleValue {
	std::string value;
	bool assigned = false;
};
using StyleValues = std::map<std::string, StyleValue>;

// Strict ASS event-style matching, including * prefixes and Default only's
// case normalization. It intentionally differs from AssFile::GetStyle().
AssStyle const *ResolveMetadataStyle(AssFile const& file, std::string name);
std::vector<AssDialogue*> InitialMetadata(AssFile const& file);
std::set<AssDialogue const*> HiddenMetadata(AssFile const& file, bool mangetsu_selected, bool show_metadata);
bool RemoveStyleMetadata(AssFile& file, AssStyle const& style);

// A value object: child dialogs edit copies; only the parent's Apply writes
// the document. Unknown text and gradient sources retain their exact order.
class StyleMetadataDraft {
	std::vector<std::string> sources;
	std::map<std::string, std::optional<std::string>> changes;
public:
	StyleMetadataDraft() = default;
	StyleMetadataDraft(AssFile const& file, AssStyle const& style);
	void Set(std::string const& property, std::optional<std::string> value);
	bool Dirty() const { return !changes.empty(); }
	StyleValues Values(AssStyle const& style, int script_wrap) const;
	std::vector<std::string> Texts(AssStyle const& style, int script_wrap) const;
	bool Apply(AssFile& file, AssStyle const *source, std::string const& name,
		AssStyle const& effective_style, bool copy) const;
};

// Locale-independent ASS numbers. GUI validation rejects non-finite values.
std::string StyleNumber(double value);
bool ParseStyleNumber(std::string const& text, double& value);
}
