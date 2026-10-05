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
enum class ClipMotionSource { SeparateTrack, MainTrack, None };

struct MotionApplyOptions {
	MotionEncoding encoding = MotionEncoding::Automatic;
	MotionTolerances tolerance;
	bool position_x = true;
	bool position_y = true;
	bool scale = true;
	bool rotation = true;
	bool follow_origin = true;
	bool border = true;
	bool shadow = true;
	bool blur = true;
	bool preserve_transforms = true;
	ClipMotionSource clip_source = ClipMotionSource::SeparateTrack;
	bool rectangular_clips = true;
	bool vector_clips = true;
	bool mangetsu_clippos = false;
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
std::string MotionClipSignature(AssDialogue const& line);

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
