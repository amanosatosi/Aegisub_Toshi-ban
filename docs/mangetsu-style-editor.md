# Editing Mangetsu style defaults

Choose **Mangetsu** under Preferences → Advanced → Video → Subtitles provider.
The View menu then offers **Show Mangetsu Metadata**, independent of tag display
mode. It is off by default. Only recognized comments in the initial, contiguous
metadata block are hidden. Actor Colorcoding declarations, ordinary comments,
and late comments remain visible. Changing this preference changes the grid and
selection, never the ASS document.
Hidden declarations are excluded from ordinary Find/Replace and adjoining timing
operations. Sorting keeps the initial metadata header in its authored order.

The familiar Style Editor retains its content. Its single bottom row adds
**Furigana…**, **Vertical…**, **Multi-border…**, and **Wrap Mode** on the left,
with the original action buttons on the right. Other renderers hide this group.
The three property dialogs are independently resizable. **Override** distinguishes
explicit assignments from effective native/style defaults; uncheck a property to
restore inheritance. Linked axes permit common reading/border sizes. Native
border layers 1–10 are independent; a color alone never enables a layer.

Child OK only updates the parent's pending state. Parent Apply/OK commits
ordinary style fields and metadata together, in one undo step. Parent Cancel
discards unapplied changes, including accepted child edits. Wrap Mode affects
the selected style's initial event layout; it never changes Script Info WrapStyle
or inline dialogue overrides. The preview renders pending metadata, without the
ordinary preview's forced q2 override. A missing/outdated renderer displays an
error instead of presenting fallback output as a Mangetsu preview.

On load and after edits, the initial metadata block is stably ordered with
Mangetsu Style declarations first, followed by Actor Colorcoding declarations.
Their order within each type remains intact, and Actor Colorcoding retains its
renderer priority regardless of physical order. Importing styles from another
ASS file copies declarations for the selected styles when present, and imports
all Actor Colorcoding declarations, including its applied-styles whitelist.
External Style Manager catalogs store ordinary ASS fields only.

Metadata declarations retain source order within each type. Supported scalar assignments are
edited structurally; axis aliases are split only when one axis changes. Unknown
and malformed text, transforms, image tags, and manual gradients are retained.
Gradient first-valid-source precedence is not rewritten. Actor Colorcoding
declarations remain independent. Preview/render serialization sends both kinds
of metadata in their original interleaved initial-block order.
The built-in Fix Styles and Transform Framerate export filters leave this
initial header intact; subtitle preparation only reads attachments. ASS saving
and exporting serialize a style-first view of the initial metadata block.

Current-script New, Copy, Rename and Delete manage associated style declarations.
External Style Manager catalogs store ordinary ASS fields only: extended editors
are disabled there, with an explanation. Copying an extended style into a catalog
asks before copying its ordinary fields. Catalog/clipboard Style records cannot
transport these event comments; use the ASS document to retain extended data.
There is no gradient/image GUI. Manual gradient sources can take precedence over
the solid color fallback edited by Multi-border.

The definitions follow Mangetsu revision
[`014ed134c36b45f5ce2d4173964f02a221f0ad3b`](https://github.com/amanosatosi/libassmod/commit/014ed134c36b45f5ce2d4173964f02a221f0ad3b).
Native blur is capped at 100 and edge blur rounds/clamps to 0–127. Existing relative
operands are resolved for display and retained verbatim unless that field changes.

## Verification

Native tests cover target resolution, initial-block boundaries, all ten border
layers, effective defaults, numeric validation, relative operands, property
resets, copy/rename/delete, malformed/unknown text and ASS round trips. Existing
real-backend tests compare inherited furigana, borders and vertical output with
inline equivalents in RGBA and legacy image paths.

Packaged Windows CI exercises the real wxWidgets UI through public Win32 controls
and an ordinary Automation macro. It checks metadata selection safety, the single
button row, child and parent Cancel, Apply, undo/redo, style operations, saving,
and switching to libass. Dialog PNGs and the result marker are uploaded alongside
startup diagnostics. Stable wx uses light mode; wx master uses dark mode.

Manual follow-up: inspect both screenshot sets, exercise keyboard/tab navigation,
and test 125–200% Windows display scaling, nested folds around initial metadata,
very long localized labels, malformed hand-authored declarations and Burmese/
Japanese font fallback with representative installed fonts. These checks are
not substitutes for native and GUI test results.
Also export ASS with Transform Framerate enabled and confirm the initial
metadata comments remain byte-identical while ordinary event timings change.
