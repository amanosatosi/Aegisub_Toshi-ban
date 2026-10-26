// Copyright (c) 2026, Aegisub Project
// Distributed under the ISC license; see subtitles_provider_libassmod.cpp.
#pragma once

#include <cstdint>

extern "C" {
#include <ass/ass.h>
}

// The application links ordinary libass; custom extensions are resolved only
// in the selected backend's DLL. Keep the public ABI layout unchanged.
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

struct VideoFrame;

namespace custom_subtitles {
struct RenderApi {
	ASS_ImageRGBA *(*ass_render_frame_rgba)(ASS_Renderer *, ASS_Track *, long long, int *) = nullptr;
	void (*ass_free_images_rgba)(ASS_ImageRGBA *) = nullptr;
	int (*ass_composite_images_bgra)(ASS_ImageRGBA *, uint8_t *, int, int, int) = nullptr;
	ASS_RenderResult (*ass_render_frame_auto)(ASS_Renderer *, ASS_Track *, long long, int *) = nullptr;
	ASS_Image *(*ass_render_frame)(ASS_Renderer *, ASS_Track *, long long, int *) = nullptr;

	bool HasDirectRgba() const { return ass_render_frame_rgba && ass_free_images_rgba; }
	bool CanRender() const { return HasDirectRgba() || (ass_render_frame_auto && ass_free_images_rgba) || ass_render_frame; }
};

enum class RenderPath { Empty, NativeRgba, FallbackRgba, Legacy };
struct RenderOutcome {
	RenderPath path;
	int detect_change;
};

// Exactly one frame-generation call, regardless of compositor availability or
// empty output. RGBA ownership stays scoped to this call; legacy lists are borrowed.
RenderOutcome DrawFrame(RenderApi const& api, ASS_Renderer *renderer,
	ASS_Track *track, long long time_ms, VideoFrame& frame);
}
