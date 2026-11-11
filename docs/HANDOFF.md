# Pentagrit — handoff (2026-11-11)

Read `docs/PLAN.md` (full plan and decisions) and `docs/ARCHITECTURE.md` (frozen C++ contracts) first.

## State of `main`
Status as of 2026-11-11. Merged: scaffold, crossover, transient shaper, saturator + auto-gain, engine with oversampling, CI/CD, governance/docs, presets, custom UI, brand assets (WP0-WP8); settings panel and preset bar (#12); CPU optimisation (#13); Windows and macOS installers (#15); selectable visual styles (#17); value step, typed values and ctrl-click reset (#18); clang-tidy fix and ASan/UBSan job (#19); typeface cache lifetime fix (#20); macOS CI on `macos-15` (#21); typed parameter text, non-automatable `oversampling`/`numBands`, state version validation, latency reported from the message thread (#22); opt-in update check (#23); drive compensation and ADAA hard clip / wavefold (#24); undo/redo (#25). The optional transient-shaper lookahead (`lookahead`, non-automatable, project-level, excluded from presets, adds to the reported latency) merges right after this documentation PR; the parameter count is then 65.

CI (Windows + Linux build, tests, pluginval 10, clang-format 18) is green; clang-tidy (zero project warnings, `--warnings-as-errors`) and asan-ubsan are enforced jobs; macOS runs on push to `main` and with the `ci:macos` label and passes. The latest published build is the pre-release `v0.1.0-rc2`; there is no stable release. The repository is PUBLIC (AGPL-3.0-or-later).

## Decisions taken
- Name Pentagrit; version path v0.1.0-rc1 -> rc2 (published) -> rc3 -> v0.1.0 -> v1.0.0; AGPL-3.0-or-later; repo made public on 2026-10-29 (commercial closed-source distribution would now need a JUCE commercial licence and a relicense).
- Defaults: 3 bands, crossovers 120/1200/3500/8000 Hz, oversampling 2x. 65 parameters (with `driveComp` and `lookahead`). `oversampling`, `numBands`, `osPhase`, `osRender` and `lookahead` are non-automatable; `osPhase`, `osRender`, `lookahead` and `bypass` are saved with the project and presets never overwrite them.
- Auto-gain compensates input gain (reference is dry before inGain): keep.
- Parameter IDs are immutable (CLAP hashes them).

## Open items / next steps
1. Listening check of the factory presets: Hard Clip / Wavefold timbre with ADAA (slightly darker at 1x/2x) and the drive compensation switch; tune by ear.
2. Manual test in a real host (Reaper): typed parameter text, undo/redo, latency compensation with the lookahead and with oversampling changes, zoom, crossover drag, meters, CPU at 4x/8x. Update check: only testable after the first stable release (GitHub "latest" ignores pre-releases, so `v0.1.0-rc*` is never reported).
3. Visual check of the tightened top bar (preset bar width at 1100 px with the undo/redo buttons) and of the DRIVE COMP toggle in every style.
4. Update the `main` ruleset so `asan-ubsan` and `clang-tidy` are required checks (both jobs are now enforced in `ci.yml`; `docs/BRANCH_PROTECTION.md` has the JSON and the real check names). Maintainer action after the merge.
5. Apply the branch ruleset on `main` if not applied yet (`docs/BRANCH_PROTECTION.md` has the command).
6. Code signing / notarisation (Windows signing, Apple notarisation, `docs/MACOS_SIGNING.md`).
7. Release `v0.1.0-rc3`, then `v0.1.0` (tag must equal the CMake version).
8. Verify the product name "Pentagrit" is free of trademark conflicts before the stable release.
10. Cleanup: remove stale worktrees (`git worktree list`, `git worktree remove`) and merged branches.

## Practical notes
- Merging PRs from an agent session may be blocked by the permission classifier ("merge without review"); the user added `Bash(gh pr merge:*)` to `~/.claude/settings.json` (effective from the next session) and can also merge on GitHub.
- clang-format 19.1.5 (VS 2022 Build Tools, `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\Llvm\bin`) is used locally; CI uses 18.
- GitHub Actions minutes are free because the repository is public; a full CI run (including macOS) takes about 10 minutes of wall time.
- Mockup: https://claude.ai/artifact/3eE8dkHNrkkod84GMqkyhf (sources in `docs/design/`).
