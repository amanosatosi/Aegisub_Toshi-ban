#include "application_updates.h"

#include <gtest/gtest.h>
#include <map>
#include <stdexcept>

namespace {
std::string Release(std::string const& tag, bool prerelease = false, bool draft = false,
	std::string const& url = "https://github.com/amanosatosi/Aegisub_Toshi-ban/releases/tag/2.0") {
	return "{\"tag_name\":\"" + tag + "\",\"name\":null,\"body\":null,\"draft\":" +
		(draft ? "true" : "false") + ",\"prerelease\":" + (prerelease ? "true" : "false") +
		",\"html_url\":\"" + url + "\"}";
}

std::string ReleasesPath(unsigned page = 1) {
	return ApplicationApiPath + "/releases?per_page=100&page=" + std::to_string(page);
}

std::string ComparePath(std::string const& tag) {
	return ApplicationApiPath + "/compare/current..." + tag + "?per_page=1";
}
}

TEST(ApplicationUpdates, BothChecksUseToshiBanRepository) {
	EXPECT_EQ("/repos/amanosatosi/Aegisub_Toshi-ban", ApplicationApiPath);
	EXPECT_EQ("https://github.com/amanosatosi/Aegisub_Toshi-ban/releases", ApplicationReleasesUrl);
	EXPECT_STREQ("api.github.com", ApplicationApiHost);
}

TEST(ApplicationUpdates, PreservesSpecificReleaseUrlAndTwoPartTags) {
	std::map<std::string, std::string> replies{
		{ReleasesPath(), "[" + Release("2.0") + "]"},
		{ComparePath("2.0"), R"({"status":"ahead"})"}};
	auto updates = FindApplicationUpdates([&](auto const& path) { return replies.at(path); }, "current", false);
	ASSERT_EQ(1u, updates.size());
	EXPECT_EQ("2.0", updates[0].friendly_name);
	EXPECT_EQ("https://github.com/amanosatosi/Aegisub_Toshi-ban/releases/tag/2.0", updates[0].url);
	EXPECT_TRUE(updates[0].description.empty());
}

TEST(ApplicationUpdates, OlderIdenticalAndDivergedReleasesAreNotUpgrades) {
	for (auto status : {"behind", "identical", "diverged"}) {
		auto updates = FindApplicationUpdates([&](auto const& path) {
			if (path == ReleasesPath()) return "[" + Release("1.1") + "]";
			EXPECT_EQ(ComparePath("1.1"), path);
			return std::string("{\"status\":\"") + status + "\"}";
		}, "current", false);
		EXPECT_TRUE(updates.empty());
	}
}

TEST(ApplicationUpdates, StableChannelSkipsPrereleasesAndAllChannelsSkipDrafts) {
	for (bool stable_only : {false, true}) {
		int comparisons = 0;
		auto updates = FindApplicationUpdates([&](auto const& path) {
			if (path == ReleasesPath()) return "[" + Release("draft", false, true) + "," +
				Release("2.1-beta", true) + "," + Release("2.0") + "]";
			++comparisons;
			return std::string(R"({"status":"ahead"})");
		}, "current", stable_only);
		EXPECT_EQ(stable_only ? 1u : 2u, updates.size());
		EXPECT_EQ(stable_only ? 1 : 2, comparisons);
	}
}

TEST(ApplicationUpdates, PaginatesAndFallsBackToGenericReleasesLink) {
	std::string first_page = "[";
	for (int i = 0; i < 100; ++i) first_page += (i ? "," : "") + Release("draft", false, true);
	first_page += "]";
	int pages = 0;
	auto updates = FindApplicationUpdates([&](auto const& path) {
		if (path == ReleasesPath()) { ++pages; return first_page; }
		if (path == ReleasesPath(2)) { ++pages; return "[" + Release("2.1", false, false, "") + "]"; }
		return std::string(R"({"status":"ahead"})");
	}, "current", false);
	EXPECT_EQ(2, pages);
	ASSERT_EQ(1u, updates.size());
	EXPECT_EQ(ApplicationReleasesUrl, updates[0].url);
}

TEST(ApplicationUpdates, EscapesTagRefsAndPropagatesFailures) {
	FindApplicationUpdates([](auto const& path) {
		if (path == ReleasesPath()) return "[" + Release("releases/2.0") + "]";
		EXPECT_EQ(ComparePath("releases%2F2.0"), path);
		return std::string(R"({"status":"identical"})");
	}, "current", false);
	EXPECT_THROW(FindApplicationUpdates([](auto const&) -> std::string {
		throw std::runtime_error("HTTP 403");
	}, "current", false), std::runtime_error);
	EXPECT_ANY_THROW(FindApplicationUpdates([](auto const&) { return "not JSON"; }, "current", false));
}
