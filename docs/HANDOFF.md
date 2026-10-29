# Pentagrit — handoff (2026-10-29)

Read `docs/PLAN.md` (full plan and decisions) and `docs/ARCHITECTURE.md` (frozen C++ contracts) first.

## State of `main`
Merged: scaffold, crossover (WP1), transient shaper (WP2), saturator + auto-gain (WP3), engine with oversampling (WP4), CI/CD (WP6), governance/docs (WP7), presets (WP8), custom UI (WP5b), brand assets and design sources. 82 tests pass locally (Release, `win-vs`). CI (Windows + Linux build, tests, pluginval 10, clang-format 18) is green; macOS only on push to main / label `ci:macos`.

## Work completed since the first handoff
WP9 (settings panel + preset bar wired into the editor, PR #12) and WP10 (CPU optimisation, ~2x faster at 4x/8x oversampling, PR #13) are merged on `main`. 95+ tests pass; CI (Windows + Linux build, tests, pluginval 10, clang-format 18) is green; clang-tidy is advisory and currently reports warnings. The repository is now PUBLIC (AGPL-3.0-or-later), so GitHub Actions minutes are free.

## Decisions taken
- Name Pentagrit; version path v0.1.0-rc1 -> v0.1.0 -> v1.0.0; AGPL-3.0-or-later; repo private for now.
- Defaults: 3 bands, crossovers 120/1200/3500/8000 Hz, oversampling 2x. 63 parameters; `osPhase` and `osRender` are non-automatable and saved with the project; presets never overwrite `osPhase`, `osRender`, `bypass`.
- Auto-gain compensates input gain (reference is dry before inGain): keep.
- Parameter IDs are immutable (CLAP hashes them).

## Open items / next steps
1. Triage clang-tidy warnings (CI job is advisory).
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
