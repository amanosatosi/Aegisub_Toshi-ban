#include "motion_track_commit.h"

#include "../ass_file.h"
#include <libaegisub/cajun/elements.h>
#include <libaegisub/cajun/reader.h>
#include <libaegisub/cajun/writer.h>

#include <algorithm>
#include <chrono>
#include <map>
#include <memory>
#include <random>
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

std::string NewToken() {
	static std::mt19937_64 random(std::random_device{}());
	std::ostringstream s;
	s << std::hex << random() << '-' << random();
	return s.str();
}

std::string Encode(AssFile const& file, AssDialogue const& source, int count) {
	json::Object object;
	object["version"] = json::Integer(1);
	object["family"] = json::String(NewToken());
	object["count"] = json::Integer(count);
	// EntryData carries style/actor/effect/margins/layer/comment/text. Store
	// exact native millisecond times separately to avoid centisecond rounding.
	object["original"] = json::String(source.GetEntryData());
	object["start"] = json::Integer(int(source.Start));
	object["end"] = json::Integer(int(source.End));
	json::Object fields;
	fields["text"] = json::String(source.Text.get());
	fields["style"] = json::String(source.Style.get());
	fields["actor"] = json::String(source.Actor.get());
	fields["effect"] = json::String(source.Effect.get());
	fields["layer"] = json::Integer(source.Layer);
	fields["comment"] = json::Boolean(source.Comment);
	for (size_t i = 0; i < 3; ++i) fields["margin"+std::to_string(i)] = json::Integer(source.Margin[i]);
	object["fields"] = std::move(fields);
	json::Object extra;
	for (auto const& entry : file.GetExtradata(source.ExtradataIds.get()))
		if (entry.key != MotionFamilyKey) extra[entry.key] = json::String(entry.value);
	// Values, rather than old IDs, survive extradata garbage collection and save/reload.
	object["extra"] = std::move(extra);
	std::ostringstream stream;
	agi::JsonWriter::Write(object,stream);
	return stream.str();
}

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
	return !Metadata(file,line).empty();
}

MotionCommitSelection InstallMotionApplications(AssFile& file,
	std::vector<MotionPlannedSource> const& plans, int active_source, int reference_time) {
	struct Prepared {
		AssDialogue* source;
		std::string metadata;
		std::vector<std::unique_ptr<AssDialogue>> events;
	};
	std::vector<Prepared> prepared;
	std::set<int> ids;
	for (auto const& plan : plans) {
		auto source = Find(file,plan.source_id);
		if (!source || !ids.insert(plan.source_id).second || plan.application.events.empty())
			throw std::invalid_argument("The source subtitle changed before Apply.");
		if (CanRevertMotion(file,*source)) throw std::invalid_argument("Revert this motion family before applying another track.");
		Prepared item{source,Encode(file,*source,static_cast<int>(plan.application.events.size())),{}};
		for (auto const& event : plan.application.events) {
			auto generated = std::make_unique<AssDialogue>();
			int id = generated->Id;
			static_cast<AssDialogueBase&>(*generated) = event;
			generated->Id = id;
			generated->Fold = {}; // a source fold must not be duplicated on each split
			// Preserve a fold delimiter only on its proper outer event.
			bool keep_fold = source->Fold.hasFold() && (source->Fold.isEnd() ?
				item.events.size()+1 == plan.application.events.size() : item.events.empty());
			if (!keep_fold) file.DeleteExtradataValue(*generated,"_aegi_folddata");
			item.events.push_back(std::move(generated));
		}
		prepared.push_back(std::move(item));
	}
	MotionCommitSelection selection;
	size_t count = 0;
	for (auto const& item : prepared) count += item.events.size();
	selection.selected.reserve(count);
	for (auto& item : prepared) for (auto& event : item.events)
		file.SetExtradataValue(*event,MotionFamilyKey,item.metadata);
	for (auto& item : prepared) {
		auto position = file.Events.iterator_to(*item.source);
		for (auto& event : item.events) {
			auto raw = event.release();
			file.Events.insert(position,*raw);
			selection.selected.push_back(raw);
			if (item.source->Id == active_source && (!selection.active ||
				(raw->Start <= reference_time && raw->End > reference_time))) selection.active = raw;
		}
		file.Events.erase(position);
		delete item.source;
	}
	if (!selection.active && !selection.selected.empty()) selection.active = selection.selected.front();
	return selection;
}

MotionCommitSelection RevertMotionFamilies(AssFile& file,
	std::vector<AssDialogue*> const& selected, int active_id) {
	std::map<std::string,Family> families;
	std::string active_family;
	for (auto line : selected) {
		auto metadata = Metadata(file,*line);
		if (metadata.empty()) continue;
		auto family = Decode(metadata);
		if (line->Id == active_id) active_family = family.token;
		families.emplace(family.token,std::move(family));
	}
	if (families.empty()) throw std::invalid_argument("Select a generated motion event to Revert.");
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
	if (!selection.active) selection.active = selection.selected.front();
	return selection;
}
}
