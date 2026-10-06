#pragma once

#include "motion_track_apply.h"

class AssFile;

namespace motion_tracking {
constexpr char MotionFamilyKey[] = "toshi-motion/v1";

struct MotionPlannedSource {
	int source_id;
	MotionApplication application;
};

struct MotionCommitSelection {
	std::vector<AssDialogue*> selected;
	AssDialogue* active = nullptr;
};

// All plans/metadata are validated before mutation. The caller updates selection
// before making exactly one AssFile::Commit. New applications edit text in place;
// legacy v1 split-family metadata is supported only for migration via Revert.
MotionCommitSelection InstallMotionApplications(AssFile& file,
	std::vector<MotionPlannedSource> const& plans, int active_source, int reference_time = -1);
MotionCommitSelection RevertMotionFamilies(AssFile& file,
	std::vector<AssDialogue*> const& selected, int active_id);
bool CanRevertMotion(AssFile const& file, AssDialogue const& line);
}
