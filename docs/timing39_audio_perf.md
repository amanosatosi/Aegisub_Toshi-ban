# 39 Mode audio paint profiling

The audio display has opt-in paint counters. Start Aegisub with
`AEGISUB_AUDIO_PERF=1` and collect its log. Every five seconds of painting it
reports counts and average/max times in milliseconds for total paint, style
lookup, waveform/spectrum drawing, tile builds, 39 Mode visible-range query,
ordinary dialogue spans, target spans, captured blocks, particles, and particle
updates. It also reports paints per second and tracked 39 Mode refresh requests
per second. Other audio refresh call sites are not included in that request
counter. Without the
environment variable, timing is disabled.

Generate deterministic input with
`python tools/timing39_audio_stress.py OUTPUT_DIRECTORY`. Open the generated
ASS and WAV, enable 39 Mode, start continuous playback from 0, and tap F/J
throughout a long capture. Use the same window size, audio zoom, amplitude,
visible range, and 60-second playback window for each build. Run once with
waveform and once with spectrogram. The ASS has 1,800 Dialogue events over
three minutes, with 120 Lyric-style lines among ordinary reference lines.
Keep particles enabled. Compare averages, maxima, and repaint rates from the
same scenario; do not treat the generator as a benchmark result.

The renderer already retains 32-pixel waveform/spectrum bitmap tiles and
cached spectrum power data. Audio source, zoom, height, amplitude, renderer,
and spectrum settings invalidate the relevant caches. Playback position and
timing taps do not. 39 Mode's start-sorted interval index restricts dialogue
and target drawing to the visible range. Its capture preview uses a visible
range visitor rather than copying a full session on each paint.

This profiling procedure requires a built GUI. CI build success and model
unit tests do not establish smoothness or actual paint-time improvement.

## Hot-path audit

- `AudioController::OnPlaybackTimer` publishes the playback position about
  every 20 ms. In 39 Mode, `AudioDisplay::OnPlaybackPosition` follows it with
  a centered scroll and repaint. Key capture still reads the authoritative
  playback clock in the timing input path; it is independent of paint cadence.
- `AudioDisplay::OnPaint` traverses update rectangles and paints waveform or
  spectrum, Toshiki preview, selection/dialogue overlay, timing markers and
  labels, then the 39 Mode reference, target, capture, hit-line and particle
  overlay. The scrollbar and timeline are painted when their regions are dirty.
- `AudioRenderer` uses 32-pixel bitmap tiles for both waveform and spectrum.
  Waveform peak reduction happens on tile misses. Spectrum power data and the
  bitmap tiles are separately cached; FFT computation is not repeated for a
  simple playhead move. A new full-screen bitmap cache would duplicate this
  existing machinery.
- Style ranges were linearly searched from the beginning on every audio
  repaint, and same-style transitions were retained. These now use a binary
  search and transition compaction, adapted from upstream
  [TypesettingTools/Aegisub #668](https://github.com/TypesettingTools/Aegisub/pull/668).
  That draft PR's video seek coalescing was not ported: it changes video sync
  behavior outside this focused audio pass.
- Ordinary Dialogue and 39 target ranges use a start-sorted interval index
  prepared when the controller's subtitle state changes. Paint visits only
  visible overlaps, with no ASS parsing or full-file scan. Captured blocks use
  a binary-searched lane visitor. The display reuses its overlay vector across
  paints instead of constructing one each time.
- Before this pass, 39 Mode's playback callback invalidated the full control
  even when the scroll origin had not moved. Marker updates and the 20 ms
  particle timer could request more audio-area repaints in the same frame.
  The display now coalesces these onto playback updates while active. The
  particle vector reserves its 48-item hard cap and retires oldest particles
  before adding a new hit's eight particles.

## Measurement status

No baseline or final paint times are recorded in this source tree. They must
be obtained from actual instrumented GUI builds using the procedure above.
Do not cite static code inspection or CI build time as a runtime speedup.
