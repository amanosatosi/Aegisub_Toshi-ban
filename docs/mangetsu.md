# Mangetsu Subtitle Renderer

Mangetsu support is implemented as a separate subtitle provider named `Mangetsu`.
It reuses the libassmod-compatible render path, including RGBA rendering and tag
image support, but it is loaded from its own shared library.

The currently supported Mangetsu source is:

https://github.com/amanosatosi/libassmod/tree/mangetsu

## Build And Package

Build/package the `mangetsu` branch of `libassmod` as a shared library, then
place the output beside `aegisub.exe` or the installed Aegisub binary as:

- Windows: `mangetsu.dll`
- Linux: `libmangetsu.so`
- macOS, if supported: `libmangetsu.dylib`

Configure Aegisub packaging with:

```sh
meson setup build -Dwith_mangetsu=true
```

When `mangetsu_path` is omitted, Meson builds the `mangetsu` branch from
`subprojects/libassmod-mangetsu.wrap` and installs it under the platform name
above. Release Windows installer and portable builds enable this, so
`mangetsu.dll` is bundled in shipped artifacts.

To package a prebuilt external library instead, use:

```sh
meson setup build -Dwith_mangetsu=true -Dmangetsu_path=/path/to/mangetsu.dll
```

Use the platform-appropriate path for `mangetsu_path`. Without
`-Dwith_mangetsu=true`, Aegisub still builds and runs, and the Mangetsu provider
is simply unavailable unless a matching library is placed beside the executable
by other packaging.

The libassmod provider is intentionally separate. It only probes:

- Windows: `libassmod.dll`, `assmod.dll`
- Linux: `libassmod.so`
- macOS: `libassmod.dylib`, `libassmod.so`

It does not load plain `ass.dll`, `libass.dll`, `libass.so`, or `libass.dylib`
as libassmod. Built-in `libass` is the fallback provider.

On Windows, libassmod is packaged as `libassmod.dll`. The old `ass.dll` and
`libass.dll` names are obsolete for this fork. Installer upgrades delete those
stale names from the install directory, and the uninstaller removes them as
cleanup in case they came from older Aegisub_Toshi-ban builds.

## Provider UI Behavior

The current preferences UI uses a simple read-only dropdown that cannot disable
individual items. Available subtitle providers appear in the dropdown. Missing
optional providers are shown in an `Unavailable subtitles providers` text
section below it.

A later UI improvement can replace this with real disabled/gray dropdown items.

## Video visual tools

### Curved Text (`\ct`)

The **Curved Text** video tool edits Mangetsu text-on-path data while the normal
subtitle renderer remains responsible for shaping and preview. It does not split
text into characters or create a second fake text renderer, so complex scripts
such as Burmese remain unchanged.

The default **Edit Path** mode exposes anchors, cubic Bezier controls and their
control lines for `m`, `l` and `b` path commands. **Move Whole Path** translates
all local path coordinates without changing `\pos` or `\move`. The other two
modes edit `\ctx` along-path distance and perpendicular `\cty` offset. The
compact alignment button cycles explicit `\ctan1`, `\ctan2` and `\ctan3`; when
`\ctan` is absent, the renderer default derived from horizontal `\an` is shown
without inserting a tag.

`\ct` coordinates are local to the subtitle anchor. A `\pos` or frame-evaluated
`\move` therefore moves the overlay with the subtitle without rewriting every
path point. Lines without `\ct` show a provisional baseline sized from the line;
opening the tool makes no subtitle change, and the tag is inserted only by the
first edit. Unknown or malformed path commands are left untouched rather than
being partially normalized. Static tags are edited; `\ct` inside `\t(...)` is
not an editing target.

### Mangetsu Perspective (`\perspective`)

The Perspective visual tool now has three explicit, non-destructive submodes:

- **Perspective** (default) edits Mangetsu's true projective
  `\perspective(x0,y0,x1,y1,x2,y2,x3,y3)` corner pin.
- **Distort** edits Mangetsu's separate bilinear `\distort` deformation.
- **arch1t3ct** keeps the existing ASS rotation/shear/scale approximation.

Perspective uses P0 top-left, P1 top-right, P2 bottom-right, and P3 bottom-left,
exactly matching the renderer. Existing tags are read without conversion when
switching modes. Selecting the mode is read-only; a line with no tag shows a
provisional identity quad and receives `\perspective` only on the first corner
edit. The solid quad distinguishes this mode from Distort's dashed quad and
from the arch1t3ct compatibility controls.

The triangle is the positioning-anchor handle. Dragging it updates `\pos`, or
translates both endpoints of an existing `\move`, while leaving all eight
perspective coordinates unchanged. A drag uses the visual-tool base class's
single coalesced undo transaction.

Existing tags use their literal local-to-anchor coordinates, so their overlay
handles agree exactly with Mangetsu. For a new tag, the provisional source
plane uses Aegisub's full-event metric/drawing extents, excludes
border/blur/shadow, applies the visual tool's normal ASS local transform, and
includes an active Distort warp before taking the source AABB. This is a close
authoring approximation: Aegisub does not currently expose libassmod's exact
post-shaping outline/control-point bounds to visual tools, and mixed-style
events with multiple independently distorted runs may differ until a corner is
placed. The video subtitle itself always comes from the selected real renderer;
the overlay draws only handles and guides.

### Mangetsu Distort (`\distort`)

The **Mangetsu Distort** sub-mode edits the renderer's bilinear artistic
deformation. It is not true projective perspective; the separate Perspective
mode authors `\perspective`, while arch1t3ct remains available with its existing
ASS-tag approximation.

The distortion source bounds now follow the first compactable same-style visual
run across ordinary spaces, consecutive spaces and NBSP. Whitespace advances
following glyphs but contributes no outline points of its own. Hard line breaks,
effective style changes, `\distort` changes and vector-drawing chunks end the
run. Both the historical six-argument form (implicit P0 `(0,0)`) and Mangetsu's
eight-argument form are preserved. Editing P1/P2/P3 does not expand a legacy tag;
editing P0 is the operation which requires the extended form.

Distort mode shows four corner pins plus the restored triangle position handle.
Dragging a corner edits only `\distort`; dragging the triangle translates
`\pos`, or both endpoints of an existing `\move`, while leaving the normalized
distortion values unchanged.

## Manual Checks

- Select `Mangetsu` in the config while `mangetsu.dll`/`libmangetsu.so` is
  missing; Aegisub should warn once and use `libass`.
- Select `libassmod` while `libassmod.dll`/`libassmod.so` is missing; Aegisub
  should warn once and use `libass`.
- Place only `libass.dll`/`libass.so` beside Aegisub; `Mangetsu` must remain
  unavailable.
- Place only `ass.dll`/`libass.so` beside Aegisub; `libassmod` must remain
  unavailable unless `assmod.dll` or `libassmod.dll`/`libassmod.so` is present.
- When Mangetsu is available, the log should include `Mangetsu loaded from
  <path>`.

## Gradient Editor: fixed video placement

The Mangetsu Gradient Editor has a **Lock Placement** button beside its angle
controls. It changes a primary-fill attached gradient into a fixed-frame
gradient. The angle still comes from the editor's angle control and the colors
and percentage stops still come from the stop editor; only the coordinate space
changes.

Workflow:

```text
1. Open Gradient Editor.
2. Configure the gradient angle and stops.
3. Enable Lock Placement.
4. Drag the desired area on the video.
5. Apply the change.
```

Dragging creates `\pgrd(left,top,right,bottom,angle,stops...)`. The editor
always normalizes the rectangle to `left,top,right,bottom`, stores it in ASS
script coordinates, and keeps it fixed as the video frame changes. It does not
follow `\pos`, `\move`, scaling, or rotation. Pixels outside the rectangle use
the line's normal primary color.

Current Mangetsu support is intentionally limited to the primary RGB fill.
`\pgrd(...)` and `\1pgrd(...)` are equivalent primary-fill aliases; the editor
generates the compact `\pgrd(...)` form. Placement is unavailable for
secondary, outline, shadow, fifth-color, alpha, border, and box gradients. The
button explains this when one of those channels is selected.

Opening an existing placement gradient restores its rectangle, angle, and
stops. Drag once more to replace the rectangle. Disabling **Lock Placement**
converts it back to the regular attached `\1grd(angle,stops...)` form while
preserving its angle and stops. The rectangle remains cached only for the open
editor session, so toggling it back on can restore the area; it is never stored
in an attached-gradient tag.

The video overlay is available only while a video is loaded. Without one, the
editor can still inspect a placement gradient and edit its angle and stops, but
cannot capture a new rectangle. If the active line's Effect field is `LOCK`, no
placement capture starts. A placement drag also ends safely if the active line
or visual tool changes, the video closes, or Escape/lost mouse capture cancels
the unfinished drag. The current Gradient Editor targets static tags; when the
target gradient is inside `\t(...)`, **Lock Placement** is disabled instead of
inserting a separate static placement tag.
