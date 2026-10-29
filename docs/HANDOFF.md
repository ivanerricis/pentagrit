# Pentagrit — handoff (2026-10-29)

Read `docs/PLAN.md` (full plan and decisions) and `docs/ARCHITECTURE.md` (frozen C++ contracts) first.

## State of `main`
Merged: scaffold, crossover (WP1), transient shaper (WP2), saturator + auto-gain (WP3), engine with oversampling (WP4), CI/CD (WP6), governance/docs (WP7), presets (WP8), custom UI (WP5b), brand assets and design sources. 82 tests pass locally (Release, `win-vs`). CI (Windows + Linux build, tests, pluginval 10, clang-format 18) is green; macOS only on push to main / label `ci:macos`.

## In flight when the session ended (work is NOT in git yet)
| WP | Branch | Worktree | What |
|---|---|---|---|
| WP9 | `feat/settings` | `C:\dev\pt-wp9` | SettingsManager (PropertiesFile), 4-tab SettingsPanel, PresetBar placed in the TopBar slot, UiOptions wiring, startup preset. ~10 uncommitted files. |
| WP10 | `perf/dsp-optimization` | `C:\dev\pt-wp10` | Benchmark + CPU optimisation (fast dB<->gain, control-rate shaper gain, fast saturator curves). 8x was ~109% of one core. ~10 uncommitted files. |

The sub-agents that were building these stopped with the session. To continue: open each worktree, run `git status`, finish/verify, build tests (`$env:CPM_SOURCE_CACHE='C:\dev\.cpm'; cmake --preset win-vs; cmake --build --preset win-vs-release --target PentagritTests -j 4; ctest --preset win-vs-release`), format with clang-format, commit (no attribution trailers), push, open PR (`gh pr create --base main`). Never run two builds in the same build directory at once.

Specs for both are in the prompts summarised in `docs/PLAN.md` ("Impostazioni (WP9)") and below.
- WP9: panel tabs Interface / Audio (osPhase, osRender as APVTS params) / Presets / About & Support; per-user settings in `Pentagrit/settings.settings`; update check opt-in, stub while the repo is private; reset only per-user settings; startup preset must not override a DAW-restored state.
- WP10: keep all tests passing; add max-error tests for approximations; do not enable AVX2 globally.

## Decisions taken
- Name Pentagrit; version path v0.1.0-rc1 -> v0.1.0 -> v1.0.0; AGPL-3.0-or-later; repo private for now.
- Defaults: 3 bands, crossovers 120/1200/3500/8000 Hz, oversampling 2x. 63 parameters; `osPhase` and `osRender` are non-automatable and saved with the project; presets never overwrite `osPhase`, `osRender`, `bypass`.
- Auto-gain compensates input gain (reference is dry before inGain): keep.
- Parameter IDs are immutable (CLAP hashes them).

## Open items / next steps
1. Finish and merge WP9 and WP10 (see above).
2. Tune factory presets by ear; check preset bar fit and label crowding at 5 bands (ATTACK/SUSTAIN/SPEED).
3. Test in a real host (Reaper): zoom via `setScaleFactor`, crossover drag, latency compensation, meters with running audio, CPU at 4x/8x.
4. Run pluginval locally; try the macOS CI job (label `ci:macos`) and check AU/auval.
5. Dry-run release `v0.1.0-rc1` (tag must equal CMake version), then `v0.1.0`.
6. Decide licensing before any public binary: make the repo public (AGPL) or buy a commercial JUCE licence.
7. Branch protection: free private repos cannot enforce rulesets; see `docs/BRANCH_PROTECTION.md`.
8. macOS signing/notarisation (phase 2, `docs/MACOS_SIGNING.md`).
9. Cleanup: remove stale worktrees (`git worktree list`, `git worktree remove`) and merged branches.

## Practical notes
- Merging PRs from an agent session may be blocked by the permission classifier ("merge without review"); the user added `Bash(gh pr merge:*)` to `~/.claude/settings.json` (effective from the next session) and can also merge on GitHub.
- clang-format 19.1.5 (VS 2022 Build Tools, `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\Llvm\bin`) is used locally; CI uses 18.
- GitHub Actions minutes (Free: 2000/month for private repos; Windows x2, macOS x10): a CI run is ~35 min of quota.
- Mockup: https://claude.ai/artifact/3eE8dkHNrkkod84GMqkyhf (sources in `docs/design/`).
