# Startup loading diagnostics

Automation autoload starts on the first application idle event, after main-window
construction. One worker scans each canonical autoload directory once, filters
using registered engine patterns, and initializes scripts in directory order.
Lua and MoonScript use the same loader. Include paths and application path tokens
are captured before starting the worker. Unsupported files are not initialized.

Commands and export filters remain script-owned and unpublished during loading.
The main-thread timer publishes the completed batch and notifies the existing
Automation menus and Manager. Old macros remain usable during a rescan. Publication
waits for running macros or open export-filter dialogs to release their Lua states.
The Manager's single-script Reload uses the same progress path for just that file.
Repeated rescans cancel and discard the old batch before starting a replacement;
they cannot create additional concurrent workers. Shutdown cancels and joins the
worker before destroying options, commands or logging.

The bottom-right frame panel shows a native gauge, completed/total counts, and the
current script. Counts include failures. Updates are limited to 10 per second and
are driven by actual completions. After two seconds, completed progress collapses;
the warning link remains if needed. It opens Automation Manager; failed scripts and
Show Info retain error descriptions, including directory and publication failures.

Cancellation uses a Lua count hook with the JIT engine disabled during
initialization. Global JIT mode requests are deferred until publication;
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
* `startup/automation/script`: initialization duration for each attempted script.
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
No before/after speedup is claimed without such measurements.
