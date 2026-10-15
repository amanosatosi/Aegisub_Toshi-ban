# Aegisub Toshi-ban V3.0 — Release preparation

Release branch: `ver.3.0` (forked from `main` after merging `codex/39-mode`).

**Status: preparation only.** Do not publish a GitHub Release or create a version tag until the checks below pass.

## Release gates

- [ ] GitHub Actions Meson CI passes on `ver.3.0`, including Windows MSVC, Linux and packaging.
- [ ] Windows installer builds; installation and uninstall work without replacing unrelated Aegisub installations unexpectedly.
- [ ] Windows portable archive starts without installer-only files and preserves user settings.
- [ ] Linux AppImage starts successfully and includes its intended resources.
- [ ] Bundled ASSDraw3.exe starts, pans background and drawing separately with right-drag, and supports existing drawing/export actions.
- [ ] Mangetsu renderer is available and renders gradients, furigana, outlines and VSFilterMod compatibility fixtures.
- [ ] Existing Aegisub 3.2.2 Automation scripts, karaoke templates and DependencyControl work in packaged builds.
- [ ] 39 Mode: F/J and D/K timing, pause/resume, Enter commit and Shift-click previous/next audio parts.
- [ ] Restart prompts for language and dark mode: Restart Now / Later; cancellation must not discard unsaved subtitle edits.
- [ ] Test loading, editing and saving existing ASS projects with video and audio attached.
- [ ] Review bundled dependencies and licensing notices.
- [ ] Review release asset names and paths; ensure installer, portable and AppImage correspond to one exact commit.
- [ ] Update README and Burmese README for the final V3.0 feature set and verified installation steps.
- [ ] Write final release notes with new features, fixes, known issues and migration guidance.

## Versioning

- The Meson project currently declares upstream Aegisub version `3.2.2`. Do not change this blindly: it is distinct from the Toshi-ban release version.
- The Windows installer derives `INSTALLER_VERSION` and version strings from `tools/version.ps1` and Git tags, falling back to the Meson project version without a version tag.
- Before publishing, verify that the chosen V3.0 tag (for example `3.0.0`, after confirming naming) produces intended installer metadata on all supported build platforms.
- Do not create a tag until the release commit, builds, and smoke tests are accepted.

## Final publishing sequence

1. Run all required CI and packaged application checks on the release candidate commit.
2. Freeze the `ver.3.0` commit and record its full SHA.
3. Finalize the release notes and build filenames.
4. Tag the verified commit using a version recognized by both platform scripts.
5. Build final release artifacts from that exact tag and verify the binaries.
6. Publish a GitHub Release only after every required artifact is verified.

The existing V2.0 GitHub Release is left untouched.
