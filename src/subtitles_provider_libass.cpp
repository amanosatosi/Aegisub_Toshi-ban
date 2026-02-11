// Copyright (c) 2006-2007, Rodrigo Braz Monteiro, Evgeniy Stepanov
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//   * Redistributions of source code must retain the above copyright notice,
//     this list of conditions and the following disclaimer.
//   * Redistributions in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other materials provided with the distribution.
//   * Neither the name of the Aegisub Group nor the names of its contributors
//     may be used to endorse or promote products derived from this software
//     without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
// Aegisub Project http://www.aegisub.org/

/// @file subtitles_provider_libass.cpp
/// @brief libass-based subtitle renderer
/// @ingroup subtitle_rendering
///

#include "subtitles_provider_libass.h"

#include "ass_attachment.h"
#include "ass_file.h"
#include "compat.h"
#include "include/aegisub/subtitles_provider.h"
#include "video_frame.h"

#include <libaegisub/ass/uuencode.h>
#include <libaegisub/background_runner.h>
#include <libaegisub/dispatch.h>
#include <libaegisub/exception.h>
#include <libaegisub/log.h>
#include <libaegisub/make_unique.h>
#include <libaegisub/util.h>

#include <atomic>
#include <algorithm>
#include <boost/gil.hpp>
#include <cctype>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <wx/image.h>
#include <wx/imagpng.h>
#if wxUSE_LIBJPEG
#include <wx/imagjpeg.h>
#endif
#if wxUSE_WEBP
#include <wx/imagwebp.h>
#endif
#include <wx/intl.h>
#include <wx/mstream.h>
#include <wx/thread.h>

extern "C" {
#include <ass/ass.h>
}

namespace {
std::unique_ptr<agi::dispatch::Queue> cache_queue;
ASS_Library *library;

void msg_callback(int level, const char *fmt, va_list args, void *) {
	if (level >= 7) return;
	char buf[1024];
#ifdef _WIN32
	vsprintf_s(buf, sizeof(buf), fmt, args);
#else
	vsnprintf(buf, sizeof(buf), fmt, args);
#endif

	if (level < 2) // warning/error
		LOG_I("subtitle/provider/libass") << buf;
	else // verbose
		LOG_D("subtitle/provider/libass") << buf;
}

#ifdef LIBASSMOD_FEATURE_TAG_IMAGE
struct TagImage {
	std::string key;
	std::string basename_lower;
	ASS_TagImageFormat format = ASS_TAG_IMAGE_FORMAT_PNG;
	int width = 0;
	int height = 0;
	int stride = 0;
	std::vector<unsigned char> rgba;
};

std::string trim_copy(std::string str) {
	auto not_space = [](unsigned char c) { return !std::isspace(c); };
	auto begin = std::find_if(str.begin(), str.end(), not_space);
	if (begin == str.end())
		return {};
	auto end = std::find_if(str.rbegin(), str.rend(), not_space).base();
	return std::string(begin, end);
}

std::string to_lower_copy(std::string str) {
	std::transform(str.begin(), str.end(), str.begin(), [](unsigned char c) {
		return static_cast<char>(std::tolower(c));
	});
	return str;
}

std::string path_basename(std::string const& path) {
	size_t cut = path.find_last_of("/\\");
	if (cut == std::string::npos)
		return path;
	return path.substr(cut + 1);
}

bool parse_tag_image_format(std::string const& path, ASS_TagImageFormat *format) {
	std::string lower = to_lower_copy(path);
	if (lower.size() >= 4 && lower.compare(lower.size() - 4, 4, ".png") == 0) {
		*format = ASS_TAG_IMAGE_FORMAT_PNG;
		return true;
	}
	if (lower.size() >= 4 && lower.compare(lower.size() - 4, 4, ".jpg") == 0) {
		*format = ASS_TAG_IMAGE_FORMAT_JPEG;
		return true;
	}
	if (lower.size() >= 5 && lower.compare(lower.size() - 5, 5, ".jpeg") == 0) {
		*format = ASS_TAG_IMAGE_FORMAT_JPEG;
		return true;
	}
	if (lower.size() >= 5 && lower.compare(lower.size() - 5, 5, ".webp") == 0) {
		*format = ASS_TAG_IMAGE_FORMAT_WEBP;
		return true;
	}
	return false;
}

void ensure_image_handlers() {
	static std::once_flag handlers_once;
	std::call_once(handlers_once, [] {
		if (!wxImage::FindHandler(wxBITMAP_TYPE_PNG))
			wxImage::AddHandler(new wxPNGHandler);
#if wxUSE_LIBJPEG
		if (!wxImage::FindHandler(wxBITMAP_TYPE_JPEG))
			wxImage::AddHandler(new wxJPEGHandler);
#endif
#if wxUSE_WEBP
		if (!wxImage::FindHandler(wxBITMAP_TYPE_WEBP))
			wxImage::AddHandler(new wxWEBPHandler);
#endif
	});
}

bool decode_image_to_rgba(wxImage &image, TagImage *out) {
	if (!image.IsOk())
		return false;

	int width = image.GetWidth();
	int height = image.GetHeight();
	if (width <= 0 || height <= 0)
		return false;

	unsigned char *rgb = image.GetData();
	unsigned char *alpha = image.HasAlpha() ? image.GetAlpha() : nullptr;
	if (!rgb)
		return false;

	out->width = width;
	out->height = height;
	out->stride = width * 4;
	out->rgba.resize(static_cast<size_t>(out->stride) * static_cast<size_t>(height));

	for (int i = 0; i < width * height; ++i) {
		out->rgba[i * 4 + 0] = rgb[i * 3 + 0];
		out->rgba[i * 4 + 1] = rgb[i * 3 + 1];
		out->rgba[i * 4 + 2] = rgb[i * 3 + 2];
		out->rgba[i * 4 + 3] = alpha ? alpha[i] : 255;
	}

	return true;
}

bool decode_attachment_image(AssAttachment const& attachment, TagImage *out) {
	std::string const& entry = attachment.GetEntryData();
	size_t header_end = entry.find('\n');
	if (header_end == std::string::npos)
		return false;

	std::string header = trim_copy(entry.substr(0, header_end));
	if (header.size() < 9 || to_lower_copy(header.substr(0, 9)) != "filename:")
		return false;

	std::string filename = trim_copy(header.substr(9));
	if (filename.empty())
		return false;
	if (!parse_tag_image_format(filename, &out->format))
		return false;

	auto decoded = agi::ass::UUDecode(entry.c_str() + header_end + 1,
		entry.c_str() + entry.size());
	if (decoded.empty())
		return false;

	ensure_image_handlers();
	wxMemoryInputStream stream(decoded.data(), decoded.size());
	wxImage image;
	if (!image.LoadFile(stream, wxBITMAP_TYPE_ANY))
		return false;
	if (!decode_image_to_rgba(image, out))
		return false;

	out->key = filename;
	out->basename_lower = to_lower_copy(path_basename(filename));
	return true;
}

bool decode_file_image(std::string const& path, TagImage *out) {
	if (!parse_tag_image_format(path, &out->format))
		return false;

	wxString wxpath = wxString::FromUTF8(path.c_str());
	if (wxpath.empty())
		return false;

	ensure_image_handlers();
	wxImage image;
	if (!image.LoadFile(wxpath, wxBITMAP_TYPE_ANY))
		return false;
	if (!decode_image_to_rgba(image, out))
		return false;

	out->key = path;
	out->basename_lower = to_lower_copy(path_basename(path));
	return true;
}

std::vector<std::string> collect_img_paths(const char *data, size_t len) {
	std::vector<std::string> paths;
	for (size_t i = 0; i + 4 < len; ++i) {
		if (data[i] != '\\')
			continue;

		size_t j = i + 1;
		if (j < len && data[j] >= '1' && data[j] <= '4')
			++j;
		if (j + 2 >= len)
			continue;
		if (data[j] != 'i' || data[j + 1] != 'm' || data[j + 2] != 'g')
			continue;

		j += 3;
		while (j < len && (data[j] == ' ' || data[j] == '\t'))
			++j;
		if (j >= len || data[j] != '(')
			continue;

		++j;
		while (j < len && (data[j] == ' ' || data[j] == '\t'))
			++j;

		size_t start = j;
		while (j < len && data[j] != ',' && data[j] != ')')
			++j;
		if (j <= start)
			continue;

		std::string path(data + start, data + j);
		path = trim_copy(path);
		if (path.size() >= 2) {
			bool quoted = (path.front() == '"' && path.back() == '"')
				|| (path.front() == '\'' && path.back() == '\'');
			if (quoted)
				path = path.substr(1, path.size() - 2);
		}
		path = trim_copy(path);
		if (!path.empty())
			paths.push_back(path);
	}
	return paths;
}
#endif

// Stuff used on the cache thread, owned by a shared_ptr in case the provider
// gets deleted before the cache finishing updating
struct cache_thread_shared {
	ASS_Renderer *renderer = nullptr;
	std::atomic<bool> ready{false};
	~cache_thread_shared() { if (renderer) ass_renderer_done(renderer); }
};

class LibassSubtitlesProvider final : public SubtitlesProvider {
	agi::BackgroundRunner *br;
	std::shared_ptr<cache_thread_shared> shared;
	ASS_Track* ass_track = nullptr;

#ifdef LIBASSMOD_FEATURE_TAG_IMAGE
	std::vector<TagImage> attachment_tag_images;
	std::vector<std::string> tag_image_paths;
	bool tag_images_dirty = false;

	void PrepareSubtitles(AssFile *subs, int) override {
		attachment_tag_images.clear();
		for (auto const& attachment : subs->Attachments) {
			if (attachment.Group() != AssEntryGroup::GRAPHIC)
				continue;

			TagImage image;
			if (decode_attachment_image(attachment, &image))
				attachment_tag_images.push_back(std::move(image));
		}
	}

	void RegisterTagImages() {
		if (!tag_images_dirty)
			return;

		ASS_Renderer *ass_renderer = renderer();
		if (!ass_renderer)
			return;

		ass_clear_tag_images(ass_renderer);

		std::unordered_set<std::string> registered_paths;
		std::unordered_map<std::string, const TagImage *> attachment_by_name;
		for (auto const& image : attachment_tag_images) {
			attachment_by_name.emplace(image.basename_lower, &image);
			if (ass_set_tag_image_rgba(ass_renderer, image.key.c_str(), image.format,
				image.width, image.height, image.stride, image.rgba.data()) >= 0) {
				registered_paths.insert(image.key);
			}
		}

		for (auto const& raw_path : tag_image_paths) {
			if (registered_paths.find(raw_path) != registered_paths.end())
				continue;

			ASS_TagImageFormat format;
			if (!parse_tag_image_format(raw_path, &format))
				continue;

			std::string base = to_lower_copy(path_basename(raw_path));
			auto attachment_it = attachment_by_name.find(base);
			if (attachment_it != attachment_by_name.end()) {
				auto const& image = *attachment_it->second;
				if (image.format != format)
					continue;
				if (ass_set_tag_image_rgba(ass_renderer, raw_path.c_str(), image.format,
					image.width, image.height, image.stride, image.rgba.data()) >= 0) {
					registered_paths.insert(raw_path);
				}
				continue;
			}

			TagImage file_image;
			if (!decode_file_image(raw_path, &file_image))
				continue;
			if (ass_set_tag_image_rgba(ass_renderer, raw_path.c_str(), file_image.format,
				file_image.width, file_image.height, file_image.stride,
				file_image.rgba.data()) >= 0) {
				registered_paths.insert(raw_path);
			}
		}

		tag_images_dirty = false;
	}
#endif

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
	LibassSubtitlesProvider(agi::BackgroundRunner *br);
	~LibassSubtitlesProvider();

	void LoadSubtitles(const char *data, size_t len) override {
		if (ass_track) ass_free_track(ass_track);
		ass_track = ass_read_memory(library, const_cast<char *>(data), len, nullptr);
		if (!ass_track) throw agi::InternalError("libass failed to load subtitles.");
#ifdef LIBASSMOD_FEATURE_TAG_IMAGE
		tag_image_paths = collect_img_paths(data, len);
		tag_images_dirty = true;
#endif
	}

	void DrawSubtitles(VideoFrame &dst, double time) override;

	void Reinitialize() override {
		// No need to reinit if we're not even done with the initial init
		if (!shared->ready)
			return;

		ass_renderer_done(shared->renderer);
		shared->renderer = ass_renderer_init(library);
		ass_set_font_scale(shared->renderer, 1.);
		ass_set_fonts(shared->renderer, nullptr, "Sans", 1, nullptr, true);
#ifdef LIBASSMOD_FEATURE_TAG_IMAGE
		tag_images_dirty = true;
#endif
	}
};

LibassSubtitlesProvider::LibassSubtitlesProvider(agi::BackgroundRunner *br)
: br(br)
, shared(std::make_shared<cache_thread_shared>())
{
	auto state = shared;
	cache_queue->Async([state] {
		auto ass_renderer = ass_renderer_init(library);
		if (ass_renderer) {
			ass_set_font_scale(ass_renderer, 1.);
			ass_set_fonts(ass_renderer, nullptr, "Sans", 1, nullptr, true);
		}
		state->renderer = ass_renderer;
		state->ready = true;
	});
}

LibassSubtitlesProvider::~LibassSubtitlesProvider() {
	if (ass_track) ass_free_track(ass_track);
}

#define _r(c) ((c)>>24)
#define _g(c) (((c)>>16)&0xFF)
#define _b(c) (((c)>>8)&0xFF)
#define _a(c) ((c)&0xFF)

void LibassSubtitlesProvider::DrawSubtitles(VideoFrame &frame,double time) {
	ASS_Renderer *ass_renderer = renderer();
	if (!ass_renderer || !ass_track)
		return;
#ifdef LIBASSMOD_FEATURE_TAG_IMAGE
	RegisterTagImages();
#endif
	ass_set_frame_size(ass_renderer, frame.width, frame.height);
	// Note: this relies on Aegisub always rendering at video storage res
	ass_set_storage_size(ass_renderer, frame.width, frame.height);

	int detect_change = 0;
	ASS_RenderResult render_result = ass_render_frame_auto(ass_renderer, ass_track, int(time * 1000), &detect_change);

	// libass now returns either premultiplied RGBA images or the legacy alpha-masked monochrome list.
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
		for (ASS_Image* img = render_result.imgs; img; img = img->next) {
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
		ass_free_images_rgba(render_result.imgs_rgba);
}
}

namespace libass {
std::unique_ptr<SubtitlesProvider> Create(std::string const&, agi::BackgroundRunner *br) {
	return agi::make_unique<LibassSubtitlesProvider>(br);
}

void CacheFonts() {
	// Initialize the cache worker thread
	cache_queue = agi::dispatch::Create();

	// Initialize libass
	library = ass_library_init();
	ass_set_message_cb(library, msg_callback, nullptr);

	// Initialize a renderer to force fontconfig to update its cache
	cache_queue->Async([] {
		auto ass_renderer = ass_renderer_init(library);
		ass_set_fonts(ass_renderer, nullptr, "Sans", 1, nullptr, true);
		ass_renderer_done(ass_renderer);
	});
}
}
