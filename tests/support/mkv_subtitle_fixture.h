// Copyright (c) 2026, Aegisub Project
#pragma once

#include "ass_attachment.h"
#include "ass_dialogue.h"
#include "ass_file.h"
#include "ass_info.h"
#include "ass_parser.h"
#include "ass_style.h"
#include "include/aegisub/subtitles_provider.h"
#include "mkv_subtitle_packet.h"

#include <stdexcept>

namespace mkv_test {
inline auto ImportPacket(std::string const& packet, bool srt = false) {
	auto begin = packet.data();
	auto end = matroska::TrimTextPacketEnd(begin, begin + packet.size());
	return matroska::ParseTextPacket(begin, end, srt, agi::Time(1000), agi::Time(4000), 0);
}

class CaptureProvider final : public SubtitlesProvider {
	void LoadSubtitles(const char *data, size_t len) override { script.assign(data, len); }
public:
	using SubtitlesProvider::LoadSubtitles;
	std::string script;
	void DrawSubtitles(VideoFrame&, double) override { }
};

inline std::string ImportedScript(size_t padding) {
	AssFile file;
	AssParser parser(&file, 1);
	parser.AddLine("[Script Info]");
	parser.AddLine("ScriptType: v4.00+");
	parser.AddLine("PlayResX: 320");
	parser.AddLine("PlayResY: 180");
	parser.AddLine("[V4+ Styles]");
	parser.AddLine("Style: Default,Sans,22,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,0,0,7,10,10,10,1");
	parser.AddLine("[Events]");
	for (auto text : {"Matroska dialogue", "{\\p1\\an7\\pos(20,20)}m 0 0 l 40 0 40 20 0 20"}) {
		std::string packet = "0,0,Default,,0,0,0,,";
		packet += text;
		packet.append(padding, '\0');
		auto imported = ImportPacket(packet);
		if (!imported) throw std::runtime_error("fixture packet was skipped");
		parser.AddLine(imported->second);
	}
	CaptureProvider provider;
	provider.LoadSubtitles(&file);
	return provider.script;
}
}
