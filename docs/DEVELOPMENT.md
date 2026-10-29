# Development guide

## Setup

Windows: run `scripts/dev/setup-windows.ps1` to check tools (Git, CMake, Visual Studio, Ninja, clang-format) and set `CPM_SOURCE_CACHE`. On macOS/Linux install CMake, Ninja and a C++20 compiler.

Keep the checkout path short and share dependencies via `CPM_SOURCE_CACHE` (for example `C:\dev\.cpm`). With git worktrees each worktree has its own `build/` but reuses the cache.

## Build and test

```
cmake --preset win-vs            # or win-ninja, mac-universal, linux
cmake --build --preset win-vs-release
ctest --preset win-vs-release
```

Debug build: `cmake --build --preset win-vs-debug`. Presets set `CMAKE_POLICY_VERSION_MINIMUM=3.5` so newer CMake versions accept older dependencies.

## Validating the plugin

`scripts/dev/run-pluginval.ps1` downloads pluginval (once) and validates a built VST3 at strictness 10:

```
powershell -File scripts/dev/run-pluginval.ps1 -Vst3 path\to\Pentagrit.vst3
```

On macOS also run `auval`; for CLAP use clap-validator. CI runs all of these.

## Formatting and lint

`clang-format` (v18 in CI) with `.clang-format`; `clang-tidy` is advisory. See `CONTRIBUTING.md`.

## Architecture

`docs/ARCHITECTURE.md` describes the DSP chain, the 63 parameters and the frozen C++ contracts. `docs/PLAN.md` holds the work plan. Parameter IDs are immutable (see `CONTRIBUTING.md`).
