<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->
# Pentagrit architecture

Source of truth for the frozen WP0 contracts. Parameter IDs are stable and must never be renamed
(CLAP hashes them, hosts store them). See `docs/PLAN.md` for the full plan.

## DSP summary

Signal flow:
`input -> Oversampler.up -> inGain -> crossover LR4 (cascaded TPT SVF) -> per band [TransientShaper -> Saturator -> dry/wet mix -> gain x mute/solo] -> sum -> AutoGain -> global mix with phase-matched dry (allpass chain) -> Oversampler.down -> outGain`.
Bypass is a delay of L samples with a 20 ms crossfade.

- One oversampler around the whole core (same latency for every band); equiripple half-band FIR, integer latency; factor limited to fs * F <= 384 kHz; `setLatencySamples` updated on change.
- Crossover: LR4 = 2x Butterworth SVF; the sum is an allpass; ordering enforced with a minimum distance of 1/3 octave (parameter values are not rewritten).
- Transient shaper: 4 envelope followers (2 fast / 2 slow), dB difference mapped to attack/sustain gains, speed x4..x0.25, low-frequency ripple guard, -70 dBFS gate, 1 ms smoothing, stereo-linked, everything in seconds (rate independent).
- Saturator: pre-gain drive + curves (tanh, cubic soft clip, asymmetric tube, sin-fold, hard clip), 10 ms crossfade on curve change, 5 Hz DC blocker. "Off" is transparent.
- AutoGain: wet RMS vs dry reference (400 ms), clamp +-24 dB, 200 ms smoothing, hold on silence.
- Realtime safe: no allocation/locks in the audio thread, `ScopedNoDenormals`, parameters through `std::atomic<float>*`, atomic meters.

## Parameters (63)

Global (`osPhase` and `osRender` are NON-automatable, saved with the project): `inGain`, `outGain`, `mix`, `autoGain`, `oversampling`, `numBands` (2..5, default 3), `osPhase` (Linear/Minimum, default Linear), `osRender` (Same as live/2x/4x/8x, default Same as live),  `xover1..4` (20-20k Hz, log, defaults 120/1200/3500/8000), `bypass`.
Per band `b1_`..`b5_`: `gain`, `attack`, `sustain` (+-24 dB), `speed`, `satType`, `drive` (0-36 dB), `mix`, `mute`, `solo`, `bypass`.
All IDs use `ParameterID{id, 1}`. State: APVTS XML `PENTAGRIT_STATE`, `stateVersion=1`, plus UI size (`uiWidth`, `uiHeight`).

## Frozen C++ contracts

```cpp
// params/ParamIDs.h
namespace pentagrit {
inline constexpr int kMaxBands = 5, kNumXovers = 4, kParamVersion = 1;
namespace pid { inline constexpr const char *inGain="inGain", *outGain="outGain", *mix="mix",
  *autoGain="autoGain", *oversampling="oversampling", *numBands="numBands", *bypass="bypass";
  inline constexpr std::array<const char*,4> xover{"xover1","xover2","xover3","xover4"}; }
enum class BandParam { gain, attack, sustain, speed, satType, drive, mix, mute, solo, bypass };
juce::String bandParamID(int band0, BandParam p);   // (0, gain) -> "b1_gain"
}
// params/Parameters.h
juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
struct ParamRefs { explicit ParamRefs(juce::AudioProcessorValueTreeState&); /* cache atomics */
  std::atomic<float> *inGain,*outGain,*mix,*autoGain,*oversampling,*numBands,*bypass;
  std::array<std::atomic<float>*,4> xover;
  struct Band { std::atomic<float> *gain,*attack,*sustain,*speed,*satType,*drive,*mix,*mute,*solo,*bypass; };
  std::array<Band,kMaxBands> bands;  EngineSettings snapshot() const noexcept; };
// dsp/DspTypes.h
enum class SatType { off, tanh, softClip, tube, wavefold, hardClip };
enum class OSFactor { x1, x2, x4, x8 };
struct BandSettings { float gainDb=0, attackDb=0, sustainDb=0, speed01=.5f, driveDb=6, mix01=1;
                      SatType sat=SatType::off; bool mute=false, solo=false, bypass=false; };
struct EngineSettings { float inGainDb=0, outGainDb=0, mix01=1; bool autoGain=false, bypass=false;
  OSFactor os=OSFactor::x2; OSPhase osPhase=OSPhase::linear; OSFactor osRender=OSFactor::x1; bool osRenderSame=true; int numBands=3; std::array<float,4> xoverHz{120,1200,3500,8000};
  std::array<BandSettings,kMaxBands> bands{}; };
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
- `Saturator`: `setType`, `setDriveDb`, `process`, `static shape(SatType, float u)` (also used by the UI preview).
- `AutoGain`: `setEnabled`, `process(ref, wet)`, `getCurrentGainDb()`.
- `BandProcessor`: `setSettings(BandSettings, audible, lowFreqHintHz)`, `process`, `getTransientGainDb()`.
- `MultibandEngine`: `prepare(sr, maxBlock, numCh)`, `setSettings(EngineSettings)`, `process(AudioBuffer<float>&)` (internal chunking), `getLatencySamples()`, `static effectiveFactor(OSFactor, sr)`, `static computeAudibility(settings)`, `meters()`.
- `PentagritAudioProcessor`: `getAPVTS()`, `getMeters()`, `getPresetManager()`; UI size persisted via `getUiSize()` / `setUiSize()`.

## Oversampling phase / render factor (not implemented yet)

`EngineSettings` carries `osPhase` (`OSPhase::linear` / `minimum`), `osRender` and `osRenderSame`
(`osRenderSame == true` means "Same as live": use `os`; otherwise use `osRender`; parameter choice 1..3 -> x2, x4, x8).
`processBlock` reads `AudioProcessor::isNonRealtime()` to pick the live or the render factor.
Minimum phase uses `juce::dsp::Oversampling` with `filterHalfBandPolyphaseIIR`, reporting a rounded integer latency.

## Per-user settings (not implemented yet)

Per-user UI settings (zoom, theme, layout, tooltips, double-click, knob mode/sensitivity, meter hold/release/refresh,
show CPU, preset folder, startup preset, update-check opt-in) are NOT parameters and not part of presets. They live in a
`juce::PropertiesFile` (`Pentagrit/settings.settings` in the user app-data folder), owned later by a `SettingsManager` (WP9).

## UI zoom (not implemented yet)

UI zoom is a per-user setting, not a plugin parameter and not part of presets. The editor stores it in a
`juce::PropertiesFile` in the user app-data folder (`Pentagrit/settings.settings`).

## WP0 notes

- `src/` sources are collected with a CMake glob (`CONFIGURE_DEPENDS`) into the `PentagritCore` INTERFACE target, so work packages can add files without editing CMake.
- Stubs: crossover routes everything to band 0, other DSP modules are no-ops, engine is a passthrough with latency 0, `PresetManager` is an empty class (WP8).
