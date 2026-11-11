<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->
# Pentagrit architecture

Source of truth for the frozen WP0 contracts. Parameter IDs are stable and must never be renamed
(CLAP hashes them, hosts store them). See `docs/PLAN.md` for the full plan.

## DSP summary

Signal flow:
`input -> Oversampler.up -> inGain -> crossover LR4 (cascaded TPT SVF) -> per band [TransientShaper -> Saturator -> dry/wet mix -> gain x mute/solo] -> sum -> AutoGain -> global mix with phase-matched dry (allpass chain) -> Oversampler.down -> outGain`.
Bypass is a delay of L samples with a 20 ms crossfade.

- One oversampler around the whole core (same latency for every band); equiripple half-band FIR (linear phase) or polyphase IIR (minimum phase), integer latency; factor limited to fs * F <= 384 kHz.
- Latency reported to the host = oversampler latency + transient-shaper lookahead (0 when the lookahead is Off).
- Crossover: LR4 = 2x Butterworth SVF; the sum is an allpass; ordering enforced with a minimum distance of 1/3 octave (parameter values are not rewritten).
- Transient shaper: 4 envelope followers (2 fast / 2 slow), dB difference mapped to attack/sustain gains, speed x4..x0.25, low-frequency ripple guard, -70 dBFS gate, 1 ms smoothing, stereo-linked, everything in seconds (rate independent). Followers run per sample; the gain mapping, smoothing and dB-to-linear conversion run at a control rate (every 8..32 samples) with a linear ramp in between.
- Lookahead (`lookahead` parameter, Off / 1 / 2 / 5 ms): optional look-ahead of the transient shaper. Its time is added to the reported latency. This is specified here and lands right after the drive-compensation and undo work; see the headers in `src/dsp/` for the exact interface.
- Saturator: pre-gain drive + curves (tanh, cubic soft clip, asymmetric tube, sin-fold, hard clip), 10 ms crossfade on curve change, 5 Hz DC blocker. "Off" is transparent.
  - Hard clip and wavefold use first-order antialiasing (ADAA1): `y[n] = (F(u[n]) - F(u[n-1])) / (u[n] - u[n-1])`, evaluated in closed form, with a fallback to the curve at the mean input for tiny steps. The per-channel state `u[n-1]` is allocated in `prepare()`, cleared in `reset()` and while the saturator is Off. ADAA1 is a two-tap average, so these two curves lose some high end (about -2 dB at 10 kHz at 48 kHz without oversampling, -0.5 dB at 2x) and are delayed by half a sample at the processing rate. This is not compensated.
  - `Saturator::shape(SatType, float)` (UI curve preview) is the plain curve, without ADAA.
  - Drive compensation (`driveComp`, global, default off): the output is scaled by `1 / driveGain`, the same smoothed gain as the pre-gain, so a signal below the knee keeps the level it has at 0 dB drive and only the harmonics change. The Off curve is never compensated. The switch is blended with a ~20 ms ramp. The small-signal slope of each curve at 0 dB drive is not removed. With compensation off and settled the extra code is skipped.
- AutoGain: wet RMS vs dry reference (400 ms), clamp +-24 dB, 200 ms smoothing, hold on silence.
- Realtime safe: no allocation/locks in the audio thread, `ScopedNoDenormals`, parameters through `std::atomic<float>*`, atomic meters.

## Parameters (65)

`kNumParameters` in `src/params/Parameters.h` is the authority: 14 global + 5 bands x 10 per-band parameters (64 on `main` today, 65 with `lookahead`).

Global: `inGain`, `outGain`, `mix`, `autoGain`, `driveComp`, `oversampling`, `numBands` (2..5, default 3), `osPhase` (Linear/Minimum, default Linear), `osRender` (Same as live/2x/4x/8x, default Same as live), `lookahead` (Off/1/2/5 ms, default Off), `xover1..4` (20-20k Hz, log, defaults 120/1200/3500/8000), `bypass`.
Per band `b1_`..`b5_`: `gain`, `attack`, `sustain` (+-24 dB), `speed`, `satType`, `drive` (0-36 dB), `mix`, `mute`, `solo`, `bypass`.
All IDs use `ParameterID{id, 1}`.

- Non-automatable (hosts cannot automate them; changing them alters latency or resets filters): `oversampling`, `numBands`, `osPhase`, `osRender`, `lookahead`.
- Project-level, excluded from presets (loading a preset never changes them): `osPhase`, `osRender`, `lookahead` and `bypass`. They are saved with the project state. `oversampling`, `numBands` and `driveComp` are stored in presets; a preset that lacks `driveComp` loads it as off.
- Typed text: percent, frequency (Hz/kHz) and dB parameters parse typed text ("50 %", "1.2k", "-3") through `withValueFromStringFunction`; `getText` -> `getValueForText` round-trips.
- Latency changes: `processBlock` only stores the engine latency in an atomic. A 30 Hz message-thread timer in the processor calls `setLatencySamples`, so the host sees a change up to ~33 ms late. `prepareToPlay` still sets it synchronously.

## State and versioning

State is the APVTS value tree as XML, tag `PENTAGRIT_STATE`, plus the UI size (`uiWidth`, `uiHeight`) and the `stateVersion` property (`kStateVersion`, currently 1).

- `getStateInformation` always writes `stateVersion`.
- `setStateInformation` ignores a state whose `stateVersion` is newer than the running plugin's; a missing version counts as 1; older versions go through `PentagritAudioProcessor::migrateState(tree, fromVersion)` (a no-op today: add `if (fromVersion < N)` steps there when `kStateVersion` is bumped) before `replaceState`.
- Presets carry their own `stateVersion` and newer ones are rejected as well.
- Parameter IDs and `kParamVersion` never change (see above).

## Undo and redo

Undo is gesture based and owned by the processor, not by the APVTS.

- Every UI edit already runs inside `beginChangeGesture()` / `endChangeGesture()` (attachments, crossover handles, selectors, typed values, wheel notches); the host never calls them on our listeners. `UndoRecorder` (a parameter listener inside `PluginProcessor.cpp`) turns each outermost gesture into one `juce::UndoManager` transaction: the value at gesture start and at gesture end. No step is recorded if nothing changed; nested gestures join the outer step.
- The APVTS is created with `nullptr` as its `UndoManager`. An APVTS with an `UndoManager` copies values to the tree from a timer and records every copy, so host automation would flood the history and UI edits could not be told apart from automation.
- Undo and redo write the parameters through a gesture (the host sees them) and do not record themselves. The `UndoManager` is used on the message thread only; the audio thread never touches it. `getUndoManager()` exposes it to the editor.
- The history is bounded (`kUndoMaxUnits`, about 200 single-parameter steps) and is cleared on preset load, A/B switch and state restore (`setOnStateReplaced`, `valueTreeRedirected`, `setStateInformation`). A restore from a non-message thread is deferred to the processor's message-thread timer.
- Crossover handles that the editor moves when the host raises the band count are not recorded (`ScopedUndoPause`).
- The editor handles Ctrl/Cmd+Z, Ctrl/Cmd+Shift+Z and Ctrl/Cmd+Y, except while a text editor has focus or the settings overlay is open.

## Frozen C++ contracts

Excerpts; the headers in `src/` are authoritative.

```cpp
// params/ParamIDs.h
namespace pentagrit {
inline constexpr int kMaxBands = 5, kNumXovers = 4, kParamVersion = 1, kStateVersion = 1;
inline constexpr const char* kStateTag = "PENTAGRIT_STATE";
namespace pid { inline constexpr const char *inGain="inGain", *outGain="outGain", *mix="mix",
  *autoGain="autoGain", *oversampling="oversampling", *numBands="numBands", *bypass="bypass",
  *osPhase="osPhase", *osRender="osRender", *driveComp="driveComp"; /* + lookahead, see the header */
  inline constexpr std::array<const char*,4> xover{"xover1","xover2","xover3","xover4"}; }
enum class BandParam { gain, attack, sustain, speed, satType, drive, mix, mute, solo, bypass };
inline constexpr int kNumBandParams = 10;
juce::String bandParamID(int band0, BandParam p);   // (0, gain) -> "b1_gain"
}
// params/Parameters.h
inline constexpr int kNumParameters = 14 + kMaxBands * kNumBandParams;   // current value: see the header
juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
struct ParamRefs { explicit ParamRefs(juce::AudioProcessorValueTreeState&); /* cache atomics */
  std::atomic<float> *inGain,*outGain,*mix,*autoGain,*oversampling,*numBands,*bypass,*osPhase,*osRender,*driveComp; /* + lookahead */
  std::array<std::atomic<float>*,4> xover;
  struct Band { std::atomic<float> *gain,*attack,*sustain,*speed,*satType,*drive,*mix,*mute,*solo,*bypass; };
  std::array<Band,kMaxBands> bands;  EngineSettings snapshot() const noexcept; };
// dsp/DspTypes.h
enum class SatType { off, tanh, softClip, tube, wavefold, hardClip };
enum class OSFactor { x1, x2, x4, x8 };
enum class OSPhase { linear, minimum };
struct BandSettings { float gainDb=0, attackDb=0, sustainDb=0, speed01=.5f, driveDb=6, mix01=1;
                      SatType sat=SatType::off; bool mute=false, solo=false, bypass=false;
                      bool driveComp=false; /* copied from the global switch by the engine */ };
struct EngineSettings { float inGainDb=0, outGainDb=0, mix01=1; bool autoGain=false, bypass=false, driveComp=false;
  OSFactor os=OSFactor::x2; OSPhase osPhase=OSPhase::linear; OSFactor osRender=OSFactor::x1; bool osRenderSame=true;
  int numBands=3; std::array<float,4> xoverHz{120,1200,3500,8000};
  std::array<BandSettings,kMaxBands> bands{}; /* + lookahead, see the header */ };
// dsp/Meters.h  (audio thread writes relaxed, UI reads)
struct MeterState { std::array<std::atomic<float>,kMaxBands> bandPeakDb, bandTransientGainDb;
  std::atomic<float> inPeakDb{-100}, outPeakDb{-100}, autoGainDb{0};
  std::array<std::atomic<float>,4> effectiveXoverHz; std::atomic<int> latencySamples{0};
  std::atomic<float> cpuLoad{0.f}; /* fraction 0..1 of block time in processBlock */ };
```

DSP module conventions: `prepare()` may allocate (only from `prepareToPlay`); `setProcessingRate(double)`, `reset()` and `process()` are `noexcept` and allocation free.

Module APIs (see the headers in `src/dsp/` for exact signatures):

- `Crossover`: `prepare(baseRate, procRate, maxBlock, numCh)`, `setNumBands`, `setFrequencies(array<float,4>)`, `getEffectiveFrequencies`, `static enforceOrdering(hz, numBands, baseRate)`, `split(in, array<AudioBlock<float>,5>&)`, `processAllpassChain(io)`.
- `TransientShaper`: `setParameters(attackDb, sustainDb, speed01)`, `setLowFrequencyHint(hz)`, `process(block)`, `getLastGainDb()`.
- `Saturator`: `setType`, `setDriveDb`, `process`, `static shape(SatType, float u)` (also used by the UI preview); drive compensation and ADAA state are internal.
- `AutoGain`: `setEnabled`, `process(ref, wet)`, `getCurrentGainDb()`.
- `BandProcessor`: `setSettings(BandSettings, audible, lowFreqHintHz)`, `process`, `getTransientGainDb()`.
- `MultibandEngine`: `prepare(sr, maxBlock, numCh)`, `setSettings(EngineSettings)`, `process(AudioBuffer<float>&)` (internal chunking), `getLatencySamples()`, `static effectiveFactor(OSFactor, sr)`, `static computeAudibility(settings)`, `meters()`.
- `PentagritAudioProcessor`: `getAPVTS()`, `getMeters()`, `getPresetManager()`, `getSettings()`, `getUndoManager()`; UI size persisted via `getUiSize()` / `setUiSize()`.

## Oversampling phase / render factor

`EngineSettings` carries `osPhase` (`OSPhase::linear` / `minimum`), `osRender` and `osRenderSame`
(`osRenderSame == true` means "Same as live": use `os`; otherwise use `osRender`; parameter choice 1..3 -> x2, x4, x8).
`processBlock` reads `AudioProcessor::isNonRealtime()` to pick the live or the render factor.
Linear phase uses the half-band FIR oversampler. Minimum phase uses `juce::dsp::Oversampling` with `filterHalfBandPolyphaseIIR`, reporting a rounded integer latency (a small mismatch with the dry path and the bypass delay is accepted).

## Per-user settings

Per-user UI settings (zoom, visual style, layout, tooltips, knob mode/sensitivity, value step of the dB knobs (`valueStep`: continuous/0.1/0.5/1), meter hold/release/refresh,
show CPU, preset folder, startup preset, update-check opt-in and last-check time) are NOT parameters and not part of presets. They live in a
`juce::PropertiesFile` (`Pentagrit/settings.settings` in the user app-data folder), owned by `SettingsManager` (`src/settings/`), which is shared by all plugin instances in the process and tolerates a missing or corrupt file.
The visual style (`Style` in `ui/Theme.h`, stored as `uiTheme`) and the UI zoom are part of these settings; the editor applies them live.

## Update checker

`src/update/UpdateChecker.*` holds pure functions (semver parse/compare, release JSON parse, daily schedule, response evaluation) and a small background-thread checker owned by `SettingsManager`.

- Opt-in, off by default. When on, at most one check per 24 h per user; "Check now" always checks.
- One `GET https://api.github.com/repos/ivanerricis/pentagrit/releases/latest`; the only header set by the code is `User-Agent: Pentagrit`; no identifiers. Timeout 6 s, 2 redirects, 512 KB response cap. Only `tag_name` and `html_url` are used, and the URL must start with `https://github.com/ivanerricis/pentagrit/`.
- Runs on its own thread, never on the audio thread; the result is delivered on the message thread.
- `/releases/latest` ignores pre-releases and drafts, so a release candidate is not reported.
- Windows and macOS only (`SettingsManager::kUpdateCheckAvailable`); on Linux JUCE has no HTTP backend (`JUCE_USE_CURL=0`) and "Check now" opens the releases page.

## Typeface cache lifetime

The embedded typefaces (`src/ui/Fonts.cpp`) are cached in a `juce::DeletedAtShutdown` singleton, created lazily and deleted while JUCE is still alive. Function-local statics that hold JUCE graphics objects must not be used: they are destroyed after JUCE shuts down, which crashed on exit on macOS. Callers hold a `shared_ptr` to the faces, so a face stays valid if the cache is deleted meanwhile; the cache is re-created if fonts are requested again.

## Build notes

- `src/` sources are collected with a CMake glob (`CONFIGURE_DEPENDS`) into the `PentagritCore` INTERFACE target, so work packages can add files without editing CMake.
- `PresetManager` runs on the message thread only and is independent of the audio thread.
