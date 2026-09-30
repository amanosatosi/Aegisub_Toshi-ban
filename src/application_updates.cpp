#include "application_updates.h"

#include <libaegisub/cajun/elements.h>
#include <libaegisub/cajun/reader.h>

#include <sstream>
#include <stdexcept>

namespace {
json::UnknownElement ReadJson(std::string const& body) {
	std::istringstream stream(body);
	json::UnknownElement result;
	json::Reader::Read(result, stream);
	return result;
}

std::string EncodeRef(std::string const& ref) {
	static char const hex[] = "0123456789ABCDEF";
	std::string encoded;
	for (unsigned char c : ref) {
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
			(c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~')
			encoded += c;
		else {
			encoded += '%';
			encoded += hex[c >> 4];
			encoded += hex[c & 15];
		}
	}
	return encoded;
}

std::string StringField(json::Object const& object, char const* key) {
	auto it = object.find(key);
	if (it == object.end()) return {};
	try { return static_cast<json::String const&>(it->second); }
	catch (json::Exception const&) { return {}; } // GitHub permits a null body/name.
}
}

std::vector<AegisubUpdateDescription> FindApplicationUpdates(
	std::function<std::string(std::string const&)> const& fetch,
	std::string const& current_commit, bool stable_only)
{
	if (current_commit.empty()) throw std::runtime_error("This build has no Git commit for update comparison.");
	std::vector<AegisubUpdateDescription> updates;
	for (unsigned page = 1; ; ++page) {
		auto document = ReadJson(fetch(ApplicationApiPath + "/releases?per_page=100&page=" + std::to_string(page)));
		auto const& releases = static_cast<json::Array const&>(document);
		for (auto const& release : releases) {
			auto const& object = static_cast<json::Object const&>(release);
			if (static_cast<json::Boolean const&>(object.at("draft"))) continue;
			if (stable_only && static_cast<json::Boolean const&>(object.at("prerelease"))) continue;
			auto tag = StringField(object, "tag_name");
			if (tag.empty()) continue;

			auto comparison = ReadJson(fetch(ApplicationApiPath + "/compare/" +
				EncodeRef(current_commit) + "..." + EncodeRef(tag) + "?per_page=1"));
			// Older/identical releases and unrelated branches are not upgrades.
			if (StringField(static_cast<json::Object const&>(comparison), "status") != "ahead") continue;
			auto name = StringField(object, "name");
			auto url = StringField(object, "html_url");
			updates.push_back({url.empty() ? ApplicationReleasesUrl : url,
				name.empty() ? tag : name, StringField(object, "body")});
		}
		if (releases.size() < 100) break;
	}
	return updates;
}
