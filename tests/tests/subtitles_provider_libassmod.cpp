// Copyright (c) 2026, Aegisub Project
// Distributed under the ISC license; see subtitles_provider_libassmod.cpp.
#include "subtitles_provider_libassmod_render.h"
#include "video_frame.h"

#include <gtest/gtest.h>
#include <libaegisub/exception.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

using custom_subtitles::RenderApi;
using custom_subtitles::RenderPath;
using custom_subtitles::DrawFrame;

namespace {
VideoFrame Background(bool flipped = false, int padding = 0, unsigned seed = 0) {
	VideoFrame frame;
	frame.width = 320; frame.height = 180; frame.pitch = frame.width * 4 + padding; frame.flipped = flipped;
	frame.data.assign(frame.pitch * frame.height, 0xCD);
	for (size_t y = 0; y < frame.height; ++y) for (size_t x = 0; x < frame.width; ++x) {
		auto p = frame.data.data() + (flipped ? frame.height - 1 - y : y) * frame.pitch + x * 4;
		p[0] = (x + seed) % 193; p[1] = (y * 2 + seed) % 191; p[2] = (x + y + seed) % 197; p[3] = 0;
	}
	return frame;
}

void ReferenceBlend(ASS_ImageRGBA *list, uint8_t *dst, int width, int height, int stride) {
	for (auto img = list; img; img = img->next) {
		for (int y = 0; y < img->h; ++y) for (int x = 0; x < img->w; ++x) {
			int dy = y + img->dst_y, dx = x + img->dst_x;
			if (dy < 0 || dx < 0 || dy >= height || dx >= width) continue;
			auto s = img->rgba + y * img->stride + x * 4;
			auto d = dst + static_cast<ptrdiff_t>(dy) * stride + dx * 4;
			unsigned inverse = 255 - s[3];
			for (int c = 0; c < 3; ++c) d[c] = static_cast<uint8_t>(s[2-c] + d[c] * inverse / 255);
			d[3] = 0;
		}
	}
}

void ReferenceBlend(ASS_ImageRGBA *list, VideoFrame& frame) {
	auto top = frame.data.data() + (frame.flipped ? (frame.height - 1) * frame.pitch : 0);
	ReferenceBlend(list, top, static_cast<int>(frame.width), static_cast<int>(frame.height),
		frame.flipped ? -static_cast<int>(frame.pitch) : static_cast<int>(frame.pitch));
}

struct Calls {
	RenderApi actual;
	int rgba = 0, automatic = 0, legacy = 0, composite = 0, freed = 0, nodes = 0;
	long long time = -1;
	static Calls *active;
	RenderApi Counted() {
		active = this;
		auto counted = actual;
		if (actual.ass_render_frame_rgba) counted.ass_render_frame_rgba = [](ASS_Renderer *r, ASS_Track *t, long long now, int *change) {
			auto& c = *active; ++c.rgba; c.time = now;
			auto list = c.actual.ass_render_frame_rgba(r,t,now,change);
			for (auto p = list; p; p = p->next) ++c.nodes;
			return list;
		};
		if (actual.ass_render_frame_auto) counted.ass_render_frame_auto = [](ASS_Renderer *r, ASS_Track *t, long long now, int *change) {
			++active->automatic; return active->actual.ass_render_frame_auto(r,t,now,change);
		};
		if (actual.ass_render_frame) counted.ass_render_frame = [](ASS_Renderer *r, ASS_Track *t, long long now, int *change) {
			++active->legacy; return active->actual.ass_render_frame(r,t,now,change);
		};
		if (actual.ass_composite_images_bgra) counted.ass_composite_images_bgra = [](ASS_ImageRGBA *images, uint8_t *dst, int w, int h, int stride) {
			++active->composite; return active->actual.ass_composite_images_bgra(images,dst,w,h,stride);
		};
		if (actual.ass_free_images_rgba) counted.ass_free_images_rgba = [](ASS_ImageRGBA *images) {
			++active->freed; active->actual.ass_free_images_rgba(images);
		};
		return counted;
	}
};
Calls *Calls::active = nullptr;

// Caller-created tiles are used only with fake APIs, never with Mangetsu's
// compositor, which reads private renderer-owned image metadata.
struct FakeOutput {
	std::array<uint8_t,16> pixels{40,60,80,128, 0,90,0,128, 30,0,0,64, 0,0,150,192};
	ASS_ImageRGBA second{}, first{};
	std::array<uint8_t,1> mask{255};
	ASS_Image legacy{};
	FakeOutput() {
		second.w = second.h = 1; second.stride = 4; second.rgba = pixels.data()+12; second.dst_x = 0; second.dst_y = 0;
		first.w = first.h = 2; first.stride = 8; first.rgba = pixels.data(); first.dst_x = -1; first.dst_y = -1; first.next = &second;
		legacy.w = legacy.h = legacy.stride = 1; legacy.bitmap = mask.data(); legacy.color = 0xFA640000;
	}
	static FakeOutput *active;
	RenderApi Api() {
		active = this;
		RenderApi api;
		api.ass_render_frame_rgba = [](ASS_Renderer*, ASS_Track*, long long, int *change) { *change = 0; return &active->first; };
		api.ass_free_images_rgba = [](ASS_ImageRGBA*) {};
		api.ass_composite_images_bgra = [](ASS_ImageRGBA *list, uint8_t *dst, int w, int h, int stride) { ReferenceBlend(list,dst,w,h,stride); return 0; };
		api.ass_render_frame_auto = [](ASS_Renderer*, ASS_Track*, long long, int *change) { *change = 2; return ASS_RenderResult{&active->legacy,&active->first,1}; };
		api.ass_render_frame = [](ASS_Renderer*, ASS_Track*, long long, int *change) { *change = 1; return &active->legacy; };
		return api;
	}
};
FakeOutput *FakeOutput::active = nullptr;
}

TEST(CustomSubtitleCompositor, NativeAndRgbaFallbackPreservePainterOrderPitchAndOrientation) {
	for (bool flipped : {false,true}) for (bool native : {false,true}) {
		FakeOutput output;
		Calls calls{output.Api()};
		if (!native) calls.actual.ass_composite_images_bgra = nullptr;
		auto frame = Background(flipped, 28), expected = frame;
		ReferenceBlend(&output.first,expected);
		auto result = DrawFrame(calls.Counted(),nullptr,nullptr,12345678901LL,frame);
		EXPECT_EQ(frame.data,expected.data);
		EXPECT_EQ(result.path, native ? RenderPath::NativeRgba : RenderPath::FallbackRgba);
		EXPECT_EQ(calls.rgba,1); EXPECT_EQ(calls.automatic,0); EXPECT_EQ(calls.legacy,0);
		EXPECT_EQ(calls.composite,native ? 1 : 0); EXPECT_EQ(calls.freed,1);
		EXPECT_EQ(calls.time,12345678901LL); EXPECT_EQ(result.detect_change,0);
	}
}

TEST(CustomSubtitleCompositor, EmptyRgbaNeverTriggersAnotherRenderer) {
	FakeOutput output; Calls calls{output.Api()};
	calls.actual.ass_render_frame_rgba = [](ASS_Renderer*,ASS_Track*,long long,int *change)->ASS_ImageRGBA* { *change=2; return nullptr; };
	auto frame=Background(), original=frame;
	EXPECT_EQ(DrawFrame(calls.Counted(),nullptr,nullptr,400,frame).path,RenderPath::Empty);
	EXPECT_EQ(frame.data,original.data); EXPECT_EQ(calls.rgba,1); EXPECT_EQ(calls.automatic,0);
	EXPECT_EQ(calls.legacy,0); EXPECT_EQ(calls.freed,0); EXPECT_EQ(calls.composite,0);
}

TEST(CustomSubtitleCompositor, CompositorFailureFallsBackOnSameOwnedList) {
	FakeOutput output; Calls calls{output.Api()};
	calls.actual.ass_composite_images_bgra = [](ASS_ImageRGBA*,uint8_t*,int,int,int) { return -1; };
	auto frame=Background(), expected=frame; ReferenceBlend(&output.first,expected);
	EXPECT_EQ(DrawFrame(calls.Counted(),nullptr,nullptr,400,frame).path,RenderPath::FallbackRgba);
	EXPECT_EQ(frame.data,expected.data); EXPECT_EQ(calls.rgba,1); EXPECT_EQ(calls.composite,1);
	EXPECT_EQ(calls.freed,1); EXPECT_EQ(calls.automatic,0); EXPECT_EQ(calls.legacy,0);
}

TEST(CustomSubtitleCompositor, ExceptionStillFreesRgbaExactlyOnce) {
	FakeOutput output; Calls calls{output.Api()};
	calls.actual.ass_composite_images_bgra = [](ASS_ImageRGBA*,uint8_t*,int,int,int)->int { throw std::runtime_error("test compositor exception"); };
	auto frame=Background(); EXPECT_THROW(DrawFrame(calls.Counted(),nullptr,nullptr,400,frame),std::runtime_error);
	EXPECT_EQ(calls.rgba,1); EXPECT_EQ(calls.freed,1); EXPECT_EQ(calls.composite,1);
}

TEST(CustomSubtitleCompositor, CompatibilityApisAreUsedOnlyWithoutUsableDirectRgba) {
	for (bool auto_api : {false,true}) {
		FakeOutput output; Calls calls{output.Api()}; calls.actual.ass_render_frame_rgba=nullptr;
		if (!auto_api) calls.actual.ass_render_frame_auto=nullptr;
		auto frame=Background(); auto result=DrawFrame(calls.Counted(),nullptr,nullptr,400,frame);
		EXPECT_EQ(result.path,auto_api ? RenderPath::NativeRgba : RenderPath::Legacy);
		EXPECT_EQ(calls.rgba,0); EXPECT_EQ(calls.automatic,auto_api ? 1 : 0);
		EXPECT_EQ(calls.legacy,auto_api ? 0 : 1); EXPECT_EQ(calls.freed,auto_api ? 1 : 0);
		if (!auto_api) { EXPECT_EQ(frame.data[0],0); EXPECT_EQ(frame.data[1],100); EXPECT_EQ(frame.data[2],250); }
	}
	FakeOutput output; Calls calls{output.Api()}; calls.actual.ass_free_images_rgba=nullptr;
	auto frame=Background(); EXPECT_EQ(DrawFrame(calls.Counted(),nullptr,nullptr,400,frame).path,RenderPath::Legacy);
	EXPECT_EQ(calls.rgba,0); EXPECT_EQ(calls.automatic,0); EXPECT_EQ(calls.legacy,1);
	RenderApi unavailable; EXPECT_FALSE(unavailable.CanRender());
	EXPECT_THROW(DrawFrame(unavailable,nullptr,nullptr,400,frame),agi::InternalError);
}

TEST(CustomSubtitleCompositor, InvalidDestinationDoesNotGenerateAFrame) {
	FakeOutput output; Calls calls{output.Api()}; auto frame=Background(); frame.pitch=frame.width*4-1;
	EXPECT_THROW(DrawFrame(calls.Counted(),nullptr,nullptr,400,frame),agi::InternalError); EXPECT_EQ(calls.rgba,0);
	frame=Background(); frame.data.pop_back();
	EXPECT_THROW(DrawFrame(calls.Counted(),nullptr,nullptr,400,frame),agi::InternalError); EXPECT_EQ(calls.rgba,0);
}

namespace {
class Backend {
#ifdef _WIN32
	HMODULE handle=nullptr;
#else
	void *handle=nullptr;
#endif
public:
	RenderApi api;
	ASS_Library *library=nullptr;
	ASS_Library *(*init)()=nullptr;
	void (*done)(ASS_Library*)=nullptr;
	ASS_Renderer *(*renderer_init)(ASS_Library*)=nullptr;
	void (*renderer_done)(ASS_Renderer*)=nullptr;
	void (*fonts)(ASS_Renderer*,const char*,const char*,int,const char*,int)=nullptr;
	void (*frame_size)(ASS_Renderer*,int,int)=nullptr;
	void (*storage_size)(ASS_Renderer*,int,int)=nullptr;
	ASS_Track *(*read)(ASS_Library*,char*,size_t,const char*)=nullptr;
	void (*free_track)(ASS_Track*)=nullptr;
	int (*needs_rgba)(ASS_Renderer*)=nullptr;
	int (*set_image)(ASS_Renderer*,const char*,int,int,int,int,const uint8_t*)=nullptr;

	template<class T> T Symbol(const char *name) {
#ifdef _WIN32
		return reinterpret_cast<T>(GetProcAddress(handle,name));
#else
		return reinterpret_cast<T>(dlsym(handle,name));
#endif
	}
	bool Open(const char *path) {
#ifdef _WIN32
		handle=LoadLibraryA(path);
#elif defined(__APPLE__)
		handle=dlopen(path,RTLD_NOW|RTLD_LOCAL);
#else
		handle=dlopen(path,RTLD_NOW|RTLD_LOCAL|RTLD_DEEPBIND);
#endif
		if (!handle) return false;
#define LOAD(field, name) field=Symbol<decltype(field)>(name)
		LOAD(api.ass_render_frame_rgba,"ass_render_frame_rgba");
		LOAD(api.ass_free_images_rgba,"ass_free_images_rgba");
		LOAD(api.ass_composite_images_bgra,"ass_composite_images_bgra");
		LOAD(api.ass_render_frame_auto,"ass_render_frame_auto"); LOAD(api.ass_render_frame,"ass_render_frame");
		LOAD(init,"ass_library_init"); LOAD(done,"ass_library_done");
		LOAD(renderer_init,"ass_renderer_init"); LOAD(renderer_done,"ass_renderer_done");
		LOAD(fonts,"ass_set_fonts"); LOAD(frame_size,"ass_set_frame_size"); LOAD(storage_size,"ass_set_storage_size");
		LOAD(read,"ass_read_memory"); LOAD(free_track,"ass_free_track"); LOAD(needs_rgba,"ass_frame_needs_rgba");
		LOAD(set_image,"ass_set_tag_image_rgba");
#undef LOAD
		if (!api.HasDirectRgba() || !init || !done || !renderer_init || !renderer_done || !fonts || !frame_size ||
			!storage_size || !read || !free_track || !needs_rgba || !api.ass_render_frame_auto) return false;
		library=init(); return library!=nullptr;
	}
	~Backend() {
		if (library) done(library);
#ifdef _WIN32
		if (handle) FreeLibrary(handle);
#else
		if (handle) dlclose(handle);
#endif
	}
};

struct Session {
	Backend& backend;
	ASS_Renderer *renderer;
	ASS_Track *track;
	Session(Backend& backend, std::string script) : backend(backend) {
		renderer=backend.renderer_init(backend.library);
		if (!renderer) throw std::runtime_error("renderer initialization failed");
		backend.frame_size(renderer,320,180); backend.storage_size(renderer,320,180);
		backend.fonts(renderer,nullptr,"Sans",1,nullptr,1);
		track=backend.read(backend.library,script.data(),script.size(),nullptr);
		if (!track) { backend.renderer_done(renderer); throw std::runtime_error("track load failed"); }
	}
	~Session() { backend.free_track(track); backend.renderer_done(renderer); }
};

std::string Script(std::string const& events) {
	return "[Script Info]\nScriptType: v4.00+\nPlayResX: 320\nPlayResY: 180\nWrapStyle: 0\nScaledBorderAndShadow: yes\n"
		"[V4+ Styles]\nFormat: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\n"
		"Style: Default,Sans,22,&H00FFFFFF,&H0000FF00,&H00000000,&H80000000,0,0,0,0,100,100,0,0,1,2,2,2,10,10,10,1\n"
		"[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"+events;
}

std::string Event(std::string const& text, int layer=0) {
	return "Dialogue: "+std::to_string(layer)+",0:00:00.00,0:00:04.00,Default,,0,0,0,,"+text+"\n";
}

class CustomSubtitleBackend : public ::testing::TestWithParam<const char*> {
protected:
	Backend backend;
	void SetUp() override {
		auto path=std::getenv(GetParam());
		ASSERT_NE(path,nullptr);
		ASSERT_TRUE(backend.Open(path)) << "Cannot load configured custom backend: " << path;
		if (std::string(GetParam()).find("MANGETSU")!=std::string::npos)
			ASSERT_NE(backend.api.ass_composite_images_bgra,nullptr) << "Bundled Mangetsu must expose its native compositor.";
	}
	void Check(std::string const& events, bool stress=false, bool special=false, bool blend=false, bool image=false) {
		Session subject(backend,Script(events)), reference(backend,Script(events));
		if (image) {
			ASSERT_NE(backend.set_image,nullptr);
			std::vector<uint8_t> tile(32*24*4);
			for (size_t i=0;i<tile.size();i+=4) { tile[i]=220;tile[i+1]=30;tile[i+2]=90;tile[i+3]=128; }
			ASSERT_EQ(backend.set_image(subject.renderer,"fixture.png",1,32,24,32*4,tile.data()),0);
			ASSERT_EQ(backend.set_image(reference.renderer,"fixture.png",1,32,24,32*4,tile.data()),0);
		}
		for (long long time : {100LL,750LL,1500LL,2500LL,3850LL}) for (bool flipped : {false,true}) {
			SCOPED_TRACE(time);
			SCOPED_TRACE(flipped);
			auto frame=Background(flipped,24), expected=frame;
			Calls calls{backend.api};
			auto result=DrawFrame(calls.Counted(),subject.renderer,subject.track,time,frame);
			EXPECT_EQ(calls.rgba,1); EXPECT_EQ(calls.automatic,0); EXPECT_EQ(calls.legacy,0);
			ASSERT_GT(calls.nodes,0); EXPECT_EQ(calls.freed,1);
			EXPECT_EQ(calls.composite,backend.api.ass_composite_images_bgra ? 1 : 0);
			EXPECT_EQ(result.path,backend.api.ass_composite_images_bgra ? RenderPath::NativeRgba : RenderPath::FallbackRgba);
			if (special) EXPECT_NE(backend.needs_rgba(subject.renderer),0);
			else EXPECT_EQ(backend.needs_rgba(subject.renderer),0);
			int change=0;
			if (blend) {
				// Existing RGBA auto path is the reference for private blend modes.
				auto old=backend.api.ass_render_frame_auto(reference.renderer,reference.track,time,&change);
				std::unique_ptr<ASS_ImageRGBA,decltype(backend.api.ass_free_images_rgba)> owned(old.imgs_rgba,backend.api.ass_free_images_rgba);
				ASSERT_TRUE(old.use_rgba); ASSERT_NE(owned.get(),nullptr);
				auto top=expected.data.data()+(flipped ? (expected.height-1)*expected.pitch : 0);
				ASSERT_EQ(backend.api.ass_composite_images_bgra(owned.get(),top,320,180,flipped ? -static_cast<int>(expected.pitch) : static_cast<int>(expected.pitch)),0);
			}
			else {
				std::unique_ptr<ASS_ImageRGBA,decltype(backend.api.ass_free_images_rgba)> owned(
					backend.api.ass_render_frame_rgba(reference.renderer,reference.track,time,&change),backend.api.ass_free_images_rgba);
				ASSERT_NE(owned.get(),nullptr); ReferenceBlend(owned.get(),expected);
			}
			EXPECT_TRUE(frame.data==expected.data) << "BGRA bytes/padding differ from ordered renderer RGBA reference";
			EXPECT_EQ(result.detect_change,change);
			if (stress) {
				EXPECT_GE(calls.nodes,500);
				std::cout << "stress renderer=" << GetParam() << " time=" << time << " nodes=" << calls.nodes
					<< " render_calls=" << calls.rgba << " native_compositor_calls=" << calls.composite << " legacy_calls=" << calls.legacy << '\n';
			}
		}
	}
};
}

TEST_P(CustomSubtitleBackend, OrdinaryAssAppearance) {
	const std::vector<std::string> fixtures{
		Event("ordinary dialogue"), Event("{\\bord4\\shad3\\1a&H70&\\3a&H20&}borders shadows alpha"),
		Event("{\\fad(500,500)}fade in/out"), Event("{\\fade(255,64,200,0,300,3000,4000)}seven argument fade"),
		Event("{\\k50}kara{\\kf100}oke{\\ko100} sweep"),
		Event("{\\pos(160,100)\\clip(70,60,250,140)}rectangular clip"),
		Event("{\\pos(160,100)\\iclip(120,60,180,140)}inverse rectangular clip"),
		Event("{\\pos(160,100)\\clip(m 0 0 l 320 0 160 180)}vector clip"),
		Event("{\\pos(160,100)\\iclip(2,m 230 110 l 410 110 320 270)}inverse scaled vector clip"),
		Event("{\\pos(100,100)\\t(0,3000,\\frz20\\fscx130\\1c&H2277DD&)}transform"),
		Event("{\\pos(160,100)\\clip(0,0,170,180)\\t(0,3000,\\clip(120,40,300,160))}animated rectangular clip"),
		Event("{\\pos(160,100)\\1c&H0000FF&}lower red",0)+Event("{\\pos(165,103)\\1c&HFF0000&\\alpha&H60&}upper blue",2),
		Event("first collision line")+Event("second collision line"),
		Event("{\\fnDefinitelyMissingAegisubFixtureFont}fallback font text")
	};
	for (auto const& fixture : fixtures) { SCOPED_TRACE(fixture); Check(fixture); }
}

TEST_P(CustomSubtitleBackend, RepeatedEventStressUsesOneRenderAndNoLegacyConsumer) {
	std::ostringstream events;
	for (int i=0;i<600;++i) {
		unsigned color=(i*7919)&0xFFFFFF;
		events << "Dialogue: " << (i%3) << ",0:00:00.00,0:00:04.00,Default,,0,0,0,,{\\pos(160,100)\\1c&H"
			<< std::hex << std::setw(6) << std::setfill('0') << color << std::dec << "&\\clip(" << (i%45)
			<< ",50," << (240+i%70) << ",140)\\t(0,3500,\\clip(" << (i%60) << ",55," << (250+i%60)
			<< ",145))}Repeated geometry\n";
	}
	Check(events.str(),true);
}

TEST_P(CustomSubtitleBackend, UnchangedSubtitlesAreCompositedOnEveryFreshVideoFrame) {
	Session session(backend,Script(Event("static subtitles")));
	auto first=Background(false,0,0), second=Background(false,0,40), expected=second;
	Calls calls{backend.api}; auto api=calls.Counted();
	DrawFrame(api,session.renderer,session.track,1000,first);
	auto result=DrawFrame(api,session.renderer,session.track,1000,second);
	EXPECT_EQ(calls.rgba,2); EXPECT_EQ(calls.freed,2); EXPECT_EQ(calls.automatic,0); EXPECT_EQ(calls.legacy,0);
	EXPECT_EQ(result.detect_change,0); EXPECT_EQ(calls.composite,backend.api.ass_composite_images_bgra ? 2 : 0);
	int change=0;
	std::unique_ptr<ASS_ImageRGBA,decltype(backend.api.ass_free_images_rgba)> owned(
		backend.api.ass_render_frame_rgba(session.renderer,session.track,1000,&change),backend.api.ass_free_images_rgba);
	ReferenceBlend(owned.get(),expected); EXPECT_TRUE(second.data==expected.data);
}

TEST_P(CustomSubtitleBackend, ExistingRgbaGradientsAndTagImages) {
	Check(Event("{\\pos(160,100)\\1vc(&H0000FF&,&H00FF00&,&HFF0000&,&HFFFFFF&)}RGBA gradient"),false,true);
	Check(Event("{\\pos(160,100)\\img(fixture.png)}image fill"),false,true,false,true);
}

TEST_P(CustomSubtitleBackend, MangetsuBlendStillUsesRendererPrivateNativeCompositor) {
	if (!backend.api.ass_composite_images_bgra) {
		EXPECT_STREQ(GetParam(),"AEGISUB_LIBASSMOD_TEST_LIBRARY");
		return; // current libassmod has neither this API nor Mangetsu blend modes
	}
	Check(Event("{\\pos(150,100)\\1c&H55DD33&}background",0)+
		Event("{\\pos(160,100)\\blend(mult)\\alpha&H50&\\1c&HCC7733&}foreground",2),false,true,true);
}

namespace {
std::vector<const char*> ConfiguredBackends() {
	std::vector<const char*> backends;
	for (auto key : {"AEGISUB_LIBASSMOD_TEST_LIBRARY","AEGISUB_MANGETSU_TEST_LIBRARY"})
		if (std::getenv(key)) backends.push_back(key);
	return backends;
}
}
INSTANTIATE_TEST_CASE_P(BundledRenderers,CustomSubtitleBackend,
	::testing::ValuesIn(ConfiguredBackends()));
