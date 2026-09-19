# 39 Mode

39 Mode is a separate timing controller. Original karaoke and Toshiki K-Timing
retain their existing controllers, tag selectors and split behavior.

## Use

1. Open audio and select a group of timed lyric dialogues. Activate **Audio →
   39 Mode**. With multiple selected rows, that selection is the explicit scope.
   With only one selected row, the active style and overlapping Japanese/ruby or
   existing karaoke evidence determine candidate targets after capture.
2. Fresh sessions seek to **media time zero**, or the earliest Comment whose
   plain text is exactly **39 mode start here** (ASCII case-insensitive, trimmed;
   ASS overrides are ignored). The Comment's absolute Start supplies the marker.
   Ordinary Dialogue text never supplies a marker. Comments do not count toward
   lyric selection, analysis, results, or serialization.
3. Playback remains paused while a large **3, 2, 1** appears at the center of the
   main window, one second each. The raised child overlay does not resize panels,
   moves with the main client area, and uses DPI-scaled size and turquoise text
   on dark backing. It disappears before playback starts. F/J/D/K capture is
   disabled until the Ready → Capturing transition; countdown taps are ignored.
   Video and audio then start together at the chosen absolute media time.
4. Perform continuously. A fresh **F/J** press starts the primary block and
   immediately takes ownership from the other primary key. Releasing the old
   key does not stop its replacement; releasing the current owner starts a gap.
   **D/K** has exactly the same independent behavior for the secondary lane.
   Repeated keydown events are ignored. No matching occurs during performance.
5. Normal audio stop/toggle or **Ctrl+P / video play** finalizes the session.
   Reaching the final selected checkpoint also stops; intermediate dialogues
   never stop recording. In discovery mode playback can continue to audio end.
6. The results dialog lists every target with **GREEN / YELLOW / RED**, its
   times, style, and source. Choose **Commit all GREEN**, or inspect ambiguous
   results, select their lane, mark reviewed, and **Commit reviewed GREEN /
   YELLOW**. RED rows have no supported assignment and cannot be committed.
   Language uncertainty is reported separately from timing confidence.
7. **Inspector** contains explicit reading edits, alternate paths, assignments,
   and detailed diagnostics. Select the block after a divider and use the arrow
   buttons or Left/Right to transfer a mora. Protected dividers cannot move.
   Assignment Undo/Redo never changes captured timestamps.
8. **Retake selected lane** uses that line minus **Audio Lead IN**, then the same
   centered countdown,
   then returns to results. It replaces only the selected result/lane. Other
   lines, the other lane, and the original full-session raw capture survive.
   Canceling a retake countdown preserves the old result.

The active session routes unmodified rhythm keys across widgets belonging to
this project window. Outside countdown/capture, typing and normal shortcuts are
untouched. App deactivation stops and sanitizes capture; moving widget focus
inside the app does not. Playback restart/seek finalizes the old session, and
ordinary replay does not silently start a new recording. Right-click audio to
reopen hidden results. Toggle 39 Mode off/on to start a fresh session. Subtitle
edits/undo invalidate cached targets rather than retaining stale event pointers.

The existing waveform/spectrum scrolls beneath a centered turquoise `#39C5BB`
hit line. Rounded captured blocks and gap outlines are **blank**: no kana,
romaji, numbers, or labels inside them. Small playback status stays above the
lanes; countdown numerals appear only in the main-window overlay.
Fresh accepted keydowns flash the hit line and emit a bounded particle burst;
a timer advances effects and paint only reads them.

### Start metadata and cancellation

With multiple valid markers, the earliest absolute Start wins regardless of grid
order (same-time ties use event order). Inspector records `SESSION START`, its
milliseconds, and the chosen comment event number, or `default media start`.
A marker at 100000 ms leaves a tap at 104250 ms as **104250**, never 4250.
Dialogue checkpoints are never shifted. Session silence before 40000 ms is not
serialized inside a lyric starting at 40000; only intersecting blocks/gaps are
partitioned, and stray out-of-target taps do not shift later assignments.

Stopping, changing modes, closing the window, or invalidating the session hides
the overlay, stops its timer, and disarms capture. Queued timer events cannot
restart a canceled model. If a marker lies at/after the existing playback range
end, activation reports the invalid range instead of playing from another time.
The existing final-selected-checkpoint end policy is preserved.

### Session checkpoints

`Timing39Session` owns the raw absolute media timestamps and follows
Idle → Countdown → Ready → Capturing → Results. Stop resolves once. Dialogue
start/end times partition copies of the recording, so a bad count cannot shift
later lines. Inter-line silence stays in session raw data and is not copied
into unrelated dialogue interiors. Blocks crossing checkpoints retain their
original raw times; clipped local copies are flagged for review. Empty unused
lanes do not generate fake lyric targets or sung blocks.

Selection scope is trusted; fallback discovery is conservative. Non-active
styles additionally need ruby or existing karaoke evidence. Opaque style names
are never interpreted as main/backing. Timestamp evidence needs corroboration;
unclear lane associations and overlapping targets require review. The results
show the reason each target entered scope.

## Reading and language

`Analysis` keeps the original ASS source, karaoke-free span source, serialization
surface, normalized reading, source spans, independent spoken tokens and morae.
Span offsets are UTF-8 bytes in `span_source`; reading offsets are codepoints;
logical offsets are UTF-8 bytes in the AssKaraoke timing surface. Diagnostics
explicitly label these coordinate systems.

The right side of `<display|reading>` is authoritative. `<現在|イマ>` is `いま`,
even though the small source dictionary knows `現在 → げんざい`. Katakana and
common decomposed voiced kana normalize without changing the original ruby.
Explicit romaji is converted with input-to-output offsets. Bare romaji and known
unannotated kanji receive a ruby surface at commit, preserving the display text.
Unknown kanji produces RED and needs a reading in the preview field. The field
accepts a complete source/ruby expression; applying it retains captured timing.

The conservative vocabulary recognizes nouns, several verbs and inflected forms,
auxiliaries and expressions used by the supplied regression corpus. Contextual
particles include を, が, と, に, で, は/わ, へ, も, の, さ, けど, から and ので.
They are not blindly identified inside known words. Unfamiliar explicit ruby
is localized to structural compound candidates rather than swallowing an entire
sentence. Consecutive ruby anchors can remain one compound (高 + 鳴 + る);
known lexical identity can span anchors (未 + 来). Written okurigana supports
candidate godan/ichidan endings, negative and polite forms, te/ta forms,
i-adjectives, and common auxiliary sequences. These are uncertain morphology
candidates, not invented dictionary identities. Inspector preserves competing
morphological explanations. Arbitrary all-kana unknowns remain conservative;
this is not a complete Japanese morphological dictionary or parse lattice.
Source spellings corroborate lexical identity, including identity spanning several
ruby groups. A group containing multiple spoken words is not treated as one word.
Artistic ruby is analyzed as the spoken reading even when the display is unrelated.

Protected boundaries include punctuation/spacing, recognized particle/word
boundaries, unsupported sound relationships and supported final-mora inflection
rules (`さまよう`, `まよう`, `おもう`, `うたう`, `ねがう`, `わらう`, `ちがう`,
`むかう`, `であう`, `いう`, `しまう`). Consequently `[をう]`, quotative `[とい]`,
particle `[がい]` and `彷徨う → [よう]` are absent from the graph, not penalized
edges which could win under pressure.

## Morae, graph and matching

Base tokenization is independent of tap count. Tested yoon and foreign-sound
clusters form one mora; っ, ん and ー remain individual base morae. Punctuation and
spaces take no timing block. Written vowel sequences remain separate base morae.

Every mora has a single-mora edge. Named rules may add two-mora edges for a long
mark, supported preceding-mora sokuon grouping, moraic nasal continuation,
written long-vowel-like sequences, and vowel adjacency inside a known lexeme.
There is no arbitrary substring partitioning, no universal sokuon attachment
claim and no rule forcing every long mark to merge.

The bounded k-best DP traverses these edges with exactly N sung blocks, retaining
up to eight alternatives by default. Gaps do not consume morae. Per-edge scores
separate provisional phonological priors, lexical/source features and soft
log-duration fit. `ScoringModel` centralizes weights. The second-best margin
determines timing ambiguity. Unsupported counts retain RED and raw capture.
Lines over 512 morae are refused to bound memory use on weak hardware.

`TimingLane` owns held keys, current owner, blocks and gaps. `TimingAssignment`
maps a raw block index to a mora range. `AssignmentEditor` owns independent
history. The controller prepares cached target analyses before countdown and partitions
the session only at stop. Text changes invalidate cached targets. Morphology and DP do not run during animation.

## Style evidence and serialization

Existing karaoke boundaries are compared to both lanes in absolute media time.
Symmetric nearest-boundary distance is aggregated by opaque style identity across
checkpoints. The best distinct style pair needs corroborating events, adequate
absolute fit and a sufficient margin. Ambiguous, conflicting or duplicate target
events require manual selection. Secondary capture is never silently written
into an unrelated event.

`timing39_karaoke.cpp` uses the existing `AssKaraoke` logical reading surface,
split operations and empty rest syllables. It preserves ruby display text and
the existing karaoke tag family, including mixed `\k`, `\kf`, `\ko`, `\kO`.
Existing `\K` normalization remains Aegisub's `\kf`. Leading, intervening and
trailing gaps become empty karaoke syllables. Absolute checkpoints are rounded
to centiseconds once before duration subtraction, avoiding cumulative drift.
The original dialogue interval remains unchanged. Invalid alignment or timing
outside the target event refuses the entire commit.

Renderer inspection established that karaoke blocks belong inside the reading
side and base-side tags do not advance its timeline. Non-karaoke overrides inside
an explicit group are currently rejected by 39 Mode because the inspected
renderer rejects those groups. Drawings and malformed ruby are also refused.
Overrides outside groups are preserved by the shared serializer. No placeholder
or special 39 Mode ASS extension is introduced.

Corrections retain predicted/current assignments and contextual features in the
inspection. Corrected commits attach this data as `39-mode-correction` extradata;
**Save inspection** exports it. Individual corrections never modify global weights.

## Worked examples

The following use synthetic 100 ms blocks to exercise realistic lyric text.
They are not measurements of the original songs. The integration regression
prints full source mappings, candidates, alternative scores, assignments and ASS
to the CI test log for all three lyrics.

### Exact count

Source: 自分の価値に目を疑って  
Reading: じぶんのかちにめをうたがって  
Spoken tokens: 自分 / の / 価値 / に / 目 / を / 疑って  
Base: じ|ぶ|ん|の|か|ち|に|め|を|う|た|が|っ|て

Fourteen blocks require fourteen single-mora assignments: block `i` maps to mora
`i`, count 1. Particle boundaries are protected. `[ぶん]` and `[がっ]` can exist
as graph candidates but cannot appear in an exact-count path. Timing is GREEN.

```ass
<自分の価値に目を疑って|{\k10}じ{\k10}ぶ{\k10}ん{\k10}の{\k10}か{\k10}ち{\k10}に{\k10}め{\k10}を{\k10}う{\k10}た{\k10}が{\k10}っ{\k10}て>
```

### One join, retained uncertainty

Source: 僕は始まった栄光のゴールを見たいのさ  
Reading: ぼくわはじまったえいこうのごーるをみたいのさ  
Spoken tokens: 僕 / わ / 始まった / 栄光 / の / ゴール / を / 見たい / の / さ  
Base: ぼ|く|わ|は|じ|ま|っ|た|え|い|こ|う|の|ご|ー|る|を|み|た|い|の|さ

With 21 blocks and 22 morae, the preferred path joins `[ごー]`; serious alternatives
join `[えい]`, `[こう]` or `[まっ]`. Particle and known word boundaries are protected.
Block 13 maps to mora range `[13,15)`; earlier blocks are 1:1, and later block `i`
maps to mora `i+1`. The supplied grouping is ranked first with uniform synthetic
durations, but nearby paths still justify YELLOW. Duration evidence can change
the ranking without adding forbidden edges.

```ass
<僕は始まった栄光のゴールを見たいのさ|{\k10}ぼ{\k10}く{\k10}わ{\k10}は{\k10}じ{\k10}ま{\k10}っ{\k10}た{\k10}え{\k10}い{\k10}こ{\k10}う{\k10}の{\k10}ごー{\k10}る{\k10}を{\k10}み{\k10}た{\k10}い{\k10}の{\k10}さ>
```

### Several plausible joins

Source: いっせーのーで鳴り響いたスタートの合図  
Reading: いっせーのーでなりひびいたすたーとのあいず  
Spoken tokens: いっせーのー / で / 鳴り響いた / スタート / の / 合図  
Base: い|っ|せ|ー|の|ー|で|な|り|ひ|び|い|た|す|た|ー|と|の|あ|い|ず

Eighteen blocks need three joins. One supported preferred path uses `[せー]`,
`[のー]`, `[たー]`; competing paths can retain a long mark and use `[いっ]`,
`[びい]` or `[あい]`. Timing remains YELLOW. `[のあ]` across the particle is absent.
The preferred mora ranges are `0+1, 1+1, 2+2, 4+2, 6+1, 7+1, 8+1, 9+1, 10+1,
11+1, 12+1, 13+1, 14+2, 16+1, 17+1, 18+1, 19+1, 20+1` in block order.

```ass
<いっせーのーで鳴り響いたスタートの合図|{\k10}い{\k10}っ{\k10}せー{\k10}のー{\k10}で{\k10}な{\k10}り{\k10}ひ{\k10}び{\k10}い{\k10}た{\k10}す{\k10}たー{\k10}と{\k10}の{\k10}あ{\k10}い{\k10}ず>
```

### Gaps and artistic ruby

Source `<現在|イマ>`, spoken `いま`, base `い|ま`: at dialogue `[1000,1600)`,
capture `[1100,1250)` and `[1350,1500)` maps block 1 to mora 0 and block 3 to
mora 1. Blocks 0, 2 and 4 are gaps. The unique mapping is GREEN:

```ass
<現在|{\k10}{\k15}イ{\k10}{\k15}マ>{\k10}
```

## Coverage and limitations

Tests exercise the specified tokenizer/romaji examples, artistic and split ruby,
language gating, exact counts, long marks, sokuon, nasal and vowel candidates,
duration ranking, GREEN/YELLOW/RED, ownership takeover, repeat suppression,
simultaneous lanes, gaps, focus/reset semantics, local retakes, correction history,
style ambiguity, golden serialization, mixed tag families and absolute rounding.
No existing tests are removed or weakened. All C++ builds/tests run in GitHub
Actions; local work is limited to editing and static inspection.

This is a conservative first implementation, not a complete Japanese parser.
The small vocabulary and greedy spoken segmentation cannot settle homophones or
all compounds. Unknown kanji, halfwidth kana, drawings, malformed ruby, unsupported
in-group styling and counts needing unimplemented joins may be RED. `みく` with
one tap and `彷徨う` with three taps are deliberate RED regressions. No arbitrary
join is invented to hide a limitation.

Only the retained best paths are offered in the picker. A local divider move
must keep both neighboring groups legal. Recorded capture/corrections are
session-local until commit/export, and switching modes discards uncommitted state.
Secondary events with differing checkpoints must contain the captured timestamps.

The supplied SAO grouping is included and selected in the synthetic regression;
actual song recordings/tap timestamps were not supplied, so measured prediction
error and real-performance calibration remain unverified. The 18-block example
has no unique supplied ground truth. Next language work should add reviewed
inflection/auxiliary rules, source-aware short-word disambiguation and a broader
spoken lexicon, backed by captured correction data rather than universal joins.
