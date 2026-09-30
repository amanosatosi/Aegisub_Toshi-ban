#pragma once

#include <functional>
#include <string>
#include <vector>

// Shared by automatic and manual checks and by all updater links.
inline const std::string ApplicationUpdateRepository = "amanosatosi/Aegisub_Toshi-ban";
inline const std::string ApplicationReleasesUrl = "https://github.com/" + ApplicationUpdateRepository + "/releases";
inline const std::string ApplicationApiPath = "/repos/" + ApplicationUpdateRepository;
inline constexpr char ApplicationApiHost[] = "api.github.com";

std::string DownloadApplicationUpdateJson(std::string const& path);

struct AegisubUpdateDescription {
	std::string url;
	std::string friendly_name;
	std::string description;
};

// Fetch accepts a path on ApplicationApiHost. Commit ancestry preserves the
// old revision ordering for tagged and development builds, including two-part
// Toshi-ban release tags (1.0, 1.1, 2.0).
std::vector<AegisubUpdateDescription> FindApplicationUpdates(
	std::function<std::string(std::string const&)> const& fetch,
	std::string const& current_commit, bool stable_only);
