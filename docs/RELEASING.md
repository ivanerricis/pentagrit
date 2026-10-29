# Releasing

## Licence warning (read first)

Pentagrit is AGPL-3.0-or-later and links JUCE under the AGPLv3 option. **Distributing binaries publicly (GitHub Release, website, forum) obliges you to make the corresponding source available** to the recipients. The repository is currently private: before the first public binary, either make the repository public or obtain a commercial JUCE licence and relicense accordingly. Decide this before `v0.1.0`.

## Versioning

[Semantic Versioning](https://semver.org/). Path:

1. `v0.1.0-rc1`: dry run of the whole release pipeline (tag, CI build on 3 OS, zip, GitHub pre-release). Delete it afterwards if not needed.
2. `v0.1.0`: first release.
3. `v0.x`: minor bumps for features, patch for fixes; breaking changes are allowed before 1.0 but must be in the changelog.
4. `v1.0.0` when stable: parameter IDs and state format frozen with migrations in place; no known crash or realtime-safety bugs; passes pluginval (strictness 10), auval and clap-validator on all platforms; manual DAW checklist green on Windows, macOS and Linux; documentation and licence/distribution decisions complete.

## Version bump rule

The version in `CMakeLists.txt` (`project(Pentagrit VERSION x.y.z ...)`) **must equal the tag** (without the `v`, and without the `-rcN` suffix). The release workflow fails otherwise.

## Procedure

1. Move `Unreleased` entries in `CHANGELOG.md` to a new version section with the date.
2. Bump `VERSION` in `CMakeLists.txt`; open a PR `chore: release x.y.z`, merge (squash).
3. Make sure CI on `main` is green, including macOS (use the `ci:macos` label or run on main).
4. Tag the merge commit and push: `git tag vX.Y.Z && git push origin vX.Y.Z`.
5. `release.yml` builds Windows/macOS/Linux, zips VST3, CLAP, AU, Standalone and LICENSE, generates notes with git-cliff and creates the GitHub Release.
6. Run the manual checklist below on the produced artifacts, then publish (if drafted).

## Pre-release manual DAW checklist

On Windows with Reaper (and other hosts as available), for both VST3 and CLAP:

- [ ] Plugin scans and loads without errors; UI opens, zooms and closes cleanly.
- [ ] Latency is reported and compensated (null test against bypass); changes with oversampling are picked up.
- [ ] Parameter automation works (record/playback); non-automatable parameters (`osPhase`, `osRender`) are saved.
- [ ] Project save/reload restores all state and UI size.
- [ ] Presets: load factory presets, save/load a user preset, nested menus work.
- [ ] CPU at 8x oversampling and 5 bands is acceptable; no dropouts at small block sizes.
- [ ] Offline bounce uses the render oversampling setting.
- [ ] Sample rates 44.1 to 192 kHz and odd block sizes behave.
- [ ] macOS: AU validates with `auval` (codes from `CMakeLists.txt`) and loads in Logic/Reaper after `xattr -dr com.apple.quarantine`.
- [ ] Bundles include LICENSE and match `THIRD_PARTY_NOTICES.md`.
