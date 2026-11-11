# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- Multiband engine: 2 to 5 bands (default 3) with LR4 crossovers, per-band transient shaper (attack, sustain, speed) and saturator (tanh, soft clip, tube, wavefold, hard clip), auto-gain, phase-matched global mix, per-band mute, solo and bypass.
- Oversampling (off, 2x, 4x, 8x) with a linear or minimum phase filter and a separate render factor for offline bounces; the latency is reported to the host.
- Factory and user presets, with A/B compare and a preset bar in the top bar.
- Settings panel: zoom, six visual styles, Full or Compact layout, tooltips, knob mode and sensitivity, value step for the dB knobs, meter options, CPU readout, preset folder and startup preset.
- Double-click a knob or crossover handle to type a value; Ctrl+click (Cmd+click on macOS) resets it to the default.
- "Drive Compensation" global switch (parameter `driveComp`, stored in presets) that keeps the level of quiet signals constant while you raise the drive.
- Optional transient-shaper lookahead (Off, 1, 2 or 5 ms; parameter `lookahead`). It is not automatable, is saved with the project, is not part of presets, and its time is added to the reported latency.
- Undo and redo for parameter changes (top bar buttons, Ctrl/Cmd+Z, Ctrl/Cmd+Shift+Z, Ctrl+Y); the history is cleared when a preset is loaded, A/B is switched or a project state is restored.
- Opt-in update check against GitHub Releases (Windows and macOS): off by default, at most once a day, sends only a plain `User-Agent: Pentagrit` request; on Linux "Check now" opens the releases page. It only reports stable releases.
- Installers: Windows (Inno Setup `.exe`) and macOS (`.pkg`), plus a Linux `.tar.gz` and `SHA256SUMS.txt` with each release.

### Changed

- Hard Clip and Wavefold are now antialiased with first-order ADAA: less aliasing, but slightly darker at low oversampling (about -2 dB at 10 kHz at 1x and -0.5 dB at 2x) and delayed by half a sample at the processing rate.
- Oversampling and Bands can no longer be automated by the host.
- Latency changes are reported to the host from the message thread, up to about 33 ms after they happen.
- The DSP is about twice as fast at 4x and 8x oversampling.

### Fixed

- Typed values such as "50 %", "1.2k" and "-3 dB" are now understood by percent, frequency and dB parameters.
- Plugin state saved by a newer version is no longer loaded; states without a version load as version 1.
- Crash on exit (and a failing macOS test run) caused by the font cache outliving JUCE.
- Debug assertion when opening the settings panel while the editor is not showing.
