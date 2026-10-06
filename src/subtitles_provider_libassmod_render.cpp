// Copyright (c) 2026, Aegisub Project
// Distributed under the ISC license; see subtitles_provider_libassmod.cpp.
#include "subtitles_provider_libassmod_render.h"
#include "video_frame.h"

#include <libaegisub/exception.h>

#include <algorithm>
#include <boost/gil.hpp>
#include <limits>
#include <memory>

namespace custom_subtitles {
namespace {
struct Surface {
	uint8_t *top;
	int width, height, stride;
};

Surface GetSurface(VideoFrame& frame) {
	if (!frame.width || !frame.height || frame.width > static_cast<size_t>(std::numeric_limits<int>::max() / 4) ||
		frame.height > static_cast<size_t>(std::numeric_limits<int>::max()) ||
		frame.pitch < frame.width * 4 || frame.pitch > static_cast<size_t>(std::numeric_limits<int>::max()) ||
		frame.height > frame.data.size() / frame.pitch)
		throw agi::InternalError("Invalid custom subtitle destination frame.");
	Surface dst{frame.data.data(), static_cast<int>(frame.width), static_cast<int>(frame.height), static_cast<int>(frame.pitch)};
	if (frame.flipped) {
		dst.top += (frame.height - 1) * frame.pitch;
		dst.stride = -dst.stride;
	}
	return dst;
}

struct Region {
	int x, y, w, h, source_x, source_y;
};

template<class Image>
Region VisibleRegion(Image const& img, Surface const& dst) {
	int x = static_cast<int>(std::clamp<int64_t>(img.dst_x, 0, dst.width));
	int y = static_cast<int>(std::clamp<int64_t>(img.dst_y, 0, dst.height));
	int right = static_cast<int>(std::clamp<int64_t>(static_cast<int64_t>(img.dst_x) + img.w, 0, dst.width));
	int bottom = static_cast<int>(std::clamp<int64_t>(static_cast<int64_t>(img.dst_y) + img.h, 0, dst.height));
	return {x, y, right - x, bottom - y,
		static_cast<int>(std::clamp<int64_t>(static_cast<int64_t>(x) - img.dst_x, 0, std::numeric_limits<int>::max())),
		static_cast<int>(std::clamp<int64_t>(static_cast<int64_t>(y) - img.dst_y, 0, std::numeric_limits<int>::max()))};
}

void BlendRgba(ASS_ImageRGBA *images, Surface const& surface) {
	using namespace boost::gil;
	auto dst = interleaved_view(surface.width, surface.height, reinterpret_cast<bgra8_pixel_t *>(surface.top), surface.stride);
	for (auto img = images; img; img = img->next) {
		auto r = VisibleRegion(*img, surface);
		if (r.w <= 0 || r.h <= 0) continue;
		auto src = interleaved_view(img->w, img->h, reinterpret_cast<rgba8_pixel_t *>(img->rgba), img->stride);
		auto srcview = subimage_view(src, r.source_x, r.source_y, r.w, r.h);
		auto dstview = subimage_view(dst, r.x, r.y, r.w, r.h);
		transform_pixels(dstview, srcview, dstview, [](bgra8_pixel_t frame_px, rgba8_pixel_t src_px) -> bgra8_pixel_t {
			unsigned inv_alpha = 255 - src_px[3];
			return bgra8_pixel_t(
				static_cast<unsigned char>(src_px[2] + frame_px[0] * inv_alpha / 255),
				static_cast<unsigned char>(src_px[1] + frame_px[1] * inv_alpha / 255),
				static_cast<unsigned char>(src_px[0] + frame_px[2] * inv_alpha / 255), 0);
		});
	}
}

void BlendLegacy(ASS_Image *images, Surface const& surface) {
	using namespace boost::gil;
	auto dst = interleaved_view(surface.width, surface.height, reinterpret_cast<bgra8_pixel_t *>(surface.top), surface.stride);
	for (auto img = images; img; img = img->next) {
		auto region = VisibleRegion(*img, surface);
		if (region.w <= 0 || region.h <= 0) continue;
		unsigned opacity = 255 - (img->color & 0xFF);
		unsigned r = img->color >> 24, g = (img->color >> 16) & 0xFF, b = (img->color >> 8) & 0xFF;
		auto src = interleaved_view(img->w, img->h, reinterpret_cast<gray8_pixel_t *>(img->bitmap), img->stride);
		auto srcview = subimage_view(src, region.source_x, region.source_y, region.w, region.h);
		auto dstview = subimage_view(dst, region.x, region.y, region.w, region.h);
		transform_pixels(dstview, srcview, dstview, [=](bgra8_pixel_t frame_px, gray8_pixel_t src_px) -> bgra8_pixel_t {
			unsigned k = static_cast<unsigned>(src_px) * opacity / 255, ck = 255 - k;
			return bgra8_pixel_t(static_cast<unsigned char>((k * b + ck * frame_px[0]) / 255),
				static_cast<unsigned char>((k * g + ck * frame_px[1]) / 255),
				static_cast<unsigned char>((k * r + ck * frame_px[2]) / 255), 0);
		});
	}
}
}

RenderOutcome DrawFrame(RenderApi const& api, ASS_Renderer *renderer,
	ASS_Track *track, long long time_ms, VideoFrame& frame) {
	auto dst = GetSurface(frame);
	int detect_change = 0;
	ASS_ImageRGBA *rgba = nullptr;
	ASS_Image *legacy = nullptr;
	bool use_rgba = false;
	if (api.HasDirectRgba()) {
		rgba = api.ass_render_frame_rgba(renderer, track, time_ms, &detect_change);
		use_rgba = true;
	}
	else if (api.ass_render_frame_auto && api.ass_free_images_rgba) {
		auto result = api.ass_render_frame_auto(renderer, track, time_ms, &detect_change);
		rgba = result.imgs_rgba;
		legacy = result.imgs;
		use_rgba = result.use_rgba != 0;
	}
	else if (api.ass_render_frame) {
		legacy = api.ass_render_frame(renderer, track, time_ms, &detect_change);
	}
	else throw agi::InternalError("Custom subtitle renderer has no usable frame API.");

	std::unique_ptr<ASS_ImageRGBA, decltype(api.ass_free_images_rgba)> owned(rgba, api.ass_free_images_rgba);
	// detect_change compares subtitle output, not the destination video pixels.
	// Aegisub supplies a fresh video frame (also black/white alpha readouts), so
	// unchanged subtitles must still be composited on every DrawSubtitles call.
	if (use_rgba) {
		if (!rgba) return {RenderPath::Empty, detect_change};
		if (api.ass_composite_images_bgra &&
			api.ass_composite_images_bgra(rgba, dst.top, dst.width, dst.height, dst.stride) == 0)
			return {RenderPath::NativeRgba, detect_change};
		// Mangetsu preflights before modifying dst on failure. Reuse the same
		// list, in painter order, without another frame-generation call.
		BlendRgba(rgba, dst);
		return {RenderPath::FallbackRgba, detect_change};
	}
	if (!legacy) return {RenderPath::Empty, detect_change};
	BlendLegacy(legacy, dst);
	return {RenderPath::Legacy, detect_change};
}
}
