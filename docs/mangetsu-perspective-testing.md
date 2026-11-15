# Testing Mangetsu Perspective in Toshi-ban

Use a Mangetsu shared library built from the perspective-enabled development branch.
The existing Meson options accept a prebuilt renderer for local tests and portable
packages.

    meson setup build-perspective -Dwith_mangetsu=true -Dmangetsu_path=<path-to-library>
    meson compile -C build-perspective

Select **Mangetsu** as the subtitle provider and select **Perspective → Mangetsu
Perspective** on the video toolbar. Open Mangetsu's `samples/perspective.ass`
with a matching 1280×720 video or dummy video. Use the Japanese and Myanmar
fonts named in the sample or select fonts installed on the test machine.

The overlay gets its source rectangle and evaluated corners from the same
render call that paints the preview. The tool appears when that renderer
provides the perspective layout API and a visible event has nonzero layout
dimensions. Each drag writes the event's eight relative `\mpersp` values. Undo,
redo and ASS save use the normal subtitle text commands. Existing `\distort`
tags keep their original values and rendering.

For a side-by-side check, compare the same frame dimensions, time, fonts,
shaping settings and renderer build on both sides. Try widening the text,
changing its font size and adding a hard line break after dragging a corner.
The relative pins should stay fixed in the tag as the handles move with
the resized layout. Animated pins are displayed at the current frame and
edited as ASS text, so the tool preserves nested transforms.
