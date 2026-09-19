# 39 Mode

39 Mode is a separate timing controller. Original karaoke and Toshiki K-Timing
retain their existing controllers, tag selectors and split behavior.

## Use

1. Open audio and select a dialogue with sensible start/end checkpoints.
2. Select **Audio → 39 Mode**. The modeless review window shows the source,
   reading, morae and diagnostics. Click **Focus audio**, then **Space**.
3. Alternate **F/J** for the primary voice and optionally **D/K** for another
   voice. A fresh press takes ownership immediately. Releasing the old key does
   nothing; releasing the current key starts a gap. Repeats are ignored.
4. **Space** stops. Matching selects the best supported assignment automatically.
   **Tab** or a right click on audio opens review. GREEN means a clear timing
   choice, YELLOW means nearby legal alternatives, RED means no supported mapping.
   Language confidence is reported separately in the inspection.
5. Select an alternative path, or select the block after a divider and press
   **Left/Right** to transfer one mora. Protected boundaries cannot be crossed.
   Undo/Redo changes assignments only; raw timestamps remain unchanged. When a
   correction redistributes a join across distant words, choose a complete path.
6. Choose the relevant subtitle event for secondary capture if evidence cannot
   identify it. Targets are labeled by their actual style/text for human review;
   style names are never interpreted as roles.
7. **Enter / Commit** writes both mapped lanes in one subtitle undo operation.
   **R / Retake lane** clears only the selected lane and replays with 500 ms preroll.
   **Capture both** explicitly restarts both lanes. Ordinary replay after capture
   auditions the line. **Discard** clears the current checkpoint's preview.

Capture only intercepts rhythm keys in the focused audio window while armed and
playing. Normal subtitle typing is unaffected. Focus loss, playback stop/restart,
seek, Escape, checkpoint end and mode exit sanitize held keys. Escape preserves
raw capture for review; Discard explicitly clears it. Other checkpoints remain
available while the mode is open. Capture is not automatically committed.

The existing waveform or spectrogram scrolls beneath a centered turquoise
(`#39C5BB`) hit line. Before resolution the overlay shows base morae or raw blocks,
not a guessed grouping. Hit flashes and bounded lightweight particles update on
a timer; painting never advances animation state.

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
They are not blindly identified inside known words. Unknown runs remain UNKNOWN.
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
history. The controller caches each visited checkpoint's analysis/capture;
text changes invalidate affected cached previews, while undo/deletion may require
a full reload. Morphology and DP do not run during animation.

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
