// Copyright (c) 2007, Rodrigo Braz Monteiro
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

/// @file subtitles_provider_csri.cpp
/// @brief Wrapper for CSRI-based subtitle renderers
/// @ingroup subtitle_rendering
///

#ifdef WITH_CSRI
#include "subtitles_provider_csri.h"

#include "ass_attachment.h"
#include "ass_file.h"
#include "include/aegisub/subtitles_provider.h"
#include "subtitle_format_ass.h"
#include "video_frame.h"

#include <libaegisub/ass/uuencode.h>
#include <libaegisub/make_unique.h>

#include <algorithm>
#include <cctype>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <wx/image.h>
#include <wx/filename.h>
#include <wx/imagpng.h>
#include <wx/mstream.h>
#include <wx/strconv.h>

#ifdef WIN32
#define CSRIAPI
#endif

#include <ass/ass.h>
#include <csri/csri.h>

namespace {
// CSRI renderers are not required to be thread safe (and VSFilter very much
// is not)
std::mutex csri_mutex;

struct closer {
	void operator()(csri_inst *inst) { if (inst) csri_close(inst); }
};

constexpr const char *CSRI_EXT_LIBASSMOD_TAG_IMAGE_RGBA = "libassmod.tag-image.rgba";

struct csri_libass_tag_image_ext {
	int (*clear)(csri_inst *inst);
	int (*set_rgba)(csri_inst *inst, const char *path, int format,
		int width, int height, int stride, const unsigned char *rgba);
};

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

std::string strip_matching_quotes(std::string path) {
	path = trim_copy(std::move(path));
	if (path.size() >= 2) {
		bool quoted = (path.front() == '"' && path.back() == '"')
			|| (path.front() == '\'' && path.back() == '\'');
		if (quoted)
			path = path.substr(1, path.size() - 2);
	}
	return trim_copy(std::move(path));
}

std::string add_double_quotes(std::string const& path) {
	std::string quoted;
	quoted.reserve(path.size() + 2);
	quoted.push_back('"');
	quoted += path;
	quoted.push_back('"');
	return quoted;
}

std::vector<wxString> file_image_candidates(std::string const& path, wxString const& script_dir) {
	std::vector<wxString> candidates;
	wxString wxpath = wxString::FromUTF8(path.c_str());
	if (wxpath.empty() && !path.empty())
		wxpath = wxString(path.c_str(), wxConvLocal);
	if (wxpath.empty())
		return candidates;

	candidates.push_back(wxpath);

	wxFileName fname(wxpath);
	if (!fname.IsAbsolute() && !script_dir.empty()) {
		wxFileName resolved(fname);
		resolved.MakeAbsolute(script_dir);
		wxString absolute = resolved.GetFullPath();
		if (!absolute.empty() && absolute != wxpath)
			candidates.push_back(absolute);
	}

	return candidates;
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

bool decode_file_image(std::string const& path, wxString const& script_dir, TagImage *out) {
	if (!parse_tag_image_format(path, &out->format))
		return false;

	ensure_image_handlers();
	for (auto const& candidate : file_image_candidates(path, script_dir)) {
		wxImage image;
		if (!image.LoadFile(candidate, wxBITMAP_TYPE_ANY))
			continue;
		if (!decode_image_to_rgba(image, out))
			return false;

		out->key = path;
		out->basename_lower = to_lower_copy(path_basename(path));
		return true;
	}

	return false;
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

		if (j >= len)
			continue;

		size_t start = j;
		size_t end = j;
		if (data[j] == '"' || data[j] == '\'') {
			char quote = data[j++];
			start = j;
			while (j < len && data[j] != quote)
				++j;
			end = j;
		} else {
			while (j < len && data[j] != ',' && data[j] != ')')
				++j;
			end = j;
		}
		if (end <= start)
			continue;

		std::string path(data + start, data + end);
		path = strip_matching_quotes(path);
		if (!path.empty())
			paths.push_back(path);
	}
	return paths;
}
#endif

class CSRISubtitlesProvider final : public SubtitlesProvider {
	std::unique_ptr<csri_inst, closer> instance;
	csri_rend *renderer = nullptr;

#ifdef LIBASSMOD_FEATURE_TAG_IMAGE
	std::vector<TagImage> attachment_tag_images;
	wxString tag_image_script_dir;

	void PrepareSubtitles(AssFile *subs, int) override {
		if (!subs->Filename.empty())
			tag_image_script_dir = wxString(subs->Filename.parent_path().wstring().c_str());
		else
			tag_image_script_dir.clear();

		attachment_tag_images.clear();
		for (auto const& attachment : subs->Attachments) {
			if (attachment.Group() != AssEntryGroup::GRAPHIC)
				continue;

			TagImage image;
			if (decode_attachment_image(attachment, &image))
				attachment_tag_images.push_back(std::move(image));
		}
	}

	void RegisterTagImages(const char *data, size_t len) {
		if (!instance)
			return;

		auto *ext = static_cast<csri_libass_tag_image_ext *>(
			csri_query_ext(renderer, CSRI_EXT_LIBASSMOD_TAG_IMAGE_RGBA));
		if (!ext || !ext->clear || !ext->set_rgba)
			return;

		ext->clear(instance.get());

		std::unordered_set<std::string> registered_paths;
		auto register_key = [&](std::string const& key, ASS_TagImageFormat format,
			int width, int height, int stride, const unsigned char *rgba) {
			if (key.empty())
				return;
			if (registered_paths.find(key) != registered_paths.end())
				return;
			if (ext->set_rgba(instance.get(), key.c_str(), format,
				width, height, stride, rgba) >= 0) {
				registered_paths.insert(key);
			}
		};
		auto register_path_variants = [&](std::string const& key, ASS_TagImageFormat format,
			int width, int height, int stride, const unsigned char *rgba) {
			std::string clean = strip_matching_quotes(key);
			if (clean.empty())
				return;
			register_key(clean, format, width, height, stride, rgba);
			register_key(add_double_quotes(clean), format, width, height, stride, rgba);
		};

		std::unordered_map<std::string, const TagImage *> attachment_by_name;
		for (auto const& image : attachment_tag_images) {
			attachment_by_name.emplace(image.basename_lower, &image);
			register_path_variants(image.key, image.format,
				image.width, image.height, image.stride, image.rgba.data());
		}

		for (auto const& raw_path : collect_img_paths(data, len)) {
			std::string path = strip_matching_quotes(raw_path);
			if (path.empty())
				continue;

			ASS_TagImageFormat format;
			if (!parse_tag_image_format(path, &format))
				continue;

			std::string base = to_lower_copy(path_basename(path));
			auto attachment_it = attachment_by_name.find(base);
			if (attachment_it != attachment_by_name.end()) {
				auto const& image = *attachment_it->second;
				if (image.format != format)
					continue;
				register_path_variants(path, image.format,
					image.width, image.height, image.stride, image.rgba.data());
				continue;
			}

			TagImage file_image;
			if (!decode_file_image(path, tag_image_script_dir, &file_image))
				continue;
			register_path_variants(path, file_image.format,
				file_image.width, file_image.height, file_image.stride,
				file_image.rgba.data());
		}
	}
#endif

	void LoadSubtitles(const char *data, size_t len) override {
		std::lock_guard<std::mutex> lock(csri_mutex);
		instance.reset(csri_open_mem(renderer, data, len, nullptr));
#ifdef LIBASSMOD_FEATURE_TAG_IMAGE
		RegisterTagImages(data, len);
#endif
	}

public:
	CSRISubtitlesProvider(std::string subType);

	void DrawSubtitles(VideoFrame &dst, double time) override;
};

CSRISubtitlesProvider::CSRISubtitlesProvider(std::string type) {
	std::lock_guard<std::mutex> lock(csri_mutex);
	for (csri_rend *cur = csri_renderer_default(); cur; cur = csri_renderer_next(cur)) {
		if (type == csri_renderer_info(cur)->name) {
			renderer = cur;
			break;
		}
	}

	if (!renderer)
		throw agi::InternalError("CSRI renderer vanished between initial list and creation?");
}

void CSRISubtitlesProvider::DrawSubtitles(VideoFrame &dst, double time) {
	if (!instance) return;

	csri_frame frame;
	if (dst.flipped) {
		frame.planes[0] = dst.data.data() + (dst.height-1) * dst.width * 4;
		frame.strides[0] = -(signed)dst.width * 4;
	}
	else {
		frame.planes[0] = dst.data.data();
		frame.strides[0] = dst.width * 4;
	}
	frame.pixfmt = CSRI_F_BGR_;

	csri_fmt format = {
		frame.pixfmt,
		static_cast<unsigned>(dst.width),
		static_cast<unsigned>(dst.height)
	};

	std::lock_guard<std::mutex> lock(csri_mutex);
	if (!csri_request_fmt(instance.get(), &format))
		csri_render(instance.get(), &frame, time);
}
}

namespace csri {
std::vector<std::string> List() {
	std::vector<std::string> final;
	for (csri_rend *cur = csri_renderer_default(); cur; cur = csri_renderer_next(cur)) {
		std::string name(csri_renderer_info(cur)->name);
		if (name.find("aegisub") != name.npos)
			final.insert(final.begin(), name);
		else
			final.push_back(name);
	}
	return final;
}

std::unique_ptr<SubtitlesProvider> Create(std::string const& name, agi::BackgroundRunner *) {
	return agi::make_unique<CSRISubtitlesProvider>(name);
}
}
#endif // WITH_CSRI
