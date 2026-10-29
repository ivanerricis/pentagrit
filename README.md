# Pentagrit

Multiband transient shaper and saturator plugin (VST3, CLAP, AU, Standalone) built with JUCE 8 and C++20.

Status: early development (scaffold). License: AGPL-3.0-or-later.

## Build

Requirements: CMake >= 3.25 and a C++20 compiler.

```
set CPM_SOURCE_CACHE=C:\dev\.cpm
cmake --preset win-vs
cmake --build --preset win-vs-release
ctest --preset win-vs-release
```

See `docs/ARCHITECTURE.md` for the frozen contracts and `docs/PLAN.md` for the work plan.
