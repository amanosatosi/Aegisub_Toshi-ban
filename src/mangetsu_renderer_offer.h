#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace agi { class Options; }

inline constexpr char MangetsuRendererOfferKey[] = "App/Mangetsu Renderer Offer Seen";

// A missing answer means dialogs are suppressed: leave the migration pending.
bool HandleMangetsuRendererOffer(agi::Options& options,
	std::vector<std::string> const& providers,
	std::function<std::optional<bool>()> const& ask);
