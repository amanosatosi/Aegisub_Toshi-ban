#include "motion_track_apply.h"

#include "../ass_style.h"
#include <libaegisub/vfr.h>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

namespace motion_tracking {
namespace {
constexpr double pi = 3.14159265358979323846;
using Values = std::map<std::string, double>;

std::string Number(double v) {
	if (!std::isfinite(v)) throw std::invalid_argument("Motion contains a non-finite coordinate.");
	if (std::abs(v) < 0.0005) v = 0;
	std::ostringstream s;
	s.imbue(std::locale::classic());
	s << std::fixed << std::setprecision(3) << v;
	auto text = s.str();
	while (text.back() == '0') text.pop_back();
	if (text.back() == '.') text.pop_back();
	return text;
}

double Param(AssOverrideTag const& tag, size_t i, double fallback = 0) {
	return i < tag.Params.size() ? tag.Params[i].Get<double>(fallback) : fallback;
}

double Progress(double time, double start, double end, double accel = 1) {
	if (end <= start) return time >= end ? 1 : 0;
	return std::pow(std::clamp((time-start)/(end-start), 0.0, 1.0), accel);
}

bool ClipTag(AssOverrideTag const& tag) { return tag.Name == "\\clip" || tag.Name == "\\iclip"; }

std::string Canonical(std::string const& n) {
	return n == "\\fr" ? "\\frz" : n;
}

Values Defaults(AssStyle const* style) {
	return {{"\\fscx", style ? style->scalex : 100}, {"\\fscy", style ? style->scaley : 100},
		{"\\frz", style ? style->angle : 0}, {"\\xbord", style ? style->outline_w : 2},
		{"\\ybord", style ? style->outline_w : 2}, {"\\xshad", style ? style->shadow_w : 2},
		{"\\yshad", style ? style->shadow_w : 2}, {"\\blur", 0}};
}

std::vector<std::string> ScalarNames(AssOverrideTag const& tag, MotionApplyOptions const& o) {
	auto n = Canonical(tag.Name);
	if (o.scale && n == "\\fsc") return {"\\fscx", "\\fscy"};
	if (o.scale && (n == "\\fscx" || n == "\\fscy")) return {n};
	if (o.rotation && n == "\\frz") return {n};
	if (o.scale && o.border) {
		if (n == "\\bord") return {"\\xbord", "\\ybord"};
		if (n == "\\xbord" || n == "\\ybord") return {n};
	}
	if (o.scale && o.shadow) {
		if (n == "\\shad") return {"\\xshad", "\\yshad"};
		if (n == "\\xshad" || n == "\\yshad") return {n};
	}
	if (o.scale && o.blur && n == "\\blur") return {n};
	return {};
}

struct Point { double x = 0, y = 0; };
struct RelativeMotion {
	Point center, reference;
	double sx = 1, sy = 1, rotation = 0;
	Point Map(Point p) const {
		double x = (p.x-reference.x)*sx, y = (p.y-reference.y)*sy;
		double c = std::cos(rotation*pi/180), s = std::sin(rotation*pi/180);
		// Tracker angles use image coordinates (positive clockwise). ASS frz
		// uses the opposite sign; image-space points use the tracker sign here.
		return {center.x + x*c - y*s, center.y + x*s + y*c};
	}
};

MotionTrackFrame const& Sample(MotionTrackResult const& track, int frame) {
	auto it = std::lower_bound(track.frames.begin(), track.frames.end(), frame,
		[](auto const& a, int b) { return a.frame < b; });
	if (it == track.frames.end() || it->frame != frame || it->state == MotionTrackState::Lost)
		throw std::invalid_argument("Track every subtitle frame and the main video reference frame before Apply.");
	if (!std::isfinite(it->x) || !std::isfinite(it->y) || !std::isfinite(it->scale_x) ||
		!std::isfinite(it->scale_y) || !std::isfinite(it->rotation_deg) || it->scale_x <= 0 || it->scale_y <= 0)
		throw std::invalid_argument("Tracking contains an invalid motion sample.");
	return *it;
}

RelativeMotion Relative(MotionTrackResult const& track, int frame, int reference,
	int width, int height, MotionApplyOptions const& o, bool clip) {
	if (track.source_width <= 0 || track.source_height <= 0)
		throw std::invalid_argument("Tracking has no source video resolution.");
	auto const& a = Sample(track, reference);
	auto const& b = Sample(track, frame);
	double rx = double(width)/track.source_width, ry = double(height)/track.source_height;
	RelativeMotion m;
	m.reference = {a.x*rx, a.y*ry};
	m.center = {(clip || o.position_x ? b.x : a.x)*rx, (clip || o.position_y ? b.y : a.y)*ry};
	m.sx = clip || o.scale ? b.scale_x/a.scale_x : 1;
	m.sy = clip || o.scale ? b.scale_y/a.scale_y : 1;
	m.rotation = clip || o.rotation ? b.rotation_deg-a.rotation_deg : 0;
	// Anamorphic script coordinates require conjugating rotation by the pixel
	// scaling matrix; callers map in source pixels then convert back below.
	return m;
}

Point MapPoint(RelativeMotion const& m, Point p, double aspect) {
	Point q = p;
	q.x /= aspect;
	auto source = m;
	source.reference.x /= aspect;
	source.center.x /= aspect;
	q = source.Map(q);
	q.x *= aspect;
	return q;
}

struct ClipShape {
	std::string name;
	bool rectangle = false;
	int scale = 1;
	std::vector<std::string> commands; // command before each point, or empty
	std::vector<Point> points;
};

ClipShape ParseClip(AssOverrideTag const& tag) {
	ClipShape shape;
	shape.name = tag.Name;
	shape.rectangle = tag.Params.size() == 4;
	if (shape.rectangle) {
		for (auto const& p : tag.Params) if (p.omitted)
			throw std::invalid_argument("Fix the malformed rectangular clip before Apply.");
		shape.points = {{Param(tag,0), Param(tag,1)}, {Param(tag,2), Param(tag,3)}};
		return shape;
	}
	if (tag.Params.size() != 2 || tag.Params[1].omitted) throw std::invalid_argument("Fix the malformed clip before Apply.");
	shape.scale = tag.Params[0].Get<int>(1);
	if (shape.scale < 1 || shape.scale > 16) throw std::invalid_argument("Clip drawing scale must be between 1 and 16.");
	double units = std::ldexp(1.0, shape.scale-1);
	std::istringstream stream(tag.Params[1].Get<std::string>());
	stream.imbue(std::locale::classic());
	std::string token, pending;
	while (stream >> token) {
		if (token.size() == 1 && std::string("mnlbspc").find(token[0]) != std::string::npos) {
			pending += token + " ";
			continue;
		}
		double x, y;
		std::istringstream number(token);
		number.imbue(std::locale::classic());
		if (!(number >> x) || !(stream >> y) || !std::isfinite(x) || !std::isfinite(y))
			throw std::invalid_argument("Fix the malformed vector clip before Apply.");
		shape.commands.push_back(pending);
		pending.clear();
		shape.points.push_back({x/units,y/units});
	}
	if (shape.points.empty()) throw std::invalid_argument("The vector clip has no points.");
	// Closing commands are retained as a trailing command after the last point.
	shape.commands.push_back(pending);
	return shape;
}

enum class SlotKind { Scalar, Position, Origin, Rectangle, Vector, ClipPosition };
struct Slot {
	SlotKind kind;
	std::string name;
	size_t index;
	size_t count;
	ClipShape shape;
};
struct Part { std::string text; int slot = -1; bool retime = false; };
struct State {
	std::vector<Part> parts;
	std::vector<Slot> slots;
	std::vector<double> values;
	std::vector<double> tolerance;
	bool animated_origin = false;
	bool animated_vector = false;
	void Text(std::string text, bool retime = false) { parts.push_back({std::move(text), -1, retime}); }
	void Add(SlotKind kind, std::string name, std::vector<double> data, double error, ClipShape shape = {}) {
		parts.push_back({{}, static_cast<int>(slots.size())});
		slots.push_back({kind, std::move(name), values.size(), data.size(), std::move(shape)});
		values.insert(values.end(), data.begin(), data.end());
		tolerance.insert(tolerance.end(), data.size(), error);
	}
};

struct SourceGeometry {
	Point position;
	Point origin;
	bool has_origin = false;
	AssOverrideTag const* move = nullptr;
	bool found_position = false;
};

SourceGeometry Geometry(AssDialogue const& source,
	std::vector<std::unique_ptr<AssDialogueBlock>> const& blocks, AssStyle const* style,
	int width, int height, double time) {
	SourceGeometry g;
	int align = style ? style->alignment : 2;
	bool found_alignment = false;
	int legacy_alignment = 0;
	auto margin = source.Margin;
	if (style) for (int i = 0; i < 3; ++i) if (!margin[i]) margin[i] = style->Margin[i];
	for (auto const& block : blocks) {
		auto b = dynamic_cast<AssDialogueBlockOverride const*>(block.get());
		if (!b) continue;
		for (auto const& t : b->Tags) {
			if (!t.IsValid()) continue;
			if (t.Name == "\\an" && !found_alignment) { align = static_cast<int>(Param(t,0,align)); found_alignment = true; }
			if (t.Name == "\\a" && !legacy_alignment) legacy_alignment = AssStyle::SsaToAss(static_cast<int>(Param(t,0,2)));
			if (!g.found_position && (t.Name == "\\pos" || t.Name == "\\move")) {
				g.position = {Param(t,0), Param(t,1)};
				g.found_position = true;
				if (t.Name == "\\move") {
					double t1 = Param(t,4), t2 = Param(t,5);
					if (t1 <= 0 && t2 <= 0) t2 = source.End-source.Start;
					double p = Progress(time,t1,t2);
					g.position.x += (Param(t,2)-g.position.x)*p;
					g.position.y += (Param(t,3)-g.position.y)*p;
				}
			}
			if (!g.has_origin && t.Name == "\\org") {
				g.origin = {Param(t,0),Param(t,1)};
				g.has_origin = true;
			}
		}
	}
	if (!found_alignment && legacy_alignment) align = legacy_alignment;
	align = std::clamp(align,1,9);
	if (!g.found_position) {
		int horizontal = (align-1)%3, vertical = (align-1)/3;
		g.position.x = horizontal == 0 ? margin[0] : horizontal == 1 ? (width+margin[0]-margin[1])/2.0 : width-margin[1];
		g.position.y = vertical == 0 ? height-margin[2] : vertical == 1 ? height/2.0 : margin[2];
	}
	return g;
}

struct TransformTime { double start, end, accel; AssDialogueBlockOverride* effect; };
TransformTime Transform(AssOverrideTag const& t, int duration) {
	if (t.Params.size() != 4) throw std::invalid_argument("Fix the malformed transform before Apply.");
	return {Param(t,0), t.Params[1].omitted ? double(duration) : Param(t,1),
		Param(t,2,1), t.Params[3].Get<AssDialogueBlockOverride*>()};
}

void EmitScalars(State& out, Values const& values, RelativeMotion const& motion, MotionApplyOptions const& o) {
	for (auto const& item : values) {
		AssOverrideTag tag(item.first + Number(item.second));
		if (ScalarNames(tag,o).empty()) continue;
		double value = item.second;
		double error = o.tolerance.outline;
		if (item.first == "\\frz") { value -= motion.rotation; error = o.tolerance.rotation; }
		else {
			double ratio = item.first == "\\fscx" || item.first == "\\xbord" || item.first == "\\xshad" ? motion.sx : motion.sy;
			value *= ratio;
			if (item.first == "\\fscx" || item.first == "\\fscy") error = o.tolerance.scale;
		}
		out.Add(SlotKind::Scalar,item.first,{value},error);
	}
}

// Remove only the numeric tags consumed by motion. Unrelated transform effects
// remain as native parsed tags, with their original acceleration and time domain.
std::string ProcessTransform(AssOverrideTag const& t, Values& values, double sample_time,
	int duration, MotionApplyOptions const& o) {
	auto transform = Transform(t,duration);
	double p = Progress(sample_time,transform.start,transform.end,transform.accel);
	std::string remaining;
	for (auto const& nested : transform.effect->Tags) {
		auto names = ScalarNames(nested,o);
		if (names.empty()) { remaining += std::string(nested); continue; }
		for (auto const& name : names) {
			double target = Param(nested,0,values[name]);
			values[name] += (target-values[name])*p;
		}
	}
	if (remaining.empty()) return {};
	return "\\t(" + Number(transform.start) + "," + Number(transform.end) + "," +
		Number(transform.accel) + "," + remaining + ")";
}

State BuildState(AssDialogue const& source, MotionStyleResolver const& styles,
	MotionTrackResult const& main, MotionTrackResult const* clip, int frame, int reference,
	agi::vfr::Framerate const& tc, int width, int height, MotionApplyOptions const& o, bool clippos) {
	State out;
	auto blocks = source.ParseTags();
	auto style = styles(source.Style.get());
	double time = tc.TimeAtFrame(o.preserve_transforms ? frame : reference)-source.Start;
	auto geometry = Geometry(source,blocks,style,width,height,time);
	auto motion = Relative(main,frame,reference,width,height,o,false);
	double aspect = (double(width)/main.source_width)/(double(height)/main.source_height);
	auto position = MapPoint(motion,geometry.position,aspect);
	// An explicitly disabled coordinate remains the positioned line coordinate,
	// including when scale/rotation would otherwise move the offset from its anchor.
	if (!o.position_x) position.x = geometry.position.x;
	if (!o.position_y) position.y = geometry.position.y;
	out.Text("{");
	out.Add(SlotKind::Position,"\\pos",{position.x,position.y},o.tolerance.position);
	if (geometry.has_origin && o.follow_origin) {
		auto origin = MapPoint(motion,geometry.origin,aspect);
		out.Add(SlotKind::Origin,"\\org",{origin.x,origin.y},o.tolerance.position);
	}
	out.Text("}");
	Values values = Defaults(style);
	bool initialized = false;
	bool emitted_clippos = false;
	for (auto const& block : blocks) {
		auto b = dynamic_cast<AssDialogueBlockOverride const*>(block.get());
		if (!b) {
			if (!initialized) { out.Text("{"); EmitScalars(out,values,motion,o); out.Text("}"); initialized = true; }
			out.Text(block->GetText());
			continue;
		}
		out.Text("{");
		for (auto const& t : b->Tags) {
			if ((t.Name == "\\mover" || t.Name == "\\moves3" || t.Name == "\\moves4") ||
				(t.Name == "\\frs" && o.rotation))
				throw std::invalid_argument("Normalize existing Mangetsu motion tags to pos/move and frz before native Apply.");
			if (t.IsValid() && (t.Name == "\\pos" || t.Name == "\\move" || (o.follow_origin && t.Name == "\\org"))) continue;
			if (t.IsValid() && t.Name == "\\r") {
				EmitScalars(out,values,motion,o);
				out.Text(std::string(t));
				auto name = t.Params[0].Get<std::string>(source.Style.get());
				values = Defaults(styles(name.empty() ? source.Style.get() : name));
				continue;
			}
			auto names = t.IsValid() ? ScalarNames(t,o) : std::vector<std::string>{};
			if (!names.empty()) {
				for (auto const& name : names) values[name] = Param(t,0,values[name]);
				continue;
			}
			if (t.IsValid() && t.Name == "\\t") {
				out.Text(ProcessTransform(t,values,time,source.End-source.Start,o),true);
				continue;
			}
			if (t.IsValid() && ClipTag(t) && clip) {
				auto shape = ParseClip(t);
				if ((shape.rectangle && !o.rectangular_clips) || (!shape.rectangle && !o.vector_clips)) {
					out.Text(std::string(t)); continue;
				}
				auto cm = Relative(*clip,frame,reference,width,height,o,true);
				double ca = (double(width)/clip->source_width)/(double(height)/clip->source_height);
				if (clippos) {
					out.Text(std::string(t));
					if (!emitted_clippos) {
						out.Add(SlotKind::ClipPosition,"\\clippos",{cm.center.x-cm.reference.x,cm.center.y-cm.reference.y},o.tolerance.position);
						emitted_clippos = true;
					}
					continue;
				}
				if (shape.rectangle && std::abs(cm.rotation) > 0.000001) {
					Point a = shape.points[0], b = shape.points[1];
					shape.points = {a,{b.x,a.y},b,{a.x,b.y}};
					shape.commands = {"m ","l ","","",""};
					shape.rectangle = false;
				}
				std::vector<double> data;
				for (auto p : shape.points) {
					p = MapPoint(cm,p,ca);
					data.push_back(p.x); data.push_back(p.y);
				}
				out.Add(shape.rectangle ? SlotKind::Rectangle : SlotKind::Vector,shape.name,std::move(data),o.tolerance.position,shape);
				continue;
			}
			out.Text(std::string(t),true);
		}
		EmitScalars(out,values,motion,o);
		initialized = true;
		out.Text("}");
	}
	if (!initialized) { out.Text("{"); EmitScalars(out,values,motion,o); out.Text("}"); }
	return out;
}

std::string Tuple(std::vector<double> const& values, size_t start, size_t count, std::string separator = ",") {
	std::string s;
	for (size_t i = 0; i < count; ++i) {
		if (i) s += separator;
		s += Number(values[start+i]);
	}
	return s;
}

std::string Serialize(State const& state, MotionRegion const& region, int t1, int t2) {
	std::string text;
	for (auto const& part : state.parts) {
		if (part.slot < 0) { text += part.text; continue; }
		auto const& slot = state.slots[part.slot];
		size_t i = slot.index;
		bool same = true;
		for (size_t k = 0; k < slot.count; ++k)
			if (std::abs(region.from[i+k]-region.to[i+k]) > 0.000001) same = false;
		auto tag = [&](std::vector<double> const& v) {
			if (slot.kind == SlotKind::Scalar) return slot.name + Number(v[i]);
			if (slot.kind == SlotKind::Vector) {
				double units = std::ldexp(1.0,slot.shape.scale-1);
				std::string drawing;
				for (size_t p = 0; p < slot.count/2; ++p) {
					if (p) drawing += " ";
					drawing += slot.shape.commands[p] + Number(v[i+2*p]*units) + " " + Number(v[i+2*p+1]*units);
				}
				drawing += " " + slot.shape.commands.back();
				return slot.name + "(" + std::to_string(slot.shape.scale) + "," + drawing + ")";
			}
			return slot.name + "(" + Tuple(v,i,slot.count) + ")";
		};
		if (slot.kind == SlotKind::Position && !same) {
			text += "\\move(" + Tuple(region.from,i,2) + "," + Tuple(region.to,i,2) + "," +
				std::to_string(t1) + "," + std::to_string(t2) + ")";
		}
		else {
			text += tag(region.from);
			if (!same) text += "\\t(" + std::to_string(t1) + "," + std::to_string(t2) + "," + tag(region.to) + ")";
		}
	}
	for (size_t empty = text.find("{}"); empty != std::string::npos; empty = text.find("{}")) text.erase(empty,2);
	return text;
}

// Retiming goes through native parameters, including nested transforms. Convert
// duration-relative fad to an explicit envelope before splitting; it must never
// restart at every generated event.
void RetimeBlock(AssDialogueBlockOverride& block, int offset, int duration) {
	for (auto& tag : block.Tags) {
		if (!tag.IsValid()) continue;
		if (tag.Name == "\\fad") {
			if (tag.Params.size() > 2 && (!tag.Params[2].omitted || !tag.Params[3].omitted))
				throw std::invalid_argument("Colored fades cannot yet be split by native motion Apply.");
			int a = static_cast<int>(Param(tag,0)), b = static_cast<int>(Param(tag,1));
			tag.SetText("\\fade(255,0,255," + std::to_string(-offset) + "," + std::to_string(a-offset) + "," +
				std::to_string(duration-b-offset) + "," + std::to_string(duration-offset) + ")");
			continue;
		}
		if (tag.Name == "\\t") {
			auto t = Transform(tag,duration);
			tag.Params[0].Set(t.start-offset);
			tag.Params[1].Set(t.end-offset);
			continue;
		}
		if (tag.Name == "\\fade" || tag.Name == "\\move") {
			for (auto& p : tag.Params)
				if (!p.omitted && p.classification == AssParameterClass::RELATIVE_TIME_START)
					p.Set(p.Get<int>()-offset);
		}
		if (tag.Name == "\\k" || tag.Name == "\\K" || tag.Name == "\\kf" || tag.Name == "\\ko" || tag.Name == "\\kt")
			throw std::invalid_argument("Karaoke timing cannot yet be split by native motion Apply.");
	}
}

std::string RetimeSource(std::string const& text, int offset, int duration) {
	AssDialogue line;
	line.Text = text;
	auto blocks = line.ParseTags();
	for (auto& block : blocks)
		if (auto b = dynamic_cast<AssDialogueBlockOverride*>(block.get())) RetimeBlock(*b,offset,duration);
	line.UpdateText(blocks);
	return line.Text.get();
}

bool TranslationOnly(MotionTrackResult const& clip, int first, int last, int reference) {
	auto const& a = Sample(clip,reference);
	for (int f = first; f <= last; ++f) {
		auto const& b = Sample(clip,f);
		// Use only for actual translation, never approximate geometric changes
		// with clippos merely because the numeric scale signal is nearly static.
		if (std::abs(b.scale_x/a.scale_x-1) > 0.000001 || std::abs(b.scale_y/a.scale_y-1) > 0.000001 ||
			std::abs(b.rotation_deg-a.rotation_deg) > 0.000001) return false;
	}
	return true;
}
}

MotionApplication BuildMotionApplication(AssDialogue const& source,
	MotionStyleResolver const& styles, MotionTrackResult const& main,
	MotionTrackResult const* separate_clip, int reference, agi::vfr::Framerate const& tc,
	int width, int height, MotionApplyOptions const& requested) {
	MotionApplyOptions o = requested;
	if (!tc.IsLoaded() || width <= 0 || height <= 0 || source.End <= source.Start)
		throw std::invalid_argument("The subtitle timing, video timing, or script resolution is invalid.");
	if (source.Comment) throw std::invalid_argument("Select dialogue events to apply motion.");
	int first = tc.FrameAtTime(source.Start,agi::vfr::START), last = tc.FrameAtTime(source.End,agi::vfr::END);
	if (last < first) throw std::invalid_argument("The subtitle does not cover a video frame.");
	auto const& reference_sample = Sample(main,reference);
	bool changes_scale = false, changes_rotation = false;
	for (int f = first; f <= last; ++f) {
		auto const& sample = Sample(main,f);
		changes_scale |= std::abs(sample.scale_x/reference_sample.scale_x-1) > 0.000001 ||
			std::abs(sample.scale_y/reference_sample.scale_y-1) > 0.000001;
		changes_rotation |= std::abs(sample.rotation_deg-reference_sample.rotation_deg) > 0.000001;
	}
	// A translation track needs only position tags. Preserve source styling and
	// its animations verbatim instead of materializing unnecessary style defaults.
	o.scale &= changes_scale;
	o.rotation &= changes_rotation;
	MotionTrackResult const* clip = o.clip_source == ClipMotionSource::MainTrack ? &main :
		o.clip_source == ClipMotionSource::SeparateTrack ? separate_clip : nullptr;
	if (!HasMotionClip(source)) clip = nullptr;
	bool clippos = clip && o.mangetsu_clippos && TranslationOnly(*clip,first,last,reference);
	bool has_vector = false, has_origin = false, rotated_clip = false;
	for (auto const& block : source.ParseTags()) {
		auto b = dynamic_cast<AssDialogueBlockOverride const*>(block.get());
		if (!b) continue;
		for (auto const& t : b->Tags) {
			if (t.Name == "\\org" && o.follow_origin) has_origin = true;
			if (ClipTag(t) && t.Params.size() == 2 && o.vector_clips) has_vector = true;
			if (clip && (t.Name == "\\clippos" || t.Name == "\\clips" || t.Name == "\\movevc"))
				throw std::invalid_argument("Remove existing animated clip offsets before tracking the clip.");
			if (t.Name == "\\t") {
				auto effect = Transform(t,source.End-source.Start).effect;
				for (auto const& nested : effect->Tags) {
					if (nested.Name == "\\pos" || nested.Name == "\\move" || nested.Name == "\\org")
						throw std::invalid_argument("Normalize source position/origin transforms before native Apply.");
					if (clip && (ClipTag(nested) || nested.Name == "\\clippos"))
						throw std::invalid_argument("Animated source clips cannot yet be combined with a clip track.");
				}
			}
		}
	}
	if (clip) {
		auto const& r = Sample(*clip,reference);
		for (int f = first; f <= last; ++f)
			if (std::abs(Sample(*clip,f).rotation_deg-r.rotation_deg) > 0.000001) rotated_clip = true;
		if (rotated_clip && !o.vector_clips && o.rectangular_clips)
			throw std::invalid_argument("Rotated rectangular clips require vector clip handling. Enable it in Advanced Apply.");
	}
	std::vector<State> states;
	std::vector<MotionSample> samples;
	for (int frame = first; frame <= last; ++frame) {
		auto state = BuildState(source,styles,main,clip,frame,reference,tc,width,height,o,clippos);
		samples.push_back({frame,double(tc.TimeAtFrame(frame)),state.values});
		states.push_back(std::move(state));
	}
	// Rotated rectangles must have a consistent vector topology even at the
	// reference sample whose rotation is zero. Rebuild them as native vectors.
	if (clip && rotated_clip) {
		AssDialogue vector_source(source);
		auto blocks = vector_source.ParseTags();
		for (auto& block : blocks) if (auto b = dynamic_cast<AssDialogueBlockOverride*>(block.get())) {
			for (auto& t : b->Tags) if (ClipTag(t) && t.Params.size() == 4 && o.rectangular_clips) {
				t.SetText(t.Name + "(m " + Number(Param(t,0)) + " " + Number(Param(t,1)) + " l " +
					Number(Param(t,2)) + " " + Number(Param(t,1)) + " " + Number(Param(t,2)) + " " +
					Number(Param(t,3)) + " " + Number(Param(t,0)) + " " + Number(Param(t,3)) + ")");
			}
		}
		vector_source.UpdateText(blocks);
		states.clear(); samples.clear();
		for (int frame = first; frame <= last; ++frame) {
			auto state = BuildState(vector_source,styles,main,clip,frame,reference,tc,width,height,o,false);
			samples.push_back({frame,double(tc.TimeAtFrame(frame)),state.values});
			states.push_back(std::move(state));
		}
		has_vector = true;
	}
	auto analysis = OptimizeMotion(samples,states.front().tolerance,o.encoding);
	// Standard ASS cannot animate org or vector paths. A static such signal
	// does not prevent independent position/scale/rotation optimization.
	if (!analysis.frame_by_frame && (has_origin || (clip && has_vector && !clippos))) {
		bool sampled_geometry = false;
		std::vector<MotionRegion> encoded;
		for (size_t index = 0; index < analysis.regions.size(); ++index) {
			auto const& region = analysis.regions[index];
			bool moving_geometry = false;
			for (auto const& slot : states[region.first].slots) {
				if (slot.kind != SlotKind::Origin && slot.kind != SlotKind::Vector) continue;
				for (size_t k = 0; k < slot.count; ++k)
					moving_geometry |= std::abs(region.from[slot.index+k]-region.to[slot.index+k]) > 0.000001;
			}
			if (!moving_geometry) { encoded.push_back(region); continue; }
			sampled_geometry = true;
			size_t stop = index+1 == analysis.regions.size() ? samples.size() : analysis.regions[index+1].first;
			for (size_t i = region.first; i < stop; ++i) {
				auto values = samples[i].values;
				// Keep independent stationary signals suppressed even when another
				// signal requires sampled geometry. Retain complete optimized holds.
				for (size_t k = 0; k < values.size(); ++k)
					if (region.from[k] == region.to[k]) values[k] = region.from[k];
				encoded.push_back({i,i,values,values,true});
			}
		}
		if (sampled_geometry) { analysis.regions = std::move(encoded); analysis.frame_by_frame = true; }
	}
	MotionApplication output;
	output.used_clippos = clippos;
	output.summary = analysis.Summary();
	if (clip) output.summary += clippos ? "; clip: clippos translation" : "; clip: ASS geometry";
	// START/END are Aegisub's subtitle-visible frame boundaries. Round common
	// split boundaries once to ASS centiseconds; original outer times survive.
	auto boundary = [&](int frame) {
		int ms = tc.TimeAtFrame(frame,agi::vfr::START);
		return std::max(0,static_cast<int>(std::floor((ms+5)/10.0))*10);
	};
	for (size_t index = 0; index < analysis.regions.size(); ++index) {
		auto const& region = analysis.regions[index];
		int start = index == 0 ? int(source.Start) : boundary(samples[region.first].frame);
		int end = index+1 == analysis.regions.size() ? int(source.End) : boundary(samples[analysis.regions[index+1].first].frame);
		if (end <= start) throw std::invalid_argument("The frame boundaries are too close for ASS centisecond event timing.");
		int t1 = static_cast<int>(samples[region.first].time)-start;
		int t2 = static_cast<int>(samples[region.last].time)-start;
		auto state = states[region.first];
		// Retime source text before inserting generated motion timing. Source
		// parts are retimed as one parsed line; generated slots cannot be shifted twice.
		// Retiming each original transform/fade happens in the literal parts.
		// Each part can contain multiple tags, but never crosses an override block.
		if (start != source.Start || end != source.End) {
			for (auto& p : state.parts) if (p.retime && !p.text.empty()) {
				std::string shifted = RetimeSource("{" + p.text + "}",start-source.Start,source.End-source.Start);
				p.text = shifted.substr(1,shifted.size()-2);
			}
		}
		AssDialogueBase event(source);
		// Keep the outer native times losslessly. The local animation clock uses
		// their normal centisecond conversion, just as the subtitle renderer does.
		event.Start = index == 0 ? source.Start : agi::Time(start);
		event.End = index+1 == analysis.regions.size() ? source.End : agi::Time(end);
		event.Text = Serialize(state,region,t1,t2);
		// Identical static sections can coalesce, but timed envelopes/transforms
		// have a different event-relative clock and therefore remain separate.
		if (o.encoding != MotionEncoding::FrameByFrame && !output.events.empty() && output.events.back().Text == event.Text &&
			output.events.back().End == event.Start && event.Text.get().find("\\t(") == std::string::npos &&
			event.Text.get().find("\\fade(") == std::string::npos)
			output.events.back().End = event.End;
		else output.events.push_back(std::move(event));
	}
	return output;
}

bool HasMotionClip(AssDialogue const& line) { return !MotionClipSignature(line).empty(); }

std::string MotionClipSignature(AssDialogue const& line) {
	std::string signature;
	for (auto const& block : line.ParseTags()) if (auto b = dynamic_cast<AssDialogueBlockOverride*>(block.get())) {
		for (auto const& tag : b->Tags) {
			if (ClipTag(tag) || tag.Name == "\\clippos" || tag.Name == "\\clips" || tag.Name == "\\movevc")
				signature += std::string(tag);
			if (tag.Name == "\\t") for (auto const& p : tag.Params)
				if (p.GetType() == VariableDataType::BLOCK && !p.omitted)
					for (auto const& nested : p.Get<AssDialogueBlockOverride*>()->Tags)
						if (ClipTag(nested) || nested.Name == "\\clippos") signature += std::string(tag);
		}
	}
	return signature;
}

MotionSourceIdentity IdentifyMotionSource(AssDialogue const& line) {
	MotionSourceIdentity identity{line.Id,line.Start.GetMilliseconds(),line.End.GetMilliseconds(),{},MotionClipSignature(line)};
	for (auto const& block : line.ParseTags()) {
		if (block->GetType() != AssBlockType::OVERRIDE) identity.content += block->GetText();
		else if (auto b = dynamic_cast<AssDialogueBlockOverride*>(block.get())) {
			for (auto const& t : b->Tags) if (t.Name == "\\p" || t.Name == "\\k" || t.Name == "\\kf" || t.Name == "\\ko" || t.Name == "\\kt")
				identity.content += std::string(t);
		}
	}
	return identity;
}

std::string ValidateMotionSources(std::vector<MotionSourceIdentity> const& sources,
	std::vector<AssDialogue const*> const& selected, int expected_active, int active, bool validate_clips) {
	if (active != expected_active) return "Active subtitle changed. Start a new tracking session.";
	if (sources.size() != selected.size()) return "Subtitle selection changed. Start a new tracking session.";
	for (auto const& source : sources) {
		auto it = std::find_if(selected.begin(),selected.end(),[&](auto p) { return p && p->Id == source.id; });
		if (it == selected.end()) return "Subtitle selection changed. Start a new tracking session.";
		auto now = IdentifyMotionSource(**it);
		if (source.start != now.start || source.end != now.end) return "Subtitle timing changed. Start a new tracking session.";
		if (source.content != now.content) return "Subtitle content changed. Start a new tracking session.";
		if (validate_clips && source.clip != now.clip) return "The tracked clip changed. Track the clip again.";
	}
	return {};
}
}
