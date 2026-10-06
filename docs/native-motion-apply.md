# Native Mangetsu motion Apply

Track → inspect → Apply keeps each selected dialogue as **one original event**,
with the same ID, native millisecond start/end, fields, folds and extradata.
The OpenCV engine, markers, forward/backward runs, handoff stitching, preview,
graphs, cleanup and AE export remain acquisition tools.

## Reference and composition

Apply reads the current **main video** frame after any Advanced Apply dialog.
That frame must have a usable tracker sample. The tracker preview never owns
the reference. Tracker coordinates convert from source-video pixels to script
pixels, then become displacement from this reference. They never replace
authored placement.

Generated position uses relative animated `\pos`, including automatic placement
resolved by Mangetsu from alignment, style/event margins and layout. Relative Y
is author-facing: up is `~+N`, down is `~-N`. Screen-space tracker Y is therefore
negated at serialization, for both `\pos` and `\clippos`.

Uniform tracker zoom multiplies authored object scale:
`authored_scale(t) * tracker_scale(t) / tracker_scale(reference)`.
Generated `\scale~+N` / `\scale~-N` values express the difference in percentage
points. Glyph axis scale, borders, shadow, blur and spacing remain authored;
Mangetsu object scale handles their local geometry.

Rotation uses explicit relative `\frz~+N` / `\frz~-N`. Native tracker rotation
is clockwise in image space, so its unwrapped reference-relative delta is
negated for ASS. Static or animated authored rotation remains in its original
tag order; generated scalar deltas add to it.

Native parsed tags evaluate object/clip scale, including absolute and relative
operands, all four transform forms, acceleration, overlapping scalar transforms
and resets. Animated zoom composition is evaluated at every integer renderer
timestamp across the event before fitting. Tracking interpolates between actual
media frame timestamps and holds boundary samples outside them. This captures
authored animation between video samples rather than assuming a product of
animations is necessarily linear.

For inline overrides, a marked inverse scalar layer removes the preceding
tracking contribution before the original overrides run; a new marked layer
composes with the resulting span state. This supports later resets and per-span
scales/rotations. Global position intents are inserted only once. Authored text
and tags are copied byte for byte.

## Simplification and Advanced Apply

The optimizer uses iterative Douglas-Peucker in actual media time with exact
shared endpoints. Default bounds are 0.35 script pixels, 0.20 percentage points,
and 0.08 rotation degrees. X/Y fit together with an axis budget whose Euclidean
error stays within the position tolerance: separate overlapping position-axis
transforms would otherwise compete in Mangetsu.

Scale, rotation, clip position and clip scale fit independently. Payloads with
identical intervals combine. Static signals contribute no transforms; exact
holds inside a moving path need no transform. Linear and move/stop combinations
collapse into long intervals. Nonlinear paths retain the intervals their bounds
require. Fits pass exactly through the reference state even on noisy tracks.
Six decimal places prevent relative interval rounding drift.

Advanced Apply exposes:

* **Automatic**: bounded fitting; difficult paths use exact sampled transform
  intervals when fitting would retain nearly every sample.
* **Force optimized**: always keep the bounded piecewise fit, without silently
  enlarging the chosen tolerances.
* **Force frame-by-frame transforms**: use consecutive video-sample transitions,
  omit no-op payloads, and still keep one dialogue event.
* X, Y, object Scale and Rotation; position/scale/rotation tolerances.
* Rectangular/vector clip applicability and a separate **Track for \clip** pass,
  the main motion track, or an unchanged clip.

Normal Apply uses automatic defaults. Mangetsu must be selected because these
are Mangetsu extensions. There is no standard-ASS splitting fallback.

## Track for \clip

A separate Track for `\clip` pass obtains the same native samples; Apply uses
relative `\clippos` for translation and `\clips` for uniform size. Rectangular/
vector `\clip` and `\iclip` remain intact, including vector drawing scales.
Existing clip offsets and clip-scale animations compose. Size is centered on
authored geometry bounds according to Mangetsu. No vector paths are rebuilt.

A standalone clip pass can Apply without an object pass. When both are present,
the selected X/Y/Scale/Rotation controls apply to both.

## Ownership, Reapply and Revert

Only complete override blocks beginning with the exact annotation
`[toshiban native motion v2]` belong to this implementation. This is a valid
Mangetsu annotation, and the payload is ordinary readable tags.

Reapply strips only owned blocks before replanning. Revert removes them in
place, preserving unrelated transforms and edits made after Apply. Markers
survive save/reload without a sidecar or original-text snapshot. Undo/redo uses
the existing native commit system.

Legacy `toshi-motion/v1` extradata families can still be reverted with the old
member-count integrity checks. Revert an old split family once before applying
the new layer. New Apply never creates v1 families.

## Explicit limits

* Authored animated position (`\move`, curved motion, transformed `\pos`) competes
  with changing tracked position under Mangetsu intent semantics. Disable X/Y
  or use static authored placement; other components still work. Stationary
  position tracking leaves these animations unchanged.
* `\clippos`/`\clips` have no rotation or anisotropic size representation.
  Nonuniform enabled zoom or changing enabled clip rotation is rejected with
  instructions to disable that component or preserve the clip.
* Animated clip geometry and `\movevc` cannot safely compose with clip state.
  Multiple legacy clip shapes are rejected because enabling clip transforms
  changes their first-vector/composition behavior to replacement semantics.
  Existing clip-transform scripts already use replacement semantics.
* Malformed/non-finite motion values, unsupported numeric spelling, malformed
  transform timing/acceleration and deeply nested effects receive an explanation.
* Explicit `\org` stays authored; Mangetsu does not animate origins. Rotation and
  zoom retain the renderer-defined pivot semantics.

## Validation and references

`tests/tests/motion_track_apply.cpp` evaluates emitted tags against an independent
numeric renderer-contract oracle and covers bounded nonlinear fits, reference
anchoring, original identity/timing, multi-selection, ownership, span resets,
scalar animation and all clip forms. Python CI contracts check the main-video
boundary, one-event commit invariant, provider requirement and UI mode wording.
Existing GitHub Actions compile and run tests; no local build is used.

Reference separation:

* [Aegisub-Motion DataHandler](https://github.com/TypesettingTools/Aegisub-Motion/blob/master/src/DataHandler.moon)
  describes acquisition/import resolution and rotation conventions.
* The supplied `toshiban_motion_track_helper (1).lua` is the behavioral baseline
  for one generated layer and relative reference-state application.
* Current Mangetsu [motion/object scale](https://github.com/amanosatosi/libassmod/blob/mangetsu/docs/motion-scale.md),
  [relative numbers](https://github.com/amanosatosi/libassmod/blob/mangetsu/docs/relative-numbers.md),
  [clip transforms](https://github.com/amanosatosi/libassmod/blob/mangetsu/docs/clip-transforms.md)
  and [parser](https://github.com/amanosatosi/libassmod/blob/mangetsu/libass/ass_parse.c)
  are the serialization authority (renderer commit
  84808c76ca6758aa9347564941d4e185da57ea0d).
