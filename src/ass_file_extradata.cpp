#include "ass_file.h"
#include "ass_dialogue.h"

#include <algorithm>
#include <functional>
#include <unordered_map>
#include <unordered_set>

uint32_t AssFile::AddExtradata(std::string const& key, std::string const& value) {
	for (auto const& data : Extradata) {
		// perform brute-force deduplication by simple key and value comparison
		if (key == data.key && value == data.value) {
			return data.id;
		}
	}
	Extradata.push_back(ExtradataEntry{next_extradata_id, 0, key, value});
	return next_extradata_id++; // return old value, then post-increment
}

void AssFile::SetExtradataValue(AssDialogue& line, std::string const& key, std::string const& value, bool del) {
	std::vector<uint32_t> id_list = line.ExtradataIds;
	std::vector<bool> to_erase(id_list.size());
	bool dirty = false;
	bool found = false;

	std::vector<ExtradataEntry> entry_list = GetExtradata(id_list);
	for (int i = static_cast<int>(id_list.size()) - 1; i >= 0; i--) {
		auto entry = std::find_if(entry_list.begin(),entry_list.end(),[&](auto const& e) { return e.id == id_list[i]; });
		if (entry != entry_list.end() && entry->key == key) {
			if (!del && entry->value == value) {
				found = true;
			} else {
				to_erase[i] = true;
				dirty = true;
			}
		}
	}

	// The key is already set, we don't need to change anything
	if (found && !dirty)
		return;

	for (int i = id_list.size() - 1; i >= 0; i--) {
		if (to_erase[i])
			id_list.erase(id_list.begin() + i, id_list.begin() + i + 1);
	}

	if (!del && !found)
		id_list.push_back(AddExtradata(key, value));

	line.ExtradataIds = id_list;
}

namespace {
struct extradata_id_cmp {
	bool operator()(ExtradataEntry const& e, uint32_t id) {
		return e.id < id;
	}
	bool operator()(uint32_t id, ExtradataEntry const& e) {
		return id < e.id;
	}
};

template<typename ExtradataType, typename Func>
void enumerate_extradata(ExtradataType&& extradata, std::vector<uint32_t> const& id_list, Func&& f) {
	auto begin = extradata.begin(), end = extradata.end();
	for (auto id : id_list) {
		auto it = lower_bound(begin, end, id, extradata_id_cmp{});
		if (it != end && it->id == id) {
			f(*it);
			begin = it;
		}
	}
}

template<typename K, typename V>
using reference_map = std::unordered_map<std::reference_wrapper<const K>, V, std::hash<K>, std::equal_to<K>>;
}

std::vector<ExtradataEntry> AssFile::GetExtradata(std::vector<uint32_t> const& id_list) const {
	std::vector<ExtradataEntry> result;
	enumerate_extradata(Extradata, id_list, [&](ExtradataEntry const& e) {
		result.push_back(e);
	});
	return result;
}

void AssFile::CleanExtradata() {
	if (Extradata.empty()) return;

	std::unordered_set<uint32_t> ids_used;
	for (auto& line : Events) {
		if (line.ExtradataIds.get().empty()) continue;

		// Find the ID for each unique key in the line
		reference_map<std::string, uint32_t> keys_used;
		enumerate_extradata(Extradata, line.ExtradataIds.get(), [&](ExtradataEntry const& e) {
			keys_used[e.key] = e.id;
		});

		for (auto const& used : keys_used)
			ids_used.insert(used.second);

		// If any keys were duplicated or missing, update the id list
		if (keys_used.size() != line.ExtradataIds.get().size()) {
			std::vector<uint32_t> ids;
			ids.reserve(keys_used.size());
			for (auto const& used : keys_used)
				ids.push_back(used.second);
			std::sort(begin(ids), end(ids));
			line.ExtradataIds = std::move(ids);
		}
	}

	for (ExtradataEntry &e : Extradata) {
		if (ids_used.count(e.id))
			e.expiration_counter = 0;
		else
			e.expiration_counter++;
	}
	if (ids_used.size() != Extradata.size()) {
		// Erase all no-longer-used extradata entries
		Extradata.erase(std::remove_if(begin(Extradata), end(Extradata), [&](ExtradataEntry const& e) {
			return e.expiration_counter >= 10;
		}), end(Extradata));
	}
}
