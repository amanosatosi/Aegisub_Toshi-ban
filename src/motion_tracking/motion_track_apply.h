#pragma once

#include "motion_track_optimizer.h"
#include "motion_track_types.h"
#include "../ass_dialogue.h"

#include <functional>
#include <string>
#include <vector>

class AssStyle;
namespace agi { namespace vfr { class Framerate; } }

namespace motion_tracking {
enum class ClipMotionSource { SeparateTrack, MainTrack, KeepUnchanged };

struct MotionApplyOptions {
	// Preserve every video-frame sample by default; simplification is opt-in in Advanced Apply.
	MotionEncoding encoding = MotionEncoding::FrameByFrame;
	MotionTolerances tolerance;
	bool position_x = true;
	bool position_y = true;
	bool scale = false;
	bool rotation = false;
	bool object_motion = true; // false for a standalone Track for \\clip pass
	ClipMotionSource clip_source = ClipMotionSource::SeparateTrack;
	bool rectangular_clips = true;
	bool vector_clips = true;
};

using MotionStyleResolver = std::function<AssStyle const*(std::string const&)>;

struct MotionApplication {
	std::vector<AssDialogueBase> events;
	std::string summary;
	bool used_clippos = false;
};

// Pure planning. Throws a short actionable reason before any subtitle mutation.
MotionApplication BuildMotionApplication(AssDialogue const& source,
	MotionStyleResolver const& styles, MotionTrackResult const& main,
	MotionTrackResult const* clip, int reference_frame,
	agi::vfr::Framerate const& timecodes, int script_width, int script_height,
	MotionApplyOptions const& options = {});

bool HasMotionClip(AssDialogue const& line);
// Acquisition needs authored clip geometry, not just clip transform tags.
bool HasTrackableMotionClip(AssDialogue const& line);
std::string MotionClipSignature(AssDialogue const& line);
// Exact owned blocks only. Original text, including user transforms, is byte-preserved.
// Start with the editor's existing Better View marker; a second annotation
// marks native-owned output so Reapply/Revert never delete legacy helper tags.
constexpr char MotionLayerMarker[] = "[toshiban motion track helper][toshiban native motion v2]";
constexpr char LegacyNativeMotionLayerMarker[] = "[toshiban native motion v2]";
std::string RemoveMotionLayers(std::string const& text);
bool HasMotionLayers(std::string const& text);

// Captures content assumptions, while allowing ordinary positioning/styling.
struct MotionSourceIdentity {
	int id = 0;
	int start = 0;
	int end = 0;
	std::string content;
	std::string clip;
};
MotionSourceIdentity IdentifyMotionSource(AssDialogue const& line);
std::string ValidateMotionSources(std::vector<MotionSourceIdentity> const& sources,
	std::vector<AssDialogue const*> const& selected, int expected_active, int active,
	bool validate_clips);
}
