// Copyright (c) 2014, Thomas Goyne <plorkyeran@aegisub.org>
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
// Aegisub Project http://www.aegisub.org/

#include "include/aegisub/subtitles_provider.h"

#include "compat.h"
#include "factory_manager.h"
#include "startup_timing.h"
#include "options.h"
#include "subtitles_provider_csri.h"
#include "subtitles_provider_libass.h"
#include "subtitles_provider_libassmod.h"

#include <libaegisub/log.h>

#include <mutex>
#include <set>

#include <wx/log.h>

namespace {
	struct factory {
		std::string name;
		std::string subtype;
		std::unique_ptr<SubtitlesProvider> (*create)(std::string const& subtype, agi::BackgroundRunner *br);
		bool (*available)(std::string *, bool) = nullptr;
	};

	void WarnFallbackOnce(std::string const& provider, std::string const& error) {
		static std::mutex mutex;
		static std::set<std::string> warned;
		std::lock_guard<std::mutex> lock(mutex);
		if (!warned.insert(provider).second) return;
		auto message = provider + " unavailable. Trying fallback subtitle renderers. (" + error + ")";
		LOG_W("subtitle/provider") << message;
		wxLogWarning("%s", to_wx(message));
	}

	// Metadata is inert. Discovery is reserved for renderer selection UI.
	std::vector<factory> const& factories() {
		static const std::vector<factory> list = {
			{"libass", "", libass::Create},
			{"libassmod", "", libassmod::Create, libassmod::IsAvailable},
			{"Mangetsu", "", mangetsu::Create, mangetsu::IsAvailable},
		};
		return list;
	}

}

std::vector<std::string> SubtitlesProviderFactory::GetClasses() {
	std::vector<std::string> names;
#ifdef WITH_CSRI
	for (auto const& subtype : csri::List()) names.push_back("CSRI/" + subtype);
#endif
	// Successful backend validation is cached for the process lifetime. Retry
	// failed validation when the user reopens selection UI (e.g. after installing a DLL).
	for (auto const& entry : factories())
		if (!entry.available || entry.available(nullptr, true)) names.push_back(entry.name);
	return names;
}

std::vector<std::string> SubtitlesProviderFactory::GetUnavailableClasses() {
	std::vector<std::string> unavailable;
	for (auto const& entry : factories()) {
		std::string error;
		if (entry.available && !entry.available(&error, false)) unavailable.push_back(entry.name + " unavailable: " + error);
	}
	return unavailable;
}

std::unique_ptr<SubtitlesProvider> SubtitlesProviderFactory::GetProvider(agi::BackgroundRunner *br) {
	auto preferred = OPT_GET("Subtitle/Provider")->GetString();
	std::string error;
	auto create = [&](factory const& entry) -> std::unique_ptr<SubtitlesProvider> {
		auto started = std::chrono::steady_clock::now();
		try {
			auto provider = entry.create(entry.subtype, br);
			LOG_I("startup/renderer") << entry.name << ": " << StartupMilliseconds(started) << " ms (provider initialization)";
			return provider;
		}
		catch (agi::UserCancelException const&) { throw; }
		catch (agi::Exception const& e) { error += entry.name + ": " + e.GetMessage() + "\n"; }
		catch (std::exception const& e) { error += entry.name + ": " + e.what() + "\n"; }
		catch (...) { error += entry.name + ": Unknown error\n"; }
		return nullptr;
	};
	// The selected factory is tried directly. No availability probes of other
	// renderers, DLL loads or CSRI enumeration are needed on the success path.
	for (auto const& entry : factories())
		if (entry.name == preferred)
			if (auto provider = create(entry)) return provider;
#ifdef WITH_CSRI
	if (preferred.compare(0, 5, "CSRI/") == 0)
		if (auto provider = create(factory{preferred, preferred.substr(5), csri::Create})) return provider;
#endif
	if (error.empty()) error = preferred + ": renderer is not registered in this build.\n";
	WarnFallbackOnce(preferred, error);
	// Custom backends have always fallen back to built-in libass. Preserve
	// the configured preference so an unavailable installation is visible.
	if (preferred == "libassmod" || preferred == "Mangetsu")
		if (auto provider = create(factories().front())) return provider;
	// Only a failed selection reaches fallback discovery, in its original order.
#ifdef WITH_CSRI
	for (auto const& subtype : csri::List()) {
		if ("CSRI/" + subtype == preferred) continue;
		if (auto provider = create(factory{"CSRI/" + subtype, subtype, csri::Create})) return provider;
	}
#endif
	for (auto const& entry : factories()) {
		if (entry.name == preferred || ((preferred == "libassmod" || preferred == "Mangetsu") && entry.name == "libass")) continue;
		if (auto provider = create(entry)) return provider;
	}
	throw error;
}
