#include "motion_track_apply.h"

#include "../ass_style.h"
#include <libaegisub/vfr.h>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <map>
#include <sstream>
#include <stdexcept>

namespace motion_tracking {
namespace {
bool ClipTag(AssOverrideTag const& t) { return t.Name == "\\clip" || t.Name == "\\iclip"; }

std::string Number(double v) {
	if (!std::isfinite(v)) throw std::invalid_argument("Motion contains a non-finite value.");
	if (std::abs(v) < 0.0000005) v = 0;
	std::ostringstream s;
	s.imbue(std::locale::classic());
	// Extra precision prevents accumulated rounding across relative intervals.
	s << std::fixed << std::setprecision(6) << v;
	auto text = s.str();
	while (text.back() == '0') text.pop_back();
	if (text.back() == '.') text.pop_back();
	return text;
}
std::string Relative(double v) { return std::string(v < 0 ? "~-" : "~+") + Number(std::abs(v)); }

MotionTrackFrame const& Sample(MotionTrackResult const& track, int frame) {
	auto it = std::lower_bound(track.frames.begin(),track.frames.end(),frame,
		[](auto const& a,int b) { return a.frame < b; });
	if (it == track.frames.end() || it->frame != frame ||
		(it->state != MotionTrackState::Tracked && it->state != MotionTrackState::WeakTracked &&
		 it->state != MotionTrackState::Predicted))
		throw std::invalid_argument("Track every selected subtitle frame and the current main-video reference frame before Apply.");
	if (!std::isfinite(it->x) || !std::isfinite(it->y) || !std::isfinite(it->scale_x) ||
		!std::isfinite(it->scale_y) || !std::isfinite(it->rotation_deg) || it->scale_x <= 0 || it->scale_y <= 0)
		throw std::invalid_argument("Tracking contains an invalid sample.");
	return *it;
}

MotionTrackResult PrepareTrack(MotionTrackResult const& input) {
	auto track = input;
	if (track.source_width <= 0 || track.source_height <= 0)
		throw std::invalid_argument("Tracking has no source video resolution.");
	for (size_t i = 0; i < track.frames.size(); ++i) {
		if (i && track.frames[i].frame <= track.frames[i-1].frame)
			throw std::invalid_argument("Tracking samples must be ordered without duplicate frames.");
		if (!std::isfinite(track.frames[i].rotation_deg))
			throw std::invalid_argument("Tracking contains a non-finite angle.");
		if (i) {
			// Both wrapped and already unwrapped tracker angles are supported.
			double delta = std::remainder(input.frames[i].rotation_deg-input.frames[i-1].rotation_deg,360.0);
			track.frames[i].rotation_deg = track.frames[i-1].rotation_deg+delta;
		}
	}
	return track;
}

struct Scalars {
	double scale = 100, rotation = 0, clip_x = 0, clip_y = 0, clips = 100;
	AssStyle const* style = nullptr;
};

// Do not use Get<double>() for relative operands: Aegisub's legacy accessor
// calls atof and would interpret "~+20" as zero.
double Operand(AssOverrideTag const& tag, size_t index, double current, bool nonnegative, bool y = false) {
	if (index >= tag.Params.size() || tag.Params[index].omitted)
		throw std::invalid_argument("An authored motion tag has missing parameters.");
	std::string token = tag.Params[index].Get<std::string>();
	// Mangetsu permits bracket annotations and whitespace in override blocks.
	for (size_t p = token.find('['); p != std::string::npos; p = token.find('[')) {
		auto end = token.find(']',p);
		if (end == std::string::npos) throw std::invalid_argument("An authored motion value has an unclosed annotation.");
		token.erase(p,end-p+1);
	}
	auto begin = token.find_first_not_of(" \t\r\n");
	if (begin == std::string::npos) throw std::invalid_argument("An authored motion value is empty.");
	token.erase(0,begin);
	bool relative = token[0] == '~';
	if (relative) {
		token.erase(0,1);
		if (token.empty() || (token[0] != '+' && token[0] != '-'))
			throw std::invalid_argument("An authored relative motion value needs ~+N or ~-N.");
	}
	else relative = nonnegative && (token[0] == '+' || token[0] == '-');
	std::istringstream stream(token);
	stream.imbue(std::locale::classic());
	double value;
	if (!(stream >> value) || !std::isfinite(value))
		throw std::invalid_argument("An authored motion value is not a finite number.");
	stream >> std::ws;
	if (!stream.eof()) throw std::invalid_argument("An authored motion value cannot be composed safely.");
	return relative ? current + (y ? -value : value) : value;
}

void Evaluate(AssDialogueBlockOverride const& block, Scalars& state, double time,
	double duration, MotionStyleResolver const& styles, bool object_scale, bool clip_scale, double weight = 1, int depth = 0) {
	if (depth > 16) throw std::invalid_argument("Authored motion transforms are nested too deeply.");
	for (auto const& t : block.Tags) {
		if (t.Name == "\\t") {
			if (t.Params.size() != 4 || t.Params[3].omitted)
				throw std::invalid_argument("An authored transform is malformed.");
			double start = t.Params[0].Get<double>(0), end = t.Params[1].Get<double>(duration);
			if (end == 0) end = duration; // renderer convention
			double accel = t.Params[2].Get<double>(1);
			if (!std::isfinite(accel) || accel <= 0 || end < start)
				throw std::invalid_argument("An authored transform has unsupported timing or acceleration.");
			double p = end == start ? (time >= end ? 1 : 0) : std::clamp((time-start)/(end-start),0.0,1.0);
			// Match Mangetsu: only a terminal nested transform is evaluated,
			// and its own weight replaces the enclosing weight.
			if (depth && &t != &block.Tags.back()) continue;
			Evaluate(*t.Params[3].Get<AssDialogueBlockOverride*>(),state,time,duration,styles,
				object_scale,clip_scale,std::pow(p,accel),depth+1);
		}
		else if (t.Name == "\\r") {
			auto name = t.Params[0].Get<std::string>("");
			auto style = name.empty() ? state.style : styles(name);
			if (!style) style = state.style;
			state.scale = state.clips = 100;
			state.clip_x = state.clip_y = 0;
			state.rotation = style ? style->angle : 0;
		}
		else {
			auto update = [&](double& current, bool nonnegative, double fallback) {
				double target = t.Params.empty() || t.Params[0].omitted ? fallback : Operand(t,0,current,nonnegative);
				if (nonnegative) target = std::max(0.0,target);
				current += (target-current)*weight;
			};
			if (object_scale && t.Name == "\\scale") update(state.scale,true,100);
			else if (clip_scale && t.Name == "\\clips") {
				double target = Operand(t,0,state.clips,false);
				if (target >= 0) state.clips += (target-state.clips)*weight;
			}
		}
	}
}

void Inspect(AssDialogueBlockOverride const& block, bool moving_position, bool clip,
	bool& animated_scalar, int& shapes, bool& authored_clip_state, int depth = 0) {
	if (depth > 16) throw std::invalid_argument("Authored transforms are nested too deeply.");
	for (auto const& t : block.Tags) {
		if (t.Name == "\\t") {
			if (t.Params.size() != 4 || t.Params[3].omitted) throw std::invalid_argument("An authored transform is malformed.");
			Inspect(*t.Params[3].Get<AssDialogueBlockOverride*>(),moving_position,clip,animated_scalar,shapes,authored_clip_state,depth+1);
		}
		else {
			if (moving_position && ((depth && t.Name == "\\pos") || t.Name == "\\move" ||
				t.Name == "\\mover" || t.Name == "\\moves3" || t.Name == "\\moves4"))
				throw std::invalid_argument("Authored animated position competes with tracked position in Mangetsu. Disable X/Y in Advanced Apply, or use a static authored position.");
			if (depth && (t.Name == "\\scale" || t.Name == "\\clips")) animated_scalar = true;
			if (ClipTag(t)) {
				++shapes;
				if (clip && depth) throw std::invalid_argument("Animated clip geometry cannot safely compose with Track for \\clip. Keep the clip unchanged in Advanced Apply.");
			}
			authored_clip_state |= t.Name == "\\clippos" || t.Name == "\\clips";
			if (clip && t.Name == "\\movevc")
				throw std::invalid_argument("Authored \\movevc cannot compose with clip transforms. Keep the clip unchanged in Advanced Apply.");
		}
	}
}

struct Signal {
	std::string tag;
	std::vector<MotionSample> samples;
	std::vector<double> tolerance;
};

std::string Payload(std::string const& tag, std::vector<double> const& delta, double sign) {
	bool nonzero = false;
	for (double v : delta) nonzero |= std::abs(v) >= 0.0000005;
	if (!nonzero) return {};
	if (delta.size() == 2)
		// Tracker Y is down-positive; relative Mangetsu Y is up-positive.
		return tag + "(" + Relative(sign*delta[0]) + "," + Relative(-sign*delta[1]) + ")";
	return tag + Relative(sign*delta[0]);
}

// A single signal owns non-overlapping intervals. Position axes share intervals
// because separate overlapping pos intents compete even on disjoint axes.
// Other properties optimize independently, then identical intervals combine.
std::string Layer(std::vector<Signal> const& signals, MotionEncoding encoding, double sign,
	size_t& transform_count) {
	std::string base;
	std::map<std::pair<int,int>,std::string> intervals;
	for (auto const& signal : signals) {
		if (signal.samples.empty()) continue;
		auto samples = signal.samples;
		bool zero = true;
		for (auto const& s : samples) for (size_t k = 0; k < s.values.size(); ++k)
			zero &= std::abs(s.values[k]) < signal.tolerance[k];
		if (zero && encoding != MotionEncoding::FrameByFrame) continue;
		auto analysis = OptimizeMotion(samples,signal.tolerance,encoding);
		base += Payload(signal.tag,samples.front().values,sign);
		for (auto const& r : analysis.regions) {
			std::vector<double> delta(r.from.size());
			for (size_t k = 0; k < delta.size(); ++k) delta[k] = r.to[k]-r.from[k];
			auto payload = Payload(signal.tag,delta,sign);
			if (!payload.empty())
				intervals[{int(samples[r.first].time),int(samples[r.last].time)}] += payload;
		}
	}
	for (auto const& interval : intervals) {
		if (interval.first.first == interval.first.second) continue;
		base += "\\t(" + std::to_string(interval.first.first) + "," +
			std::to_string(interval.first.second) + "," + interval.second + ")";
		++transform_count;
	}
	return base.empty() ? std::string() : "{" + std::string(MotionLayerMarker) + base + "}";
}

struct Group {
	size_t start = 0, end = 0;
	std::vector<std::unique_ptr<AssDialogueBlock>> blocks;
};

// Preserve original bytes instead of serializing parsed user tags back to text.
std::vector<Group> Groups(std::string const& text) {
	std::vector<Group> groups;
	if (text.empty() || text[0] != '{') groups.emplace_back();
	for (size_t p = 0; p < text.size();) {
		if (text[p] != '{') { ++p; continue; }
		size_t start = p;
		do {
			auto end = text.find('}',p);
			if (end == std::string::npos) { p = text.size(); break; }
			p = end+1;
		} while (p < text.size() && text[p] == '{');
		if (text[p-1] != '}') break;
		AssDialogue source;
		source.Text = text.substr(start,p-start);
		groups.push_back({start,p,source.ParseTags()});
	}
	if (groups.empty()) groups.emplace_back();
	return groups;
}
}

std::string RemoveMotionLayers(std::string const& text) {
	std::string result;
	size_t copied = 0;
	for (size_t p = 0; p < text.size();) {
		auto open = text.find('{',p);
		if (open == std::string::npos) break;
		auto close = text.find('}',open+1);
		if (close == std::string::npos) break;
		if (text.compare(open+1,sizeof(MotionLayerMarker)-1,MotionLayerMarker) == 0) {
			result += text.substr(copied,open-copied);
			copied = close+1;
		}
		p = close+1;
	}
	return result + text.substr(copied);
}
bool HasMotionLayers(std::string const& text) { return RemoveMotionLayers(text) != text; }

MotionApplication BuildMotionApplication(AssDialogue const& input, MotionStyleResolver const& styles,
	MotionTrackResult const& input_main, MotionTrackResult const* input_clip, int reference,
	agi::vfr::Framerate const& tc, int width, int height, MotionApplyOptions const& o) {
	AssDialogue source(input);
	source.Text = RemoveMotionLayers(input.Text.get());
	if (!tc.IsLoaded() || width <= 0 || height <= 0 || source.End <= source.Start || source.Comment)
		throw std::invalid_argument("Select positive-duration dialogue events with loaded video timing and script resolution.");
	int first = tc.FrameAtTime(source.Start,agi::vfr::START), last = tc.FrameAtTime(source.End,agi::vfr::END);
	if (last < first) throw std::invalid_argument("The subtitle does not cover a video frame.");
	bool want_main = o.object_motion && (o.position_x || o.position_y || o.scale || o.rotation);
	MotionTrackResult main = want_main ? PrepareTrack(input_main) : MotionTrackResult{};
	MotionTrackResult clip;
	auto clip_input = o.clip_source == ClipMotionSource::MainTrack ? &input_main :
		o.clip_source == ClipMotionSource::SeparateTrack ? input_clip : nullptr;
	if (clip_input && HasMotionClip(source)) clip = PrepareTrack(*clip_input);
	bool want_clip = !clip.frames.empty();
	auto groups = Groups(source.Text.get());
	bool moving_position = false;
	if (want_main && (o.position_x || o.position_y)) {
		auto const& r = Sample(main,reference);
		for (int f = first; f <= last; ++f) {
			auto const& s = Sample(main,f);
			moving_position |= (o.position_x && std::abs(s.x-r.x) > 0.0000001) || (o.position_y && std::abs(s.y-r.y) > 0.0000001);
		}
	}
	bool animated_scalar = false, authored_clip_state = false;
	int shapes = 0;
	for (auto const& group : groups) for (auto const& block : group.blocks)
		if (auto b = dynamic_cast<AssDialogueBlockOverride const*>(block.get()))
			Inspect(*b,moving_position,want_clip,animated_scalar,shapes,authored_clip_state);
	if (want_clip && shapes > 1 && !authored_clip_state)
		throw std::invalid_argument("Multiple authored clip shapes use legacy composition. Select one clip shape, or keep clips unchanged in Advanced Apply.");
	if (want_clip && ((!o.rectangular_clips || !o.vector_clips))) {
		for (auto const& group : groups) for (auto const& block : group.blocks)
			if (auto b = dynamic_cast<AssDialogueBlockOverride const*>(block.get()))
				for (auto const& t : b->Tags) if (ClipTag(t) &&
					((t.Params.size() == 4 && !o.rectangular_clips) || (t.Params.size() == 2 && !o.vector_clips)))
					want_clip = false;
	}
	auto validate = [&](MotionTrackResult const& track, bool zoom, bool clip_rotation) {
		auto const& r = Sample(track,reference);
		for (int f = first; f <= last; ++f) {
			auto const& s = Sample(track,f);
			if (zoom && std::abs(s.scale_x/r.scale_x-s.scale_y/r.scale_y) > 0.000001)
				throw std::invalid_argument("Object/clip zoom must be uniform. Disable Scale in Advanced Apply for an anisotropic track.");
			if (clip_rotation && std::abs(s.rotation_deg-r.rotation_deg) > 0.000001)
				throw std::invalid_argument("Mangetsu clippos/clips cannot rotate clip geometry. Disable Rotation in Advanced Apply, or keep the clip unchanged.");
		}
	};
	if (want_main) validate(main,o.scale,false);
	if (want_clip) validate(clip,o.scale,o.rotation);

	// Integer media timestamps are the renderer's sampling clock. For animated
	// scale composition inspect every millisecond, including original transform
	// boundaries between video samples, rather than fitting only their product
	// at frame endpoints. Forced frame mode intentionally uses video samples.
	std::vector<int> times;
	int start_time = tc.TimeAtFrame(first), end_time = tc.TimeAtFrame(last);
	if (animated_scalar && o.encoding != MotionEncoding::FrameByFrame && (o.scale || want_clip))
		for (int t = std::min(start_time,int(source.Start)); t <= std::max(end_time,int(source.End)); ++t) times.push_back(t);
	else for (int f = first; f <= last; ++f) times.push_back(tc.TimeAtFrame(f));
	auto at = [&](MotionTrackResult const& track,int t) {
		int f = std::clamp(tc.FrameAtTime(t),first,last);
		auto a = Sample(track,f);
		if (f < last) {
			auto const& b = Sample(track,f+1);
				double p = std::clamp(double(t-tc.TimeAtFrame(f))/(tc.TimeAtFrame(f+1)-tc.TimeAtFrame(f)),0.0,1.0);
			a.x += p*(b.x-a.x); a.y += p*(b.y-a.y);
			a.scale_x += p*(b.scale_x-a.scale_x); a.scale_y += p*(b.scale_y-a.scale_y);
			a.rotation_deg += p*(b.rotation_deg-a.rotation_deg);
		}
		return a;
	};
	MotionTrackFrame main_ref, clip_ref;
	if (want_main) main_ref = Sample(main,reference);
	if (want_clip) clip_ref = Sample(clip,reference);
	std::map<size_t,std::string> insertions;
	std::vector<Signal> previous;
	size_t count = 0;
	for (size_t g = 0; g < groups.size(); ++g) {
		if (g) insertions[groups[g].start] += Layer(previous,o.encoding,-1,count);
		std::vector<Signal> local;
		auto make = [&](std::string tag,std::vector<double> tol) {
			local.push_back({std::move(tag),{},std::move(tol)});
			return local.size()-1;
		};
		size_t pos = make("\\pos",{o.tolerance.position/std::sqrt(2.0),o.tolerance.position/std::sqrt(2.0)});
		size_t scale = make("\\scale",{o.tolerance.scale});
		size_t rotation = make("\\frz",{o.tolerance.rotation});
		size_t cp = make("\\clippos",{o.tolerance.position/std::sqrt(2.0),o.tolerance.position/std::sqrt(2.0)});
		size_t cs = make("\\clips",{o.tolerance.scale});
		for (size_t i = 0; i < times.size(); ++i) {
			int t = times[i];
			double time = t-int(source.Start), duration = int(source.End)-int(source.Start);
			MotionTrackFrame main_sample, clip_sample;
			if (want_main) main_sample = at(main,t);
			if (want_clip) clip_sample = at(clip,t);
			bool object_scale = want_main && o.scale && std::abs(main_sample.scale_x/main_ref.scale_x-1) > 0.0000001;
			bool clip_scale = want_clip && o.scale && std::abs(clip_sample.scale_x/clip_ref.scale_x-1) > 0.0000001;
			Scalars state;
			state.style = styles(source.Style.get());
			state.rotation = state.style ? state.style->angle : 0;
			if (object_scale || clip_scale) for (size_t k = 0; k <= g; ++k) for (auto const& block : groups[k].blocks)
				if (auto b = dynamic_cast<AssDialogueBlockOverride const*>(block.get()))
					Evaluate(*b,state,time,duration,styles,object_scale,clip_scale);
			auto add = [&](size_t index,std::vector<double> value) {
				local[index].samples.push_back({int(i),time,std::move(value),t == tc.TimeAtFrame(reference)});
			};
			if (want_main) {
				auto s = main_sample;
				if (g == 0 && (o.position_x || o.position_y))
					add(pos,{o.position_x ? (s.x-main_ref.x)*width/main.source_width : 0,
						o.position_y ? (s.y-main_ref.y)*height/main.source_height : 0});
				if (o.scale) add(scale,{state.scale*(s.scale_x/main_ref.scale_x-1)});
				if (o.rotation) add(rotation,{-(s.rotation_deg-main_ref.rotation_deg)});
			}
			if (want_clip) {
				auto s = clip_sample;
				add(cp,{o.position_x ? (s.x-clip_ref.x)*width/clip.source_width : 0,
					o.position_y ? (s.y-clip_ref.y)*height/clip.source_height : 0});
				if (o.scale) add(cs,{state.clips*(s.scale_x/clip_ref.scale_x-1)});
			}
		}
		// Do not cancel/repeat global position intents at inline scalar resets.
		insertions[groups[g].end] += Layer(local,o.encoding,1,count);
		local.erase(local.begin());
		previous = std::move(local);
	}
	std::string text;
	size_t cursor = 0;
	for (auto const& item : insertions) {
		text += source.Text.get().substr(cursor,item.first-cursor) + item.second;
		cursor = item.first;
	}
	text += source.Text.get().substr(cursor);
	AssDialogueBase event(input);
	event.Text = text;
	MotionApplication output;
	output.events.push_back(std::move(event)); // exactly one, same identity and timing
	output.used_clippos = want_clip;
	output.summary = std::to_string(count) + (o.encoding == MotionEncoding::FrameByFrame ?
		" frame-by-frame transforms; one event per subtitle" : " optimized transforms; one event per subtitle");
	if (want_clip) output.summary += "; clippos/clips";
	return output;
}

bool HasMotionClip(AssDialogue const& line) { return !MotionClipSignature(line).empty(); }
bool HasTrackableMotionClip(AssDialogue const& input) {
	AssDialogue line(input);
	line.Text = RemoveMotionLayers(input.Text.get());
	for (auto const& block : line.ParseTags()) if (auto b = dynamic_cast<AssDialogueBlockOverride const*>(block.get())) {
		for (auto const& tag : b->Tags) {
			if (!ClipTag(tag) || !tag.IsValid()) continue;
			try {
				if (tag.Params.size() == 4) {
					for (size_t i = 0; i < 4; ++i) Operand(tag,i,0,false);
					return true;
				}
				if (tag.Params.size() == 2 && !tag.Params[1].omitted &&
					tag.Params[1].Get<std::string>().find_first_not_of(" \t\r\n") != std::string::npos &&
					(tag.Params[0].omitted || Operand(tag,0,0,false) > 0)) return true;
			}
			catch (std::invalid_argument const&) { }
		}
	}
	return false;
}

std::string MotionClipSignature(AssDialogue const& input) {
	AssDialogue line(input);
	line.Text = RemoveMotionLayers(input.Text.get());
	std::string signature;
	for (auto const& block : line.ParseTags()) if (auto b = dynamic_cast<AssDialogueBlockOverride const*>(block.get())) {
		for (auto const& tag : b->Tags) {
			if (ClipTag(tag) || tag.Name == "\\clippos" || tag.Name == "\\clips" || tag.Name == "\\movevc")
				signature += std::string(tag);
			if (tag.Name == "\\t") for (auto const& p : tag.Params)
				if (p.GetType() == VariableDataType::BLOCK && !p.omitted)
					for (auto const& nested : p.Get<AssDialogueBlockOverride*>()->Tags)
						if (ClipTag(nested) || nested.Name == "\\clippos" || nested.Name == "\\clips")
							signature += std::string(tag);
		}
	}
	return signature;
}

MotionSourceIdentity IdentifyMotionSource(AssDialogue const& input) {
	AssDialogue line(input);
	line.Text = RemoveMotionLayers(input.Text.get());
	// AssDialogue's copy constructor creates a new ID for temporary copies.
	// Session identity must belong to the original selected event.
	MotionSourceIdentity identity{input.Id,line.Start.GetMilliseconds(),line.End.GetMilliseconds(),{},MotionClipSignature(line)};
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
