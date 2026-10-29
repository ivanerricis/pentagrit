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
5. `release.yml` builds Windows/macOS/Linux, zips VST3, CLAP, AU, Standalone and LICENSE, builds the installers (see below), computes `SHA256SUMS.txt`, generates notes with git-cliff and creates the GitHub Release.
6. Run the manual checklist below on the produced artifacts, then publish (if drafted).

## Release assets

| Asset | Built by |
|---|---|
| `Pentagrit-vX.Y.Z-windows-x64.zip`, `-macos-universal.zip`, `-linux-x64.zip` | `scripts/ci/package.sh` |
| `Pentagrit-vX.Y.Z-linux-x64.tar.gz` (same content as the Linux zip, keeps executable bits) | `scripts/ci/package.sh` |
| `Pentagrit-vX.Y.Z-windows-setup.exe` | `scripts/ci/make_installer_windows.ps1` + `installer/windows/pentagrit.iss` (Inno Setup) |
| `Pentagrit-vX.Y.Z-macos.pkg` | `scripts/ci/make_installer_macos.sh` + `installer/macos/distribution.xml.in` (pkgbuild/productbuild) |
| `SHA256SUMS.txt` | `release` job (`sha256sum` over all of the above) |

`X.Y.Z` is the tag as pushed (so `v0.1.0-rc1` gives `Pentagrit-v0.1.0-rc1-...`). Installers are built from the already packaged zip, only when `build.yml` is called with `installers: true`, which only `release.yml` does (tag pushes). PR and main CI never build them.

### Windows installer

- Inno Setup, installed in CI with `choco install innosetup --version=6.7.3` (pinned; bump deliberately).
- Per-machine, requires admin. Components: VST3 to `%CommonProgramFiles%\VST3\Pentagrit.vst3`, CLAP to `%CommonProgramFiles%\CLAP`, Standalone to `%ProgramFiles%\Pentagrit` plus a Start menu shortcut. Shows the AGPL licence; uninstall from Apps and features removes everything it installed.
- The version comes from the tag (`/DAppVersion` numeric part, `/DAppVersionText` full text). Never change the `AppId` GUID in the `.iss`.
- Unsigned: Windows SmartScreen shows "Windows protected your PC" on the first run (More info, then Run anyway). Code signing (for example Azure Trusted Signing or an OV/EV certificate) is a later step.
- Local build (needs Inno Setup 6, `winget install JRSoftware.InnoSetup`): `scripts/ci/make_installer_windows.ps1 -Zip dist/Pentagrit-vX.Y.Z-windows-x64.zip -Version vX.Y.Z -OutDir dist`.

### macOS installer

- Flat distribution `.pkg` with four selectable components (VST3, CLAP, AU, Standalone; all selected by default), licence screen, system-wide install: `/Library/Audio/Plug-Ins/{VST3,CLAP,Components}` and `/Applications`. Components are non-relocatable and upgrade in place.
- The bundles inside are the ad-hoc signed ones from the zip. The `.pkg` is **unsigned** by default, so Gatekeeper blocks a double-click on a downloaded file: right-click, Open (or System Settings, Privacy and Security, Open Anyway), or `xattr -d com.apple.quarantine Pentagrit-*.pkg`.
- Optional signing hook, off unless set: `MACOS_INSTALLER_CERT="Developer ID Installer: Name (TEAMID)"` (and optionally `MACOS_INSTALLER_KEYCHAIN`) makes `make_installer_macos.sh` pass `--sign` to `productbuild`. Secrets and notarisation (`notarytool submit`, then `stapler staple` on the `.pkg`) are described in `docs/MACOS_SIGNING.md`.
- Local build (macOS only): `scripts/ci/make_installer_macos.sh dist/Pentagrit-vX.Y.Z-macos-universal.zip vX.Y.Z dist`.
- Uninstall: there is no uninstaller (Apple installers have none). Delete `/Library/Audio/Plug-Ins/VST3/Pentagrit.vst3`, `/Library/Audio/Plug-Ins/CLAP/Pentagrit.clap`, `/Library/Audio/Plug-Ins/Components/Pentagrit.component` and `/Applications/Pentagrit.app`; optionally `sudo pkgutil --forget io.github.ivanerricis.pentagrit.<vst3|clap|au|standalone>`.

### Linux

`.tar.gz` and `.zip` with VST3, CLAP and the standalone binary; copy to `~/.vst3`, `~/.clap`. No installer.

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
- [ ] Windows: run the setup `.exe` on a clean machine; select/deselect components, plug-ins appear in the DAW, uninstall removes them.
- [ ] macOS: install the `.pkg` (component selection, licence screen); AU/VST3/CLAP scan in the DAW; Standalone launches from /Applications.
- [ ] `sha256sum -c SHA256SUMS.txt` passes on the downloaded assets.
- [ ] Bundles include LICENSE and match `THIRD_PARTY_NOTICES.md`.
