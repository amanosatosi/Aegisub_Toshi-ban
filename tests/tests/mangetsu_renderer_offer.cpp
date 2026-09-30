#include "mangetsu_renderer_offer.h"

#include <libaegisub/fs.h>
#include <libaegisub/make_unique.h>
#include <libaegisub/option.h>
#include <libaegisub/option_value.h>
#include <gtest/gtest.h>

namespace {
constexpr char defaults[] = R"({"Subtitle":{"Provider":"libass"}})";
class MangetsuOffer : public testing::Test {
protected:
	agi::Options options{"data/options/mangetsu-offer.json", defaults, agi::Options::FLUSH_SKIP};
	int prompts = 0;
	std::vector<std::string> providers{"libass", "Mangetsu"};
	bool Run(std::optional<bool> answer) {
		return HandleMangetsuRendererOffer(options, providers, [&] { ++prompts; return answer; });
	}
	void SetUp() override { options.RegisterOptional(MangetsuRendererOfferKey); }
};
}

TEST_F(MangetsuOffer, FirstStartupYesUsesNormalProviderPreference) {
	EXPECT_FALSE(options.Has(MangetsuRendererOfferKey));
	EXPECT_TRUE(Run(true));
	EXPECT_EQ(1, prompts);
	EXPECT_EQ("Mangetsu", options.Get("Subtitle/Provider")->GetString());
	EXPECT_TRUE(options.Has(MangetsuRendererOfferKey));
}

TEST_F(MangetsuOffer, NoPreservesProviderAndPermanentlyDismisses) {
	EXPECT_TRUE(Run(false));
	EXPECT_EQ("libass", options.Get("Subtitle/Provider")->GetString());
	EXPECT_FALSE(Run(true));
	EXPECT_EQ(1, prompts);
}

TEST_F(MangetsuOffer, AlreadySelectedIsMarkedWithoutPromptEvenIfUnavailable) {
	options.Get("Subtitle/Provider")->SetString("Mangetsu");
	providers = {"libass"};
	EXPECT_TRUE(Run(false));
	options.Get("Subtitle/Provider")->SetString("libass");
	providers.push_back("Mangetsu");
	EXPECT_FALSE(Run(true));
	EXPECT_EQ(0, prompts);
}

TEST_F(MangetsuOffer, ExistingFalseMarkerIsHandledByExistence) {
	options.Add(agi::make_unique<agi::OptionValueBool>(MangetsuRendererOfferKey, false));
	EXPECT_FALSE(Run(true));
	EXPECT_EQ(0, prompts);
	EXPECT_EQ("libass", options.Get("Subtitle/Provider")->GetString());
}

TEST_F(MangetsuOffer, UnavailableProviderLeavesMigrationPending) {
	providers = {"libass"};
	EXPECT_FALSE(Run(true));
	EXPECT_FALSE(options.Has(MangetsuRendererOfferKey));
	EXPECT_EQ(0, prompts);
	EXPECT_EQ("libass", options.Get("Subtitle/Provider")->GetString());
}

TEST_F(MangetsuOffer, SuppressedDialogLeavesMigrationPending) {
	EXPECT_FALSE(Run(std::nullopt));
	EXPECT_FALSE(options.Has(MangetsuRendererOfferKey));
	EXPECT_EQ("libass", options.Get("Subtitle/Provider")->GetString());
}

TEST_F(MangetsuOffer, RuntimeMarkerAndSelectionSurviveReloadWithoutDefaults) {
	ASSERT_TRUE(Run(true));
	options.Get(MangetsuRendererOfferKey)->SetBool(false);
	options.Flush();
	agi::Options reloaded{"data/options/mangetsu-offer.json", defaults, agi::Options::FLUSH_SKIP};
	reloaded.RegisterOptional(MangetsuRendererOfferKey);
	reloaded.ConfigUser();
	EXPECT_TRUE(reloaded.Has(MangetsuRendererOfferKey));
	EXPECT_FALSE(reloaded.Get(MangetsuRendererOfferKey)->GetBool());
	EXPECT_EQ("Mangetsu", reloaded.Get("Subtitle/Provider")->GetString());
	EXPECT_FALSE(HandleMangetsuRendererOffer(reloaded, providers, []() -> std::optional<bool> {
		ADD_FAILURE() << "A persisted marker must suppress the offer";
		return false;
	}));
	agi::fs::Remove("data/options/mangetsu-offer.json");
}
