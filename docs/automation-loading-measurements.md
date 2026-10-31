# Automation loading comparison

Measured in [Actions run 41230524088](https://github.com/amanosatosi/Aegisub_Toshi-ban/actions/runs/41230524088),
on one Ubuntu 22.04 hosted runner. Baseline: `c7c51a6`. Candidate: `b63ab0dc9`.
Both are release builds with OpenCV disabled. The workflow retains raw JSON logs,
three samples per configuration, native JIT controls, phase profiles and screenshots.
No local compilation was used.

## Same collections, median of three launches

Times below are milliseconds from process launch, except the autoload column,
which starts at the first idle event's Reload. Responsive combines the first
visible frame and the first responsive main-loop idle log; Windows separately
checks actual message responsiveness and editing during loading.

Each nonempty collection includes a trivial `000-fast.lua` macro to test early
publication. Small adds the seven bundled autoload scripts (8 total); large adds
40 module-heavy Lua/MoonScript scripts (48 total). CPU adds 12 finite, fixed-work
initializers (13 total). Broken contains two invalid scripts and two valid scripts.
Unsupported text files are present but excluded. All variants use identical files,
include paths and fresh profiles; run order alternates. Filesystem caches are warm.

| Collection | Version | Responsive | First macro | All scripts | Autoload | CPU | Peak RSS MiB | Slowest script |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| Empty | Baseline | 398.2 | — | 488.5 | 100.8 | 260 | 139.9 | — |
| Empty | Two workers | 395.3 | — | 493.3 | 100.9 | 260 | 139.9 | — |
| Small | Baseline | 394.6 | 491.6 | 491.6 | 101.0 | 330 | 147.6 | 25.6 |
| Small | Two workers | 401.6 | 492.8 | 492.8 | 101.4 | 360 | 148.2 | 37.8 |
| Large | Baseline | 412.3 | 1522.2 | 1522.2 | 1106.8 | 1400 | 217.7 | 33.0 |
| Large | Two workers | 423.1 | 523.1 | 1028.0 | 605.0 | 1480 | 217.5 | 32.0 |
| CPU | Baseline | 411.9 | 2324.7 | 2324.7 | 1911.8 | 2150 | 147.5 | 157.0 |
| CPU | Two workers | 411.1 | 510.5 | 1516.5 | 1105.5 | 2320 | 146.9 | 174.2 |
| Broken | Baseline | 414.8 | 510.2 | 510.2 | 100.4 | 290 | 143.6 | 5.8 |
| Broken | Two workers | 415.8 | 511.6 | 511.7 | 100.8 | 290 | 142.9 | 6.4 |

Two workers reduced autoload duration by 45% for large and 42% for CPU. Including
application initialization, all scripts were available 32% and 35% sooner.
The fast macro became available 66% and 78% sooner. Empty/small/broken collections
show no meaningful speedup: their loading fits within the existing 100 ms progress
poll. Editor responsiveness is broadly unchanged, and individual contended scripts
can take longer even though the whole batch finishes sooner. CPU usage rises
slightly; peak resident memory is broadly unchanged. These results do not establish
a speedup for arbitrary third-party collections or cold storage.

## Worker-count decision

| Workers | Large autoload | Large CPU | CPU autoload | CPU CPU time |
|---:|---:|---:|---:|---:|
| Baseline, one | 1106.8 | 1400 | 1911.8 | 2150 |
| Candidate, one | 1208.8 | 1430 | 2012.4 | 2280 |
| Candidate, two | 605.0 | 1480 | 1105.5 | 2320 |
| Candidate, three | 604.7 | 1810 | 1007.3 | 2900 |
| Candidate, four | 504.8 | 2040 | 905.5 | 3420 |

One worker confirms the serialization regression. Four workers are fastest on
this runner, but use 38% more CPU than two for large and 47% more for CPU, for
100–200 ms less wall time. Two workers are the default to retain most of the gain
with less contention. The diagnostic override remains bounded to 1–4.

## Phase attribution

One separate two-worker diagnostic launch per collection enabled require/include
profiling. These values are attribution, not additive totals: require, include,
compiler/bootstrap and parsing durations overlap, and worker wall times overlap.
The per-script performance samples do not enable the require wrapper.

For the large collection, directory enumeration was 0.73 ms, aggregate state setup
4.85 ms, mapping/read preparation 11.31 ms, loader/compiler bootstrap 186 ms,
inclusive parsing 1154 ms and top-level execution 978 ms. Module loading accounted
for 1161 ms inclusively; `aegisub.util`/regex imports in the generated scripts and
MoonScript compilation dominate. UI-request synchronization was 1.55 ms, macro
registration 0.12 ms and menu rebuilding 2.18 ms across the whole launch.

The slowest large diagnostic script was `large-10.lua` at 42.5 ms; the next four
were 31–33 ms. For CPU, top-level execution accounted for 1976 ms of aggregate
worker time; the slowest was `cpu-00.lua` at 205 ms, with the next four 167–171 ms.
The raw JSON retains individual filenames and all phase records.

No persistent compiler/module cache or serialization is introduced. These
measurements support bounded parallel state initialization; directory scanning,
registry publication and menu reconstruction do not justify extra infrastructure.

## JIT and publication limits

The finite 4-million-iteration controls measured median execution of 41.23 ms
(Lua) and 43.27 ms (MoonScript) with the production cancellation guard, versus
15.01/14.97 ms with JIT enabled. Interpreted controls without the hook measured
27.86/25.10 ms. Thus cancellation dispatch and interpreted execution both have
measurable cost. JIT-enabled controls execute only trusted finite literals in the
test runner; no unsafe production JIT switch is added. Production retains JIT
preferences and safe cancellation through initialization.

Initial progressive publication consumes a deterministic ready prefix. An earlier
slow script can delay later macros; this preserves replacement and export-filter
ordering. Further publication notifications are grouped at 500 ms intervals and
at completion, with an early notification for the first macro. Rescans remain
atomic so the old script/filter batch stays valid until safe replacement. Running
macros and open export-filter dialogs defer replacement. Lua count hooks cannot
interrupt a blocking native/FFI call until it returns.
