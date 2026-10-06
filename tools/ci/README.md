# Windows CI caches

The two Windows matrix jobs run concurrently. The normal MSVC job publishes
vcpkg binary archives and compiler objects; wx master only reads these caches.
Neither cache contains the application build tree or generated Meson state.

## OpenCV

`windows-vcpkg.ps1` reuses the hosted runner's vcpkg checkout, selects the commit
in `.github/vcpkg/revision.txt`, and bootstraps its matching tool. This avoids
different hosted-image versions silently selecting different OpenCV releases.
Update the pinned revision deliberately when updating dependency versions.
The helper installs the packages listed in `.github/vcpkg/opencv.txt`.
Only vcpkg's ABI-addressed binary
archives are persisted. The key includes the compiler binary, MSVC/SDK versions,
triplet contents, package configuration, feature flags, registry commit and
vcpkg executable. A fallback restore may reuse compatible packages from an older
registry; vcpkg's own ABI check decides compatibility. Obsolete archives are
pruned before saving. A miss builds normally, and the normal job saves archives
before application compilation begins.

Both variants restore the same key. The installer helper exports `OpenCV_DIR`,
`OPENCV_BIN_DIR`, `CMAKE_PREFIX_PATH` and the runtime binary path on every run,
including exact cache hits. Custom vcpkg overlays must be added to the fingerprint
before being enabled.

## Compiler objects

Both variants use `sccache cl` through `.github/meson/msvc-sccache.ini`. The shared
`SCCACHE_GHA_VERSION` has no application commit, branch or matrix name in it.
`SCCACHE_BASEDIRS` normalizes the workspace path; sccache hashes compiler inputs
to decide compatibility. GitHub's normal cache branch-access restrictions still
apply.

The normal job sets `SCCACHE_GHA_RW_MODE=READ_WRITE`; wx master uses the official
`READ_ONLY` mode. The statistics helper verifies the daemon's actual startup mode
and prints requests, hits, misses, hit percentage, successful writes, write
errors and non-cacheable compilations in the log and job summary. In sccache
0.18.0, read-only misses can increment the raw write-error counter when writes
are refused locally. The summary keeps that counter visible and separately
identifies that the read-only backend sends no uploads. JSON is printed in the
log rather than uploaded as a diagnostic artifact.

See the [sccache GHA configuration](https://github.com/mozilla/sccache/blob/v0.18.0/docs/GHA.md)
and [vcpkg binary-cache reference](https://learn.microsoft.com/en-us/vcpkg/reference/binarycaching).

## Meson sources

The separate source cache retains the existing expensive fallback sources and
downloaded archives. Its fingerprint includes only their wraps, overlays, diffs
and resolved Git revisions. Floating refs are resolved and pinned in the
ephemeral checkout before lookup, so a moving ref changes the key. Exact hits
skip refresh; fallback hits remove obsolete untracked overlays and run Meson's
reset/update. Configuration is followed by a source-commit check.

The release-preparation job runs deterministic helper tests and actionlint.
Validate changes through Actions, comparing cache-miss and warm runs on fresh
runners. Include archive restoration in dependency timings, compare the fallback
command fingerprints across variants, and check installer/portable results as
well as compiler statistics.
