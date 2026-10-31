# Startup loading diagnostics

Automation autoload starts on the first application idle event, after main-window
construction. A coordinator scans each canonical autoload directory once and
filters using registered engine patterns. Private worker states initialize scripts
independently; the diagnostic `AEGISUB_AUTOMATION_WORKERS` override accepts 1–4.
Configured directory order and sorted filenames determine publication order,
independently of which worker finishes first.
Lua and MoonScript use the same loader. Include paths and application path tokens
are captured before starting the worker. Unsupported files are not initialized.

Commands and export filters remain script-owned and unpublished during loading.
The main-thread timer progressively publishes the ready prefix during initial
loading, notifying the existing Automation menus and Manager. The first macro is
published promptly; further menu updates are grouped at 500 ms intervals and at
completion. A slow earlier script can delay publication of later scripts to keep
name conflicts deterministic. Rescans replace the batch atomically: old macros
remain usable during a rescan. Publication
waits for running macros or open export-filter dialogs to release their Lua states.
The Manager's single-script Reload uses the same progress path for just that file.
Repeated rescans cancel and discard the old batch before starting a replacement;
they cannot create additional pools. Shutdown cancels and joins all workers
before destroying options, commands or logging.

The bottom-right status-bar field contains the existing Automation icon, a short
label, thin native gauge and completed/total counts. It never enters the main
layout or changes grid height; the two existing status-message fields remain
separate. Tooltips show full active filenames and errors; clicking any part opens
Automation Manager. Counts include failures. Updates are limited to 10 per second
and driven by actual completions. Successful progress disappears after two seconds;
a subtle warning remains after failures. Manager and Show Info retain error details.

Cancellation uses a Lua count hook with the JIT engine disabled during
initialization. Global JIT mode requests are deferred until initialization ends;
function-specific requests, reported preferences and saved aliases are preserved. Clipboard, translation and non-Windows text measurements
are marshalled to the main thread using cancellable requests. Queued requests own
their payloads and never capture the manager or frame. Cancellation is cooperative:
Lua cannot interrupt arbitrary blocking native code, filesystem drivers or FFI
calls. Such code must return before shutdown can join the worker. Deliberately
disabling the cancellation hook inside a script is also outside this guarantee.

Renderer construction tries the configured provider directly. A failed selection
logs a warning and uses the existing fallback ordering without changing the saved
preference. Full discovery happens when renderer selection is requested. Valid
custom DLLs remain loaded for the process lifetime; failed availability checks are
retried when selection UI is reopened. CSRI discovery remains inside CSRI's mutex.
The one-time Mangetsu offer checks its persisted marker and CI suppression before
probing just Mangetsu, after the main UI enters its event loop.

## Measurements and verification

The ordinary JSON log includes monotonic-clock measurements in these sections:

* `startup/timing`: application initialization, main-window construction, first
  shown frame (the wxWidgets Show call), and first responsive idle event.
* `startup/renderer`: selected/fallback provider construction and separate
  asynchronous renderer/font initialization. These do not measure first video pixels.
  Completed font timings are emitted when the provider is released, while logging
  is still alive.
* `startup/automation/script`: initialization duration for each attempted script.
  Additional records separate state setup, loader/compiler bootstrap, file reading,
  parsing, top-level execution, UI-request synchronization and ready-to-publication
  wait. `AEGISUB_AUTOMATION_PROFILE=1` additionally times outermost `require` calls
  and includes. Nested module/include/parse durations are inclusive, so these are
  attribution views, not additive totals. Error paths may unwind Lua directly;
  the enclosing protected-call and per-script total still cover those failures.
  Reading measures file mapping/setup; OS page faults during parsing belong to
  the parser measurement.
* `startup/automation/enumeration`: supported files and directory enumeration time.
* `startup/automation/first_macro`: first published macro, after menu notification.
* `startup/automation/publication`: registration and listener notification time.
* `startup/automation/menu`: individual menu reconstruction time.
* `startup/automation`: completed/total, failures and total batch duration including
  scanning, UI-request waits and publication.

GitHub Actions builds the application and runs existing tests plus native Lua
cancellation/MoonScript tests. On Windows, `tests/python/startup_smoke.py` launches
isolated copies of the portable build and checks empty autoload, slow scripts,
editing during loading, macro execution, failed initialization, repeated Manager
rescans, shutdown during an infinite Lua loop, and direct renderer selection and
missing-DLL fallback. Both the stable and newer wxWidgets matrix entries exercise
the suite. Logs and observed window responsiveness timings are uploaded in the
`startup-diagnostics` artifacts. Existing custom compositor tests cover unchanged
rendered pixels. CSRI/xy-VSFilter installations still need manual acceptance testing.

For before/after comparisons, use the same machine, configuration, fonts and
autoload directories, warm up once, then record several launches for each build.
Compare medians and ranges for an empty directory and the same script collection.
The `Automation loading measurements` workflow builds c7c51a6 and the candidate
with identical release settings on one runner. The Python benchmark uses the same
empty, bundled, large mixed Lua/MoonScript, CPU-heavy and broken collections for
each, alternates run order, records three samples and reports medians. Wall times
start at process launch; responsive time combines the first visible X11 frame and
the first main-loop idle log. First macro time includes menu notification (the
baseline completion log is a conservative proxy immediately before its notification).
CPU and peak RSS come from `/proc`, sampled before benchmark process termination.
Windows smoke tests separately exercise graceful shutdown. Raw logs, samples and
phase records are retained as Actions artifacts. This is a warm-filesystem benchmark,
not a cold-disk claim or a guarantee for every third-party script.

The native finite JIT control measures Lua and MoonScript setup/parse/execution
separately. It is deliberately unavailable to arbitrary autoload scripts: compiled
LuaJIT traces ignore count hooks. Production initialization retains interpreted
execution, the 10,000-instruction cancellation hook and deferred JIT preferences.
