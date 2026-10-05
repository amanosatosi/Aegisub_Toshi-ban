# Native motion Apply

Motion Track now applies motion directly in C++; Automation is not invoked.
The existing OpenCV engine, markers, forward/backward runs, handoff stitching,
selected range, frame cache, preview and graph remain the tracking system.

Select dialogue events, open Motion Track, place a tracker square and press
**Track Motion** (or use the existing directional tracking buttons). For an
independent mask, press **Track for \clip**, place its tracker square, and press
the same action again. Direction buttons operate on the displayed channel.
Each channel retains its own markers, runs, handoff marks, mode and result.
**Clear** clears the displayed channel; **New Session** binds the current
selection/range and clears both channels.

**Minimize** hides the modeless dialog. Reopening the existing Motion Track
command restores the same `DialogManager` instance. Neither hiding nor showing
seeks video. Position/style the line normally in the main UI, then press
**Apply**. The reference is read from `VideoController::GetFrameN()` at that
instant, never from the tracker's preview frame. Subtitle-sync seeks caused by
the generated selection are corrected back to that main playhead after commit.

## Native layers

* `motion_track_optimizer`: independent rendered-unit signals, common minimal
  breakpoints, hold detection, media-time interpolation and deterministic
  bounded-error simplification.
* `motion_track_apply`: pure event planning using `AssDialogue::ParseTags`,
  `AssOverrideTag`, native style/reset defaults and project VFR timecodes.
  Source-video pixels convert into PlayRes coordinates, with image-space
  rotations conjugated through the coordinate scaling. ASS rotation has the
  opposite sign to the tracker's clockwise image-space rotation.
* `DialogMotionTrack`: owns both tracking channels, source identity and validity,
  options and passive status; reads main reference and passes plans to commit.
* `motion_track_commit`: installs prepared event families and reverts all members
  of families selected through any generated event. The window changes selection
  before one native `AssFile::Commit` for Apply or Revert.

`ass_file_extradata.cpp` factors existing production extradata operations out of
`ass_file.cpp` so actual family installation/revert can be exercised by the
existing non-GUI gtest runner. Missing IDs no longer accidentally resolve to a
different plugin's metadata, and removal maps by ID rather than list index.

## Optimization and tags

Default absolute error bounds: 0.35 script pixels for position/clip points,
0.20 percentage points for scale, 0.08 degrees for rotation, and 0.05 pixels for
outline/shadow/blur. Completely stationary signals collapse to a hold. Holds
need at least three bounded-range samples; small steps of a slow drift do not
qualify just because individual steps are tiny. Hold endpoints become mandatory
knots, after first preferring a single accurate global linear fit. Iterative
Douglas-Peucker simplification checks each independent signal
in actual media time. A stationary axis is suppressed even while another moves.
Automatic uses exact sampled events when more than three regions and more than
one region per three samples would be necessary. Force optimized retains the
piecewise result with a 1.5x bounded tolerance. Force frame-by-frame bypasses
optimization, including merging identical events.

Translation needs only `\pos`/`\move`; defaults for scale/rotation are not
materialized for a translation-only track. Relevant numeric styling can be
animated with `\t`. Existing scalar transforms are sampled and composed with
tracking; unrelated effects retain their acceleration and are retimed after
splitting. Advanced Apply can freeze relevant source scalar animations at the
reference instead. Inline resets receive the defaults of their native style.
Position/move duplicates are normalized. Unrelated tags/text are retained.
Existing origins follow the object by default; fixed origin is an advanced choice.
Moving origins require sampled geometry because standard ASS cannot animate org.

For clips, the separate track is the normal choice. Advanced Apply can follow
the main track instead or preserve the clip. Rectangular clips can use standard
ASS transforms. Vector clip drawing coordinates retain their drawing scale and
commands. Rotated rectangles become vector polygons. Animated vector geometry
uses sampled events in standard ASS. `\iclip` stays inverse.
Only regions with moving vector geometry/origins require this fallback;
optimized holds and independent stationary signals remain suppressed/compact.

When the selected, available renderer is Mangetsu, translation-only clip motion
uses stable original `\clip`/`\iclip` geometry plus `\clippos` and `\t`.
`\clippos` is an offset, not a rotation/deformation primitive. Any actual
scale/rotation in the clip track falls back to geometry. Advanced Apply can turn
the extension off for standard ASS output. No `\distort`/`\perspective` is added.

Subtitle-visible frame bounds use the native START/END conversions. Common
split boundaries use centisecond precision; original outer times are preserved.
Interpolation uses EXACT frame timestamps, matching the main video renderer.
Events retain source fields. Identical adjacent static states merge except when
Force frame-by-frame was requested or event-relative animation prevents merging.
Native `Time::GetMilliseconds()` preserves unrounded outer timestamps and Revert
metadata without changing the established ASS centisecond rendering conversion.

## Validity and Revert

Identity checks cover active event, selected IDs, exact timing, visible/drawing
content and karaoke assumptions. Ordinary position/style edits are allowed.
Clip geometry/removal invalidates its own pass; tracking the clip again binds
the new shape while retaining main motion. Document/coordinate/timecode changes
require New Session. All required frames and the main reference must be tracked;
gaps/lost samples are never silently interpolated as trustworthy tracking.
Every rerun rebuilds result data; no stale cached application plan is used.

Native `toshi-motion/v1` ASS extradata stores a random family token, original
event data, exact milliseconds, member count and original extradata values.
Revert restores the original event and removes the full generated family,
preserving unrelated events and metadata. Values survive native extradata
garbage collection and save/reload; no identifier is placed in visible text.
Incomplete/duplicated families reject Revert rather than deleting ambiguous
members. Native undo/redo separately retains its existing event/selection history.

## Current limits compared with Aegisub-Motion

There is no imported AE/SRS dataset application, absolute-position mode or
arbitrary shape deformation. Source Mangetsu curved motion and position/origin
transforms require normalization before Apply. Native tracking produces translation, isotropic
scale and image rotation; Apply supports X/Y scale signals if provided by the
model. Source animated clips cannot yet be composed with an independent clip
track; existing animated clip offsets must be removed first. Karaoke and colored
Mangetsu fades requiring event splitting are rejected with a short reason;
ordinary fades are retimed as explicit envelopes. Moving origins and animated
vector geometry cannot stay compact under standard ASS. Very short frame periods
which cannot form positive centisecond event ranges reject splitting. Perspective
tools stay separate.

`tests/tests/motion_track_apply.cpp` covers synthetic optimizer behavior, both
clip paths, source animation/reset/fade preservation, VFR boundaries, stale
identity checks, production extradata and whole-family Revert. Build/tests run
through the existing GitHub Actions Meson CI; no local compilation is required.

Reference inspection: [Aegisub-Motion at 897cd7f](https://github.com/TypesettingTools/Aegisub-Motion/tree/897cd7f6a63844849a9c566d0df337b3ba057091),
especially `MotionHandler.moon`, `DataHandler.moon`, `Line.moon`, `Transform.moon`,
and the main script's preprocessing/postprocessing and revert processors.
Mangetsu clip offset behavior was checked in its native
[parser](https://github.com/amanosatosi/libassmod/blob/mangetsu/libass/ass_parse.c)
and [renderer](https://github.com/amanosatosi/libassmod/blob/mangetsu/libass/ass_render.c).
