// Copyright (c) 2026, Aegisub Project
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS; WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
// Aegisub Project http://www.aegisub.org/

/// @file subtitles_provider_libassmod.cpp
/// @brief libassmod-based subtitle renderer
/// @ingroup subtitle_rendering
///

#include "subtitles_provider_libassmod.h"

#include "compat.h"
#include "include/aegisub/subtitles_provider.h"
#include "video_frame.h"

#include <libaegisub/background_runner.h>
#include <libaegisub/dispatch.h>
#include <libaegisub/exception.h>
#include <libaegisub/log.h>
#include <libaegisub/make_unique.h>
#include <libaegisub/util.h>

#include <atomic>
#include <boost/gil.hpp>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

#include <wx/intl.h>
#include <wx/thread.h>

#ifndef _WIN32
#include <dlfcn.h>
#endif
#ifdef _WIN32
#include <windows.h>
#endif

extern "C" {
#include <ass/ass.h>
}

#ifndef LIBASSMOD_FEATURE_RGBA
typedef struct ass_image_rgba {
	int w, h;
	int stride;
	uint8_t *rgba;
	int dst_x, dst_y;
	int type;
	struct ass_image_rgba *next;
} ASS_ImageRGBA;

typedef struct ass_render_result {
	ASS_Image *imgs;
	ASS_ImageRGBA *imgs_rgba;
	int use_rgba;
} ASS_RenderResult;
#endif

namespace {
#ifndef _WIN32
#ifdef __APPLE__
#define DLOPEN_FLAGS (RTLD_LAZY | RTLD_LOCAL)
#else
#define DLOPEN_FLAGS (RTLD_LAZY | RTLD_LOCAL | RTLD_DEEPBIND)
#endif
#endif

#ifdef _WIN32
using LibassModHandle = HMODULE;
#else
using LibassModHandle = void *;
#endif

using AssLibraryInitFunc = ASS_Library *(*)(void);
using AssLibraryDoneFunc = void (*)(ASS_Library *);
using AssSetMessageCbFunc = void (*)(ASS_Library *, void (*)(int, const char *, va_list, void *), void *);
using AssRendererInitFunc = ASS_Renderer *(*)(ASS_Library *);
using AssRendererDoneFunc = void (*)(ASS_Renderer *);
using AssSetFontScaleFunc = void (*)(ASS_Renderer *, double);
using AssSetFontsFunc = void (*)(ASS_Renderer *, const char *, const char *, int, const char *, int);
using AssReadMemoryFunc = ASS_Track *(*)(ASS_Library *, char *, size_t, const char *);
using AssFreeTrackFunc = void (*)(ASS_Track *);
using AssSetFrameSizeFunc = void (*)(ASS_Renderer *, int, int);
using AssSetStorageSizeFunc = void (*)(ASS_Renderer *, int, int);
using AssRenderFrameAutoFunc = ASS_RenderResult (*)(ASS_Renderer *, ASS_Track *, long long, int *);
using AssFreeImagesRGBAFunc = void (*)(ASS_ImageRGBA *);

struct LibassModApi {
	LibassModHandle handle = nullptr;
	AssLibraryInitFunc ass_library_init = nullptr;
	AssLibraryDoneFunc ass_library_done = nullptr;
	AssSetMessageCbFunc ass_set_message_cb = nullptr;
	AssRendererInitFunc ass_renderer_init = nullptr;
	AssRendererDoneFunc ass_renderer_done = nullptr;
	AssSetFontScaleFunc ass_set_font_scale = nullptr;
	AssSetFontsFunc ass_set_fonts = nullptr;
	AssReadMemoryFunc ass_read_memory = nullptr;
	AssFreeTrackFunc ass_free_track = nullptr;
	AssSetFrameSizeFunc ass_set_frame_size = nullptr;
	AssSetStorageSizeFunc ass_set_storage_size = nullptr;
	AssRenderFrameAutoFunc ass_render_frame_auto = nullptr;
	AssFreeImagesRGBAFunc ass_free_images_rgba = nullptr;
};

LibassModApi api;
bool api_loaded = false;
std::string api_error;
std::once_flag api_once;
ASS_Library *library = nullptr;
std::unique_ptr<agi::dispatch::Queue> cache_queue;
std::once_flag cache_queue_once;

void msg_callback(int level, const char *fmt, va_list args, void *) {
	if (level >= 7) return;
	char buf[1024];
#ifdef _WIN32
	vsprintf_s(buf, sizeof(buf), fmt, args);
#else
	vsnprintf(buf, sizeof(buf), fmt, args);
#endif

	if (level < 2) // warning/error
		LOG_I("subtitle/provider/libassmod") << buf;
	else // verbose
		LOG_D("subtitle/provider/libassmod") << buf;
}

void CloseLibassModHandle() {
#ifdef _WIN32
	if (api.handle) {
		FreeLibrary(api.handle);
		api.handle = nullptr;
	}
#else
	if (api.handle) {
		dlclose(api.handle);
		api.handle = nullptr;
	}
#endif
}

template <typename T>
bool LoadSymbol(LibassModHandle handle, const char *name, T &out, std::string &error) {
#ifdef _WIN32
	auto sym = reinterpret_cast<T>(GetProcAddress(handle, name));
#else
	auto sym = reinterpret_cast<T>(dlsym(handle, name));
#endif
	if (!sym) {
		error = std::string("Missing libassmod symbol: ") + name;
		return false;
	}
	out = sym;
	return true;
}

bool LoadLibassModApi(std::string &error) {
#ifdef _WIN32
	static const wchar_t *kLibassmodNames[] = { L"libassmod.dll", L"assmod.dll", L"ass.dll", L"libass.dll" };
	for (auto name : kLibassmodNames) {
		api.handle = LoadLibraryW(name);
		if (api.handle)
			break;
	}
	if (!api.handle) {
		error = "Could not load libassmod (tried libassmod.dll, assmod.dll, ass.dll, libass.dll).";
		return false;
	}
#else
#ifdef __APPLE__
	static const char *kLibassmodNames[] = { "libassmod.dylib", "libassmod.so", "libass.dylib", "libass.so" };
#else
	static const char *kLibassmodNames[] = { "libassmod.so", "libass.so" };
#endif
	for (auto name : kLibassmodNames) {
		api.handle = dlopen(name, DLOPEN_FLAGS);
		if (api.handle)
			break;
	}
	if (!api.handle) {
		error = "Could not load libassmod (tried libassmod and libass shared library names).";
		return false;
	}
#endif

	if (!LoadSymbol(api.handle, "ass_library_init", api.ass_library_init, error) ||
		!LoadSymbol(api.handle, "ass_library_done", api.ass_library_done, error) ||
		!LoadSymbol(api.handle, "ass_set_message_cb", api.ass_set_message_cb, error) ||
		!LoadSymbol(api.handle, "ass_renderer_init", api.ass_renderer_init, error) ||
		!LoadSymbol(api.handle, "ass_renderer_done", api.ass_renderer_done, error) ||
		!LoadSymbol(api.handle, "ass_set_font_scale", api.ass_set_font_scale, error) ||
		!LoadSymbol(api.handle, "ass_set_fonts", api.ass_set_fonts, error) ||
		!LoadSymbol(api.handle, "ass_read_memory", api.ass_read_memory, error) ||
		!LoadSymbol(api.handle, "ass_free_track", api.ass_free_track, error) ||
		!LoadSymbol(api.handle, "ass_set_frame_size", api.ass_set_frame_size, error) ||
		!LoadSymbol(api.handle, "ass_set_storage_size", api.ass_set_storage_size, error) ||
		!LoadSymbol(api.handle, "ass_render_frame_auto", api.ass_render_frame_auto, error) ||
		!LoadSymbol(api.handle, "ass_free_images_rgba", api.ass_free_images_rgba, error)) {
		CloseLibassModHandle();
		return false;
	}

	library = api.ass_library_init();
	if (!library) {
		error = "libassmod initialization failed.";
		CloseLibassModHandle();
		return false;
	}

	api.ass_set_message_cb(library, msg_callback, nullptr);
	return true;
}

bool EnsureLibassMod(std::string *error) {
	std::call_once(api_once, [] {
		api_loaded = LoadLibassModApi(api_error);
	});
	if (!api_loaded && error)
		*error = api_error;
	return api_loaded;
}

void EnsureCacheQueue() {
	std::call_once(cache_queue_once, [] {
		cache_queue = agi::dispatch::Create();
	});
}

// Stuff used on the cache thread, owned by a shared_ptr in case the provider
// gets deleted before the cache finishing updating
struct cache_thread_shared {
	ASS_Renderer *renderer = nullptr;
	std::atomic<bool> ready{false};
	~cache_thread_shared() {
		if (renderer && api.ass_renderer_done)
			api.ass_renderer_done(renderer);
	}
};

class LibassModSubtitlesProvider final : public SubtitlesProvider {
	agi::BackgroundRunner *br;
	std::shared_ptr<cache_thread_shared> shared;
	ASS_Track *ass_track = nullptr;

	ASS_Renderer *renderer() {
		if (shared->ready)
			return shared->renderer;

		auto block = [&] {
			if (shared->ready)
				return;
			agi::util::sleep_for(250);
			if (shared->ready)
				return;
			br->Run([=](agi::ProgressSink *ps) {
				ps->SetTitle(from_wx(_("Updating font index")));
				ps->SetMessage(from_wx(_("This may take several minutes")));
				ps->SetIndeterminate();
				while (!shared->ready && !ps->IsCancelled())
					agi::util::sleep_for(250);
			});
		};

		if (wxThread::IsMain())
			block();
		else
			agi::dispatch::Main().Sync(block);
		return shared->renderer;
	}

public:
	LibassModSubtitlesProvider(agi::BackgroundRunner *br);
	~LibassModSubtitlesProvider();

	void LoadSubtitles(const char *data, size_t len) override {
		if (ass_track) api.ass_free_track(ass_track);
		ass_track = api.ass_read_memory(library, const_cast<char *>(data), len, nullptr);
		if (!ass_track) throw agi::InternalError("libassmod failed to load subtitles.");
	}

	void DrawSubtitles(VideoFrame &dst, double time) override;

	void Reinitialize() override {
		// No need to reinit if we're not even done with the initial init
		if (!shared->ready)
			return;

		api.ass_renderer_done(shared->renderer);
		shared->renderer = api.ass_renderer_init(library);
		api.ass_set_font_scale(shared->renderer, 1.);
		api.ass_set_fonts(shared->renderer, nullptr, "Sans", 1, nullptr, true);
	}
};

LibassModSubtitlesProvider::LibassModSubtitlesProvider(agi::BackgroundRunner *br)
: br(br)
, shared(std::make_shared<cache_thread_shared>())
{
	std::string error;
	if (!EnsureLibassMod(&error))
		throw agi::InternalError("libassmod unavailable: " + error);

	EnsureCacheQueue();
	auto state = shared;
	cache_queue->Async([state] {
		auto ass_renderer = api.ass_renderer_init(library);
		if (ass_renderer) {
			api.ass_set_font_scale(ass_renderer, 1.);
			api.ass_set_fonts(ass_renderer, nullptr, "Sans", 1, nullptr, true);
		}
		state->renderer = ass_renderer;
		state->ready = true;
	});
}

LibassModSubtitlesProvider::~LibassModSubtitlesProvider() {
	if (ass_track) api.ass_free_track(ass_track);
}

#define _r(c) ((c)>>24)
#define _g(c) (((c)>>16)&0xFF)
#define _b(c) (((c)>>8)&0xFF)
#define _a(c) ((c)&0xFF)

void LibassModSubtitlesProvider::DrawSubtitles(VideoFrame &frame, double time) {
	api.ass_set_frame_size(renderer(), frame.width, frame.height);
	// Note: this relies on Aegisub always rendering at video storage res
	api.ass_set_storage_size(renderer(), frame.width, frame.height);

	int detect_change = 0;
	ASS_RenderResult render_result = api.ass_render_frame_auto(renderer(), ass_track, int(time * 1000), &detect_change);

	// libassmod returns either premultiplied RGBA images or the legacy alpha-masked monochrome list.
	// Blend whichever list is preferred by the renderer into the frame.

	using namespace boost::gil;
	auto dst = interleaved_view(frame.width, frame.height, (bgra8_pixel_t*)frame.data.data(), frame.width * 4);
	if (frame.flipped)
		dst = flipped_up_down_view(dst);

	if (render_result.use_rgba && render_result.imgs_rgba) {
		for (ASS_ImageRGBA *img = render_result.imgs_rgba; img; img = img->next) {
			auto srcview = interleaved_view(img->w, img->h, (rgba8_pixel_t*)img->rgba, img->stride);
			auto dstview = subimage_view(dst, img->dst_x, img->dst_y, img->w, img->h);

			transform_pixels(dstview, srcview, dstview, [](const bgra8_pixel_t frame_px, const rgba8_pixel_t src_px) -> bgra8_pixel_t {
				unsigned int alpha = src_px[3];
				unsigned int inv_alpha = 255 - alpha;

				bgra8_pixel_t ret;
				ret[0] = static_cast<unsigned char>(src_px[2] + (frame_px[0] * inv_alpha) / 255);
				ret[1] = static_cast<unsigned char>(src_px[1] + (frame_px[1] * inv_alpha) / 255);
				ret[2] = static_cast<unsigned char>(src_px[0] + (frame_px[2] * inv_alpha) / 255);
				ret[3] = 0;
				return ret;
			});
		}
	}
	else {
		for (ASS_Image *img = render_result.imgs; img; img = img->next) {
			unsigned int opacity = 255 - ((unsigned int)_a(img->color));
			unsigned int r = (unsigned int)_r(img->color);
			unsigned int g = (unsigned int)_g(img->color);
			unsigned int b = (unsigned int)_b(img->color);

			auto srcview = interleaved_view(img->w, img->h, (gray8_pixel_t*)img->bitmap, img->stride);
			auto dstview = subimage_view(dst, img->dst_x, img->dst_y, img->w, img->h);

			transform_pixels(dstview, srcview, dstview, [=](const bgra8_pixel_t frame_px, const gray8_pixel_t src_px) -> bgra8_pixel_t {
				unsigned int k = ((unsigned)src_px) * opacity / 255;
				unsigned int ck = 255 - k;

				bgra8_pixel_t ret;
				ret[0] = (k * b + ck * frame_px[0]) / 255;
				ret[1] = (k * g + ck * frame_px[1]) / 255;
				ret[2] = (k * r + ck * frame_px[2]) / 255;
				ret[3] = 0;
				return ret;
			});
		}
	}

	if (render_result.imgs_rgba)
		api.ass_free_images_rgba(render_result.imgs_rgba);
}
}

namespace libassmod {
std::unique_ptr<SubtitlesProvider> Create(std::string const&, agi::BackgroundRunner *br) {
	return agi::make_unique<LibassModSubtitlesProvider>(br);
}

void CacheFonts() {
	std::string error;
	if (!EnsureLibassMod(&error)) {
		LOG_I("subtitle/provider/libassmod") << "libassmod unavailable: " << error;
		return;
	}

	EnsureCacheQueue();

	cache_queue->Async([] {
		auto ass_renderer = api.ass_renderer_init(library);
		if (!ass_renderer)
			return;
		api.ass_set_fonts(ass_renderer, nullptr, "Sans", 1, nullptr, true);
		api.ass_renderer_done(ass_renderer);
	});
}
}
