<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->
# Pentagrit user guide

Pentagrit splits the signal into 2 to 5 bands (3 by default). Each band has a transient shaper and a saturator, and the bands are summed again. Hover any control to see what it does: the description also appears in the footer.

## Signal flow

```
input -> oversampling -> input gain -> crossover -> per band [transient shaper -> saturation -> band mix -> gain, mute, solo]
      -> sum -> auto-gain -> global mix (phase-matched dry) -> oversampling -> output gain
```

The crossover is Linkwitz-Riley 4th order, so the bands add back to a flat response. The dry signal used by the global mix is delayed and phase-matched, so partial mixes do not comb.

## Band strip

Each strip is one band, with its colour. From top to bottom:

- **Header**: link toggle, then **M** (mute), **S** (solo) and **B** (bypass). Solo plays only the soloed bands (and overrides mute). Band bypass skips the shaper, saturation and band mix; the band gain, mute and solo still apply.
- **Gain-change meter**: shows what the transient shaper does to the band, in dB (+-24). The bar jumps to the largest boost (right) and cut (left) of each audio block and falls back to 0 dB at the meter release speed. Markers hold the extremes for the peak-hold time; the number is the held extreme. A flat meter means the shaper is idle (neutral settings, bypassed band, or signal below the -70 dBFS gate).
- **Attack** and **Sustain** (+-24 dB): boost or reduce the onset and the tail of each hit. **Speed** (x0.25 to x4) sets how fast the detector reacts.
- **Saturation**: click the name or the arrows to step through the curves (Off, tanh, soft clip, tube, wavefold, hard clip); the small plot previews the curve at the current drive. **Drive** is 0 to 36 dB. **Mix** blends the band's shaped and saturated signal with the untouched band.
- **Gain**: band output level, with its own peak meter on the right.

In the Compact layout the crossover display is hidden and each strip has FROM and TO fields: click one and type a frequency (for example `1.2k`).

## Crossover display (Full layout)

Drag a handle to move a crossover (20 Hz to 20 kHz); neighbours stay at least 1/3 octave apart. Double-click a handle to type a frequency, Ctrl/Cmd+click to reset it.
## Top bar and master panel

- **Bands**: 2 to 5. Raising the count pushes crossovers up to keep the spacing. Not automatable.
- **OS** (oversampling): Off, 2x, 4x or 8x around the whole chain. It reduces aliasing from the saturators and adds latency and CPU. The latency of the plugin is shown next to it and reported to the host. Not automatable.
- **Auto-gain**: matches the output level to the input level (up to +-24 dB), with a slow smoothing, and holds on silence. The master panel shows the current correction.
- **Bypass**: the whole plugin; delay-compensated, with a 20 ms crossfade.
- **In / Out**: gain before the crossover and after the global mix. **Mix**: global dry/wet.
- **Drive Comp**: scales the output by the inverse of the drive gain, so quiet signals keep their level when you raise the drive and only the harmonics change.
- The two peak meters show input and output levels.

### Monitor

- **Delta** plays only what the plugin changes (processed minus the phase-matched dry). With neutral bands, or mix at 0, it is silent. It is a session switch: not saved with the project or presets, off when the plugin loads. Bypass wins, and the output meter is tinted "DELTA" while it is on.
- **Match bypass** makes the bypassed sound as loud as the plugin output was, so A/B compares tone and not volume. It learns the level while the plugin is active (about 2 s average), freezes it during bypass and shows the applied gain next to MONITOR. It works in live playback only: offline renders keep the exact, bit-identical bypass. It is a per-user setting, not part of the project.

## Oversampling, latency and lookahead

- The latency is the oversampler latency plus the lookahead time. The host compensates it; a latency change reaches the host with a short delay.
- Settings, Audio tab: the oversampling filter is **Linear phase** (flat phase, more latency) or **Minimum phase** (low latency, slight phase shift near the filter edge). **Offline render oversampling** is used automatically when the host bounces or renders offline. **Transient lookahead** (Off, 1, 2, 5 ms) lets the shaper react before an attack arrives and adds the same amount of latency. These three are saved with the project, are not part of presets and cannot be automated.

## Band link

The link toggle in each band header links that band with the other linked bands. Moving a knob on a linked band moves the same knob of the other linked bands by the same amount (relative offsets: each band keeps its own difference, and values stop at their limits). The saturation type is copied. Mute, solo and bypass are never linked, and inactive bands are left alone. The whole edit is a single undo step. The link choice is saved with the project, survives preset changes and A/B, and is not undoable. Host automation and preset loads do not propagate.

## Presets and A/B

The preset bar has the previous and next arrows, the browser (Factory and User, with categories) and a menu (save, save as, rename, delete, duplicate, import, export). **A** and **B** are two compare slots: switching stores the current state in the active slot and recalls the other one. Presets store the sound (including band count, oversampling and drive compensation) but not the project-level settings (filter phase, render oversampling, lookahead, bypass), the band link or the delta switch. User presets live in the folder chosen in Settings, Presets; you can also pick the preset loaded in new instances there.

## Settings

The gear opens four tabs: **Interface** (zoom, visual style, Full or Compact layout, tooltips, knob drag mode and sensitivity, value step for dB knobs, meter hold, release and refresh rate, CPU readout), **Audio** (see above), **Presets** and **About & Support** (version, links, debug information, update check, reset). Interface, Presets and About settings are per user; Audio settings travel with the project.

## Undo, redo and shortcuts

- **Undo / redo**: the two buttons next to the preset bar, Ctrl/Cmd+Z, Ctrl/Cmd+Shift+Z or Ctrl+Y. Every gesture (a drag, a typed value, a wheel notch, a handle move) is one step. The history is cleared when a preset is loaded, A/B is switched or a project is restored.
- **Knobs**: drag to change, Shift for fine adjustment, double-click to type a value (`-3`, `50 %`), Ctrl/Cmd+click to reset, mouse wheel to nudge.
- **Crossover handles**: drag, double-click to type, Ctrl/Cmd+click to reset.
- **Settings**: Esc closes the panel; the Left and Right arrows change the tab.

## Workflow tips

1. **Drums**: start with 3 bands, push Attack up on the mid band and watch its gain-change meter. If the meters barely move, lower the Speed or raise Attack. Use Delta to hear exactly what you add.
2. **Saturation without a level jump**: switch Auto-gain and Drive Comp on while you raise the drive, so you judge colour and not loudness. Use Match bypass to compare against the original.
3. **Edit several bands at once**: link the bands, then move one knob to shape them together; unlink one band to keep it apart. Ctrl+Z undoes the whole move.
4. **Cleaner highs**: use 2x oversampling or more with the wavefold or hard clip curves, and keep lookahead off if you need the lowest latency (for live monitoring), or 2 to 5 ms to catch sharp attacks when mixing.
