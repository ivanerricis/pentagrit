# Pentagrit

Multiband transient shaper and saturator plugin (VST3, CLAP, AU, Standalone) built with JUCE 8 and C++20.

Status: early development. License: AGPL-3.0-or-later. The repository is currently private.

## Features

- 3 to 5 band multiband processing with Linkwitz-Riley 4th order (LR4) crossovers (default 3 bands, 120 / 1200 / 3500 / 8000 Hz).
- Per-band transient shaper (attack and sustain, adjustable speed).
- Per-band saturation with several curves (tanh, soft clip, tube, wavefold, hard clip) and drive.
- Auto-gain, global dry/wet mix with phase-matched dry path, per-band mute, solo and bypass.
- Oversampling (linear or minimum phase) with a separate render quality for offline bounces.
- Factory and user presets.

Formats: VST3 and CLAP on Windows, macOS and Linux; AU on macOS; plus a Standalone app.

## Install

Copy the plugin bundles to the standard locations:

| Format | Windows | macOS | Linux |
|---|---|---|---|
| VST3 | `C:\Program Files\Common Files\VST3` | `/Library/Audio/Plug-Ins/VST3` or `~/Library/Audio/Plug-Ins/VST3` | `~/.vst3` |
| CLAP | `C:\Program Files\Common Files\CLAP` | `/Library/Audio/Plug-Ins/CLAP` or `~/Library/Audio/Plug-Ins/CLAP` | `~/.clap` |
| AU | n/a | `/Library/Audio/Plug-Ins/Components` or `~/Library/Audio/Plug-Ins/Components` | n/a |

### macOS: Gatekeeper quarantine

Builds are ad-hoc signed (not notarized yet, see `docs/MACOS_SIGNING.md`). Remove the quarantine flag after downloading:

```
xattr -dr com.apple.quarantine /path/to/Pentagrit.vst3
```

Repeat for the `.component`, `.clap` and `.app` bundles. If an AU does not show up, run `killall -9 AudioComponentRegistrar` and rescan.

## Build from source

Requirements: CMake >= 3.25, a C++20 compiler (Visual Studio 2026 on Windows, Xcode on macOS, GCC/Clang on Linux), Git. Dependencies are fetched with CPM.

On Windows:

```
set CPM_SOURCE_CACHE=C:\dev\.cpm
cmake --preset win-vs
cmake --build --preset win-vs-release
ctest --preset win-vs-release
```

Other presets: `win-ninja` (from a VS developer prompt), `mac-universal`, `linux`.

Tips:

- Set `CPM_SOURCE_CACHE` to a shared folder to avoid re-downloading JUCE and friends in every checkout or worktree.
- Keep the checkout path short (for example `C:\dev\pentagrit`): deep paths can exceed the Windows 260-character limit during the build.

See `docs/DEVELOPMENT.md` for the full developer guide, `docs/ARCHITECTURE.md` for the frozen contracts and `CONTRIBUTING.md` for how to contribute.

## License

Pentagrit is licensed under the GNU Affero General Public License v3.0 or later (see `LICENSE`). Third-party components and their licences are listed in `THIRD_PARTY_NOTICES.md`.
