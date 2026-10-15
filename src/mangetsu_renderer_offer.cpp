#include "mangetsu_renderer_offer.h"

#include <libaegisub/make_unique.h>
#include <libaegisub/option.h>
#include <libaegisub/option_value.h>

#include <algorithm>

bool HandleMangetsuRendererOffer(agi::Options& options,
	std::vector<std::string> const& providers,
	std::function<std::optional<bool>()> const& ask)
{
	if (options.Has(MangetsuRendererOfferKey)) return false;

	auto provider = options.Get("Subtitle/Provider");
	if (provider->GetString() != "Mangetsu") {
		if (std::find(providers.begin(), providers.end(), "Mangetsu") == providers.end())
			return false;
		auto answer = ask();
		if (!answer) return false;
		if (*answer) provider->SetString("Mangetsu");
	}

	options.Add(agi::make_unique<agi::OptionValueBool>(MangetsuRendererOfferKey, true));
	return true;
}
