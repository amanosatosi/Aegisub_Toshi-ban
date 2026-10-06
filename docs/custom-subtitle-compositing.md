# Custom subtitle provider composition

Only `src/subtitles_provider_libassmod.cpp` and its custom rendering helper use
these extensions. The ordinary upstream-libass provider is unchanged.

## Verified ABI

The build wraps dynamically load separate libraries from
`amanosatosi/libassmod` master and mangetsu. The inspected revisions are
master `3f5640045bbd737661f3858480ec948fece929cb` and
mangetsu `3c0d5e4cd5685a51bef31dd0d69b300a77defe77`.

| Public function | libassmod master | Mangetsu |
| --- | --- | --- |
| `ASS_ImageRGBA *ass_render_frame_rgba(ASS_Renderer *, ASS_Track *, long long, int *)` | exported | exported |
| `void ass_free_images_rgba(ASS_ImageRGBA *)` | exported | exported |
| `int ass_composite_images_bgra(ASS_ImageRGBA *, uint8_t *, int, int, int)` | absent | exported |
| `ASS_RenderResult ass_render_frame_auto(...)` | exported | exported |
| `ASS_Image *ass_render_frame(...)` | exported | exported |

Verified against each branch's `libass/ass.h`, `libass/libass.sym`,
`libass/ass_render_api.c` and `libass/ass_render_rgba.c`:
[master exports](https://github.com/amanosatosi/libassmod/blob/3f5640045bbd737661f3858480ec948fece929cb/libass/libass.sym),
[Mangetsu exports](https://github.com/amanosatosi/libassmod/blob/3c0d5e4cd5685a51bef31dd0d69b300a77defe77/libass/libass.sym).

Both auto APIs call the RGBA renderer, then free the returned RGBA list when
`frame_needs_rgba` is false and expose the borrowed legacy list. Previously,
ordinary ASS therefore paid for conversion/discard and then a Boost.GIL
operation per legacy image in Aegisub.

## Single-render hierarchy

The selected library's symbols are loaded independently. A usable direct
RGBA API requires both the render and free functions. The provider prefers:

1. Direct RGBA plus the native BGRA compositor, when exported.
2. Direct RGBA plus the existing premultiplied RGBA Boost.GIL fallback.
3. Compatibility auto rendering, only when direct RGBA is unavailable; this
   preserves RGBA features on older builds that expose only the wrapper.
4. Borrowed legacy images, only when usable RGBA APIs are unavailable (or the
   compatibility auto API returns legacy output).

Each path invokes exactly one frame-generation function. Empty RGBA means an
empty frame; it never triggers another render. A failed native composition
uses the same RGBA list. Current Mangetsu preflights the entire list before
modifying the destination, making that fallback safe. No other backend is
selected silently.

Current master lacks the native compositor, so it uses the RGBA fallback.
This retains the converted RGBA output instead of discarding it, but does not
eliminate its per-tile GIL operations. Obtaining Mangetsu's one-call native composition
benefit on that backend requires adding/exporting that API in libassmod.
The provider will use it automatically when available.

## Destination and ownership

Composition writes directly into the existing `VideoFrame::data`. The physical
row pitch is `frame.pitch`. For flipped frames the logical top row points at
the last stored row and the compositor receives negative pitch. Both host
fallbacks use that same signed layout, clip to visible pixels, and preserve
padding. Images retain renderer painter order; there is no Aegisub batching,
sorting, temporary full-frame buffer or copy.

Every non-null RGBA result is held by a scoped unique owner calling that
backend's `ass_free_images_rgba` exactly once, including early returns and
composition exceptions. Legacy image lists remain renderer-owned. Native
composition receives the original renderer-created nodes, since Mangetsu
uses private metadata for destination-aware blend modes.

`detect_change` is passed through unchanged. It describes subtitle output,
not the underlying video. Aegisub fetches fresh video pixels for preview and
also renders black/white backgrounds for alpha readouts; unchanged subtitles
must still be composited. No subtitle-output caching or early change skip is
introduced.

## Deterministic regression coverage

The tests call the exact helper used by both custom providers. Fake APIs
check one renderer call, one native composition, same-list failure fallback,
empty output, exception cleanup, compatibility selection, long timestamps,
painter order, padding, clipped tiles and flipped buffers.

Real backend tests dynamically load the built libraries and compare every
BGRA byte with an independent ordered RGBA compositor. Fixtures cover ordinary
dialogue, borders/shadows, alpha, both fade forms, karaoke, all clip forms,
animated rectangular clips, transforms, colors, layers, collisions and missing
font fallback. Gradients and registered image fills exercise existing RGBA
features. Mangetsu blend modes compare against its existing auto/native path
because those modes intentionally require private renderer metadata.

A 600-event workload varies primary color, layer and static/animated rectangular
clips over repeated text/geometry. Counters verify RGBA selection, at least
500 fragments, no legacy/auto renderer calls, one native compositor call when
exported, and one release per nonempty result. Diagnostics report node/call
counts; elapsed time is not a pass/fail condition. Tests compare against
renderer RGBA output, not a second font implementation or legacy mask rounding.

The dedicated GitHub Actions job builds both libraries and runs these tests.
The existing full Windows matrix runs them with normal wx and wx 3.3.3;
other existing checks remain enabled. No local builds or tests are used.

## Possible next renderer API

A future `ass_render_frame_to_bgra(renderer, track, now, dst, width, height,
stride, detect_change)` could let the renderer composite ordinary monochrome
masks directly into the destination while preserving event order, collisions,
gradients and blend metadata. That would avoid materializing temporary RGBA
tiles for ordinary ASS, removing another allocation/conversion stage. It is
not implemented here; this change uses the existing exported ABI.
