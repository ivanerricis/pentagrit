# Pentagrit — handoff (2026-11-12)

Read `docs/PLAN.md` (full plan and decisions) and `docs/ARCHITECTURE.md` (frozen C++ contracts) first.

## State of `main`
Status as of 2026-11-12. Merged: scaffold, crossover, transient shaper, saturator + auto-gain, engine with oversampling, CI/CD, governance/docs, presets, custom UI, brand assets (WP0-WP8); settings panel and preset bar (#12); CPU optimisation (#13); Windows and macOS installers (#15); selectable visual styles (#17); value step, typed values and ctrl-click reset (#18); clang-tidy fix and ASan/UBSan job (#19); typeface cache lifetime fix (#20); macOS CI on `macos-15` (#21); typed parameter text, non-automatable `oversampling`/`numBands`, state version validation, latency reported from the message thread (#22); opt-in update check (#23); drive compensation and ADAA hard clip / wavefold (#24); undo/redo (#25). The optional transient-shaper lookahead (`lookahead`, non-automatable, project-level, excluded from presets, adds to the reported latency) is merged; the parameter count is 65. v0.2: per-band gain-change meters, delta monitoring, level-matched bypass, band link, tooltips and `docs/GUIDE.md`, clap-validator blocking in CI, CLAP state fix, README with screenshots and audio examples (#32-#41).

CI (Windows + Linux build, tests, pluginval 10, clang-format 18) is green; clang-tidy (zero project warnings, `--warnings-as-errors`) and asan-ubsan are enforced jobs; macOS runs on push to `main` and with the `ci:macos` label and passes. The first stable release is `v0.1.0` (after pre-releases rc1 to rc3). The v0.2 work packages are done (PRs #32-#41) and the release PR bumps the version to 0.2.0; the tag `v0.2.0` goes on the merge commit after the manual checks in `docs/RELEASING.md`. The repository is PUBLIC (AGPL-3.0-or-later).

## Decisions taken
- Name Pentagrit; version path v0.1.0-rc1 -> rc2 -> rc3 -> v0.1.0 (published) -> v0.2.0 -> v1.0.0; AGPL-3.0-or-later; repo made public on 2026-10-29 (commercial closed-source distribution would now need a JUCE commercial licence and a relicense).
- Defaults: 3 bands, crossovers 120/1200/3500/8000 Hz, oversampling 2x. 65 parameters (with `driveComp` and `lookahead`). `oversampling`, `numBands`, `osPhase`, `osRender` and `lookahead` are non-automatable; `osPhase`, `osRender`, `lookahead` and `bypass` are saved with the project and presets never overwrite them.
- Auto-gain compensates input gain (reference is dry before inGain): keep.
- Parameter IDs are immutable (CLAP hashes them).

## Open items / next steps
1. Next step: merge the release PR (`chore: release 0.2.0`), run the manual DAW checklist of `docs/RELEASING.md` (v0.2 checks plus the carried-over v0.1.0 ones: Windows installer, factory preset listening, lookahead latency in Reaper, update check), then tag `v0.2.0` on the merge commit once CI on `main` is green including macOS. Fixes found go into the next release.
2. v0.2.0 scope (all done, PRs #32-#41): per-band gain-change meters; delta (effect only) monitoring; band link; level-matched bypass; tooltips and a short guide; clap-validator blocking in CI; CLAP state fix; README with current screenshots and audio examples. Design decisions D0-D7 are listed below (no new parameters, `kNumParameters` 65, `stateVersion` 1).
3. Code signing / notarisation (Windows signing, Apple notarisation, `docs/MACOS_SIGNING.md`): out of scope for 0.2.
4. Done: the `main` ruleset requires six checks, including `asan-ubsan` and `clang-tidy` (`docs/BRANCH_PROTECTION.md`).
5. Verify the product name "Pentagrit" is free of trademark conflicts before the stable release.
6. Cleanup: remove stale worktrees (`git worktree list`, `git worktree remove`) and merged branches.

## Practical notes
- Merging PRs from an agent session may be blocked by the permission classifier ("merge without review"); the user added `Bash(gh pr merge:*)` to `~/.claude/settings.json` (effective from the next session) and can also merge on GitHub.
- clang-format 19.1.5 (VS 2022 Build Tools, `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\Llvm\bin`) is used locally; CI uses 18.
- GitHub Actions minutes are free because the repository is public; a full CI run (including macOS) takes about 10 minutes of wall time.
- Mockup: https://claude.ai/artifact/3eE8dkHNrkkod84GMqkyhf (sources in `docs/design/`).
