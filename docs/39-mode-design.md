# 39 Mode implementation notes

39 Mode is independent of Original and Toshiki K-Timing. No local compilation
is permitted; the repository's GitHub Actions matrix is the build/test authority.
`feature/39-mode` conflicts with the existing `feature` ref, so development uses
`codex/39-mode`. The pre-existing bisect state is left alone.

## Architecture

The portable C++ model separates source spans, authoritative readings, spoken
lexical tokens, deterministic morae, protected boundaries, named candidate
edges, captured blocks, assignments and confidence. A conservative vocabulary
and contextual grammar rules may return UNKNOWN. Explicit ruby always wins;
unknown kanji requires a supplied reading. Lexical identity is independent of
ruby span identity. The matcher traverses only generated edges with exactly N
blocks, retaining multiple paths and feature-level scores.

Timing39Session owns two independent full-session raw lanes and snapshots of
candidate targets. Countdown arms recording automatically; the live path only
records media-clock key events. Stop partitions copies at dialogue checkpoints,
then runs one matcher per line/lane. Crossing blocks retain raw provenance and
require review. A local retake has separate raw storage and replaces one result
lane. Corrections own separate history; no operation rewrites original taps.
All results remain preview state until explicit bulk commit.

Serialization reuses AssKaraoke's logical ruby surface and empty syllables.
Renderer inspection: libassmod/mangetsu `ass_render.c`,
`append_furi_reading` parses karaoke override blocks; `append_furi_base` ignores
base overrides. `parse_furi_candidate` rejects malformed groups and non-karaoke
override blocks inside groups. No placeholder extension or invented syntax is
needed. Existing fork tests in `ass_karaoke_mangetsu.cpp` establish ruby insertion
points; new golden tests must exercise gaps, tag families and rounding.

https://github.com/amanosatosi/libassmod/blob/mangetsu/libass/ass_render.c

The audio display retains its waveform/spectrum renderer. A scoped wx event filter routes
rhythm keys from project child widgets only during countdown/capture. Normal
pause/stop controls finalize once. The results panel appears after stop, while
reading, paths, movement, undo/redo and diagnostics live in an Inspector.
Blank rounded blocks, a centered hit line and timer-driven particles use the
existing waveform/spectrum renderer. Style mapping
uses accumulated timestamp evidence, never style-name semantics; ambiguity
requires explicit target selection before writing secondary capture.
