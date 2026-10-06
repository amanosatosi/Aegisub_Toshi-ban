#include "motion_track_commit.h"

#include "../ass_file.h"
#include <libaegisub/cajun/elements.h>
#include <libaegisub/cajun/reader.h>

#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>

namespace motion_tracking {
namespace {
struct Family {
	std::string token;
	AssDialogueBase original;
	std::vector<std::pair<std::string,std::string>> extra;
	int count = 0;
};

Family Decode(std::string const& text) {
	try {
		std::istringstream stream(text);
		json::UnknownElement root;
		json::Reader::Read(root,stream);
		auto const& object = static_cast<json::Object const&>(root);
		if (static_cast<json::Integer const&>(object.at("version")) != 1) throw std::runtime_error("version");
		AssDialogue original(static_cast<json::String const&>(object.at("original")));
		original.Start = static_cast<json::Integer const&>(object.at("start"));
		original.End = static_cast<json::Integer const&>(object.at("end"));
		if (object.count("fields")) {
			auto const& fields = static_cast<json::Object const&>(object.at("fields"));
			original.Text = static_cast<json::String const&>(fields.at("text"));
			original.Style = static_cast<json::String const&>(fields.at("style"));
			original.Actor = static_cast<json::String const&>(fields.at("actor"));
			original.Effect = static_cast<json::String const&>(fields.at("effect"));
			original.Layer = static_cast<json::Integer const&>(fields.at("layer"));
			original.Comment = static_cast<json::Boolean const&>(fields.at("comment"));
			for (size_t i = 0; i < 3; ++i) original.Margin[i] = static_cast<json::Integer const&>(fields.at("margin"+std::to_string(i)));
		}
		original.ExtradataIds = std::vector<uint32_t>{};
		Family family{static_cast<json::String const&>(object.at("family")),original,{},
			static_cast<int>(static_cast<json::Integer const&>(object.at("count")))};
		if (family.token.empty() || family.count < 1) throw std::runtime_error("family");
		for (auto const& item : static_cast<json::Object const&>(object.at("extra")))
			family.extra.emplace_back(item.first,static_cast<json::String const&>(item.second));
		return family;
	}
	catch (...) { throw std::invalid_argument("Motion Revert metadata is damaged or unsupported."); }
}

std::string Metadata(AssFile const& file, AssDialogue const& line) {
	for (auto const& entry : file.GetExtradata(line.ExtradataIds.get()))
		if (entry.key == MotionFamilyKey) return entry.value;
	return {};
}

AssDialogue* Find(AssFile& file, int id) {
	for (auto& line : file.Events) if (line.Id == id) return &line;
	return nullptr;
}
}

bool CanRevertMotion(AssFile const& file, AssDialogue const& line) {
	return HasMotionLayers(line.Text.get()) || !Metadata(file,line).empty();
}

MotionCommitSelection InstallMotionApplications(AssFile& file,
	std::vector<MotionPlannedSource> const& plans, int active_source, int) {
	struct Prepared { AssDialogue* source; std::string text; };
	std::vector<Prepared> prepared;
	std::set<int> ids;
	// Validate and allocate all text before changing any selected line.
	for (auto const& plan : plans) {
		auto source = Find(file,plan.source_id);
		if (!source || !ids.insert(plan.source_id).second || plan.application.events.size() != 1)
			throw std::invalid_argument("Apply requires exactly one event per original subtitle.");
		auto const& event = plan.application.events.front();
		if (event.Start.GetMilliseconds() != source->Start.GetMilliseconds() ||
			event.End.GetMilliseconds() != source->End.GetMilliseconds())
			throw std::invalid_argument("Apply cannot change subtitle timing.");
		if (!Metadata(file,*source).empty())
			throw std::invalid_argument("Revert the legacy split motion family once before applying Mangetsu tracking.");
		prepared.push_back({source,event.Text.get()});
	}
	MotionCommitSelection selection;
	selection.selected.reserve(prepared.size());
	for (auto const& item : prepared) {
		item.source->Text = item.text;
		selection.selected.push_back(item.source);
		if (item.source->Id == active_source) selection.active = item.source;
	}
	if (!selection.active && !selection.selected.empty()) selection.active = selection.selected.front();
	return selection;
}

MotionCommitSelection RevertMotionFamilies(AssFile& file,
	std::vector<AssDialogue*> const& selected, int active_id) {
	std::map<std::string,Family> families;
	std::vector<std::pair<AssDialogue*,std::string>> layers;
	std::string active_family;
	for (auto line : selected) {
		auto metadata = Metadata(file,*line);
		if (metadata.empty()) {
			if (HasMotionLayers(line->Text.get())) layers.emplace_back(line,RemoveMotionLayers(line->Text.get()));
			continue;
		}
		auto family = Decode(metadata);
		if (line->Id == active_id) active_family = family.token;
		families.emplace(family.token,std::move(family));
	}

	std::map<std::string,std::vector<AssDialogue*>> members;
	for (auto& line : file.Events) {
		auto metadata = Metadata(file,line);
		if (metadata.empty()) continue;
		try {
			auto family = Decode(metadata);
			if (families.count(family.token)) members[family.token].push_back(&line);
		}
		catch (std::invalid_argument const&) { /* unrelated damaged metadata is not a family member */ }
	}
	// Missing/copied family members make deletion ambiguous. Never silently
	// remove an incomplete or duplicated family, or an unrelated damaged family.
	for (auto const& item : families)
		if (members[item.first].size() != static_cast<size_t>(item.second.count))
			throw std::invalid_argument("This motion family is incomplete or was duplicated. Use Undo or restore its missing events.");
	MotionCommitSelection selection;
	for (auto const& item : families) {
		auto const& family = item.second;
		auto restored = new AssDialogue();
		int id = restored->Id;
		static_cast<AssDialogueBase&>(*restored) = family.original;
		restored->Id = id;
		for (auto const& extra : family.extra) file.SetExtradataValue(*restored,extra.first,extra.second);
		auto& lines = members[item.first];
		file.Events.insert(file.Events.iterator_to(*lines.front()),*restored);
		for (auto line : lines) { file.Events.erase(file.Events.iterator_to(*line)); delete line; }
		selection.selected.push_back(restored);
		if (item.first == active_family) selection.active = restored;
	}
	// New tracking layers are removed in place. User edits made after Apply,
	// unrelated transforms, folds, IDs, exact timing and extradata survive.
	for (auto const& layer : layers) {
		auto line = layer.first;
		line->Text = layer.second;
		selection.selected.push_back(line);
		if (line->Id == active_id) selection.active = line;
	}
	if (selection.selected.empty()) throw std::invalid_argument("Select a subtitle with generated tracking to Revert.");
	if (!selection.active) selection.active = selection.selected.front();
	return selection;
}
}
