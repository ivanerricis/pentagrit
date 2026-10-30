# Pentagrit UI theme (WP5a)

Source of truth for the JUCE implementation (WP5b). Mockup: `C:\dev\pt-design\index.html` (generated from `mockup.template.html` + `mockup.js` + `gen_brand.py`).
Base window 1100 x 680 px (logical), resizable later with a uniform scale factor. Dark theme only.

## Colour tokens

| Token | Hex | Use |
|---|---|---|
| `bg0` | `#0E1013` | window ground, recessed sections inside strips |
| `bg1` | `#151920` | panels (top bar, strips, master, footer, crossover) |
| `bg2` | `#1D222B` | controls at rest (buttons, segments, curve box, meter troughs) |
| `bg3` | `#2A313D` | hover, knob track, menus, grid lines |
| `bg4` | `#394252` | menu hover, identity line in curve preview |
| `ink` | `#E8ECF2` | primary text, active toggle fill, knob pointer |
| `ink2` | `#98A2B3` | secondary text, readouts |
| `ink3` | `#5F6A7C` | labels, captions, disabled |
| `band1..5` | `#FF5A5F` `#FFB347` `#F9F871` `#4CD7A6` `#4DA3FF` | band identity (strip bar, knob arcs, spectrum, curve, meter) |
| `mute` / `solo` / `bypass` | `#FF5A5F` / `#F9F871` / `#4DA3FF` | M / S / B active fill (text `bg0`) |
| `warn` | `#FF5A5F` | meter clip (> -3 dBFS), global BYPASS on |
| `modified` | `#FFB347` | the `*` next to a modified preset name |

Rules: surfaces are separated by tone steps (bg0 < bg1 < bg2 < bg3), not by borders. Active toggles are inverted (`ink` fill, `bg0` text); colour is reserved for bands. Contrast: ink on bg1 is 14:1, ink2 on bg1 is 6.7:1, ink3 on bg1 is 3.3:1 (labels only, never values). Muted/silenced strips draw at 45% opacity (content) and unused strips (numBands < 5) at 28%.
Spectrum fill alpha: band tint 0.18, spectrum 0.70 (silent: 0.10 / 0.25).

## Typography

- UI text: **IBM Plex Sans** (Regular 400, Medium 500, SemiBold 600). Licence: SIL Open Font License 1.1, free to embed and redistribute with the plugin (ship `OFL.txt` in THIRD_PARTY_NOTICES). Embed via `juce_add_binary_data`, load with `juce::Typeface::createSystemTypefaceFor`.
- Numeric readouts and frequencies: **IBM Plex Mono** (Regular 400, Medium 500), same licence. Tabular digits so values do not jitter.
- Fallback if fonts are not embedded: system sans (`Segoe UI` / `SF Pro` / `DejaVu Sans`).

| Role | Font | Size px | Notes |
|---|---|---|---|
| Wordmark in top bar | Plex Sans 600 | 13 | tracking +0.14 em |
| Preset name | Plex Sans 500 | 12.5 | |
| Band name (B1..B5) | Plex Sans 600 | 12.5 | band colour |
| Button / segment text | Plex Sans 500-600 | 11-11.5 | AUTO-GAIN / BYPASS 11, tracking +0.06 em |
| Section / knob labels | Plex Sans 500 | 10 | uppercase, tracking +0.08 em, `ink3` |
| Values, frequencies | Plex Mono 400-500 | 10-10.5 | `ink2` (live gain readout `ink`) |
| Footer hint | Plex Sans 400 | 11 | `ink2` |

## Spacing grid and sizes

Base unit 2 px, common steps 4 / 6 / 8 / 12 / 16. Radius: 3 px small controls, 4 px buttons and sections, 6 px panels, 8 px modal.

| Element | Size |
|---|---|
| Top bar | height 52, padding 0 10, gap 8 |
| Crossover display | x 8, y 60, 1084 x 168 (plot area top 26, bottom 18, side 18 px) |
| Body area | y 228 to 654 (flex), padding 8, gap 6 |
| Band strip | min 168 wide, `flex 1` (active strips share the width left of Master: 3 bands about 290 px each, 5 bands 168 px), padding 8/8/6, gap 6, colour bar 3 px at left |
| Master panel | 190 wide |
| Footer | height 26, padding 0 12 |
| Band knob | 38 px (Gain 42, Master 44), arc 270 deg from 135 to 405 deg, track 4 px, pointer 2 px; label above, value below |
| M / S / B buttons | 20 x 20, gap 2 |
| Preset prev/next/menu | 28-30 x 30; preset name 168 x 30; A/B 26 x 30 |
| Segment buttons (Bands, OS) | min 30 x 26 in a 2 px padded trough |
| Saturation selector | arrows 22 x 22 + name 22 high |
| Curve preview | 152 x 44 (x -1..1, y clipped at +-1.15) |
| Transient indicator | height 14, centre-zero, +-24 dB full scale |
| Band meter | 2 bars 8 wide x 54 high; master meters 12 x 140 |
| Crossover handle | 2 px bar + label chip 18 high (value in Hz/kHz); hit zone +-14 px |

Frequency axis: log 20 Hz to 20 kHz, `x = pad + log(f/20)/log(1000) * (W - 2 pad)`. Handles keep a 1/3 octave minimum distance (matches DSP `enforceOrdering`). Unused handles (numBands < n+1) are hidden.

## Interaction

- Knobs: vertical drag (about 170 px = full range), Shift = fine (x4), double-click = type a value, Ctrl/Cmd+click = reset to default, wheel = 2% steps. Use `juce::Slider::RotaryVerticalDrag`, `setDoubleClickReturnValue`.
- M/S/B are per-band toggles; solo dims all non-solo strips; a muted strip dims to 45%.
- **Collapse (numBands < 5):** only strips 1..N exist on screen; they widen to fill the body (use `juce::FlexBox` / `Grid` with `flexGrow 1`, minimum width 168, and hide strips N+1..5 with `setVisible(false)`). Master stays 190 px wide. Band colours stay in order (band1..bandN). The crossover display draws only the N active band regions and N-1 handles. Parameters of hidden bands keep their values. **Default numBands = 3**, default crossovers **120 / 1200 / 3500 / 8000 Hz** (3 bands use 120/1200, 4 bands 120/1200/3500, 5 bands all four). When the band count rises, handle values are pushed up to keep the 1/3 octave spacing.
- **Settings panel:** the gear icon (top bar right, 30 x 30) opens a modal overlay inside the editor (780 px wide card, 4 tabs: Interface, Audio, Presets, About & Support; Esc or click outside closes). Rows are `bg2` bars (label + description left, control right); controls are segmented selectors, switches (38 x 22), range sliders and buttons on `bg3`. Implement as a `Component` overlay with a `TabbedComponent` (or four child components); see the storage table below.
- **CPU readout (footer, right):** text `CPU 3.2%`, `ink2`, `Plex Mono 10.5`, refreshed at the meter refresh rate from `MeterState::cpuLoad` (add `std::atomic<float> cpuLoad` written by the processor from `AudioProcessLoadMeasurer` or a timing wrapper around `process`). Hidden when `showCpu` is off.
- Preset name shows `*` (colour `modified`) while the state differs from the stored preset. Factory presets are read-only (Rename/Delete disabled, Save opens Save As).
- A/B: two in-memory state slots; switching copies the current state first.
- Footer shows a one-line hint for the hovered control (and 2.6 s toasts for preset actions).
- Animations: meters and spectrum at 30-60 Hz via `juce::Timer`/`VBlankAttachment`; no easing needed. Use `MeterState` atomics for real data.

## Saturation curve preview (matches `Saturator::shape`)

`y = shape(x * g)`, `g = 1 + drive/36 * 6` (illustrative, not the real gain); off = identity.
tanh: `tanh(u)`; soft clip: cubic `1.5u - 0.5u^3` clamped to +-1; tube: asymmetric `(tanh(u+0.35) - tanh(0.35)) / (1 - tanh(0.35))` with negative side x0.85; wavefold: `sin(u*pi/2)`; hard clip: clamp +-1. In JUCE call `Saturator::shape(type, u)` directly.

## Mockup component to JUCE component

| Mockup | JUCE |
|---|---|
| `#plug` window | `PluginEditor` (fixed aspect 1100:680, `setResizable` + `setFixedAspectRatio`) |
| Top bar | `TopBar : Component` (owns preset bar, selectors, toggles) |
| Brand mark | `juce::Drawable::createFromImageData` of `icon.svg`-style mark, or hand-built `juce::Path` (5 wedges) |
| Preset bar (prev/name/next/menu/A-B) | `PresetBar` (WP8 owns it): `TextButton` x3 + preset browser built as nested `juce::PopupMenu`: root has two submenu items `Factory >` and `User >` (`root.addSubMenu`), each holding one submenu per category (Init, Drums, Bass, Mix Bus, Creative; for User the category is the subfolder name under the presets directory), each holding the presets as items. The current preset is ticked (`PopupMenu::Item::setTicked(true)`). Factory items are read-only (Rename/Delete disabled, Save redirects to Save As). Prev/next walk the same flattened order as the menu (Factory categories in order, then User categories). A second `PopupMenu` holds Save/Save As/Rename/Delete/Duplicate/Import/Export. Save As opens an `AlertWindow` with a name editor and an editable `addComboBox` for the category listing existing user folders (typing a new one creates the folder); Rename/Delete also use `AlertWindow`. |
| Bands / OS segmented selector | `SegmentedSelector : Component` (custom) bound with `ComboBoxParameterAttachment` / `ParameterAttachment` (numBands, oversampling) |
| Latency readout | `Label`, text from `MeterState::latencySamples`, refreshed by timer |
| AUTO-GAIN / BYPASS | `ToggleButton` + `ButtonParameterAttachment`, custom look in `PentagritLookAndFeel::drawToggleButton` |
| Crossover display | `CrossoverDisplay : Component, Timer` - draws grid, band fills and spectrum (fake or from engine later); handles with `mouseDown/Drag/DoubleClick`, writes `xover1..4` via `RangedAudioParameter::beginChangeGesture/setValueNotifyingHost`; reads `MeterState::effectiveXoverHz` |
| Band strip | `BandStrip : Component` (one per band, constructed with band index + colour) |
| M / S / B | `ToggleButton` + `ButtonParameterAttachment` (`bN_mute/solo/bypass`) |
| Transient indicator | `TransientIndicator : Component` reading `bandTransientGainDb[i]` |
| Attack / Sustain / Speed / Drive / Mix / Gain knobs | `juce::Slider` (RotaryVerticalDrag, no text box) + `SliderParameterAttachment`; labels and value text drawn by a small `LabelledKnob` wrapper; `PentagritLookAndFeel::drawRotarySlider` |
| Saturation selector | `SatTypeSelector` (two arrow buttons + name) with `ParameterAttachment` on `bN_satType` |
| Curve preview | `CurvePreview : Component` using `Saturator::shape` |
| Band meter / master meters | `LevelMeter : Component` from `bandPeakDb`, `inPeakDb`, `outPeakDb` |
| Master panel | `GlobalPanel : Component` (inGain, outGain, mix knobs, in/out meters, auto-gain readout from `autoGainDb`) |
| Footer hint + CPU readout | `Label` driven by a `MouseListener` on children (`getTooltip`-like hint text); right-aligned `Label` for `CPU x.x%` from `MeterState::cpuLoad` |
| Gear / settings panel | icon `Button` toggling a `SettingsPanel : Component` overlay (see storage table); tabs via `TabbedComponent` or a segmented selector |
| Menus | `juce::PopupMenu` with `LookAndFeel::drawPopupMenuItem` in `bg3` |
| Modal | `juce::DialogWindow` or in-editor `Component` overlay |

## Brand

Files in `C:\dev\pt-design\brand\`: `logo.svg` (mark + wordmark, colour, for dark), `logo-mono.svg` (one colour `#0E1013`, for light), `icon.svg` (256 px rounded tile, `#0E1013`).
Mark: a pentagon split into five wedges in the five band colours, separated by 7 px gaps (the gaps are the crossovers). Wordmark: custom monoline capitals (cap height 64, stem 11, drawn as paths and scaled x1.25 in the lockup), no live `<text>`, no font dependency; the SVGs contain only paths. Colour version uses ink `#E8ECF2` for the letters, the mono version `#0E1013`. Use the mono mark below 24 px or on one-colour print. App icon: tile plus mark at 66% scale.

## Themes and layouts

Dark (default) uses the tokens above. Light palette (same token names, set on the editor via `PentagritLookAndFeel::setColourScheme`):

| Token | Light hex |
|---|---|
| `bg0` / `bg1` / `bg2` / `bg3` / `bg4` | `#E6EAF0` / `#F6F8FA` / `#E4E9EF` / `#CDD5DF` / `#B6C1CE` |
| `ink` / `ink2` / `ink3` | `#14181F` / `#4A5566` / `#76808F` |
| `band1..5` | `#E5484D` `#E58A00` `#B8A400` `#14A478` `#1F7FE0` (darker so they stay legible on light panels) |

Layouts: Full = 1100 x 680 (crossover display + strips). Compact = 1100 x 540: the crossover display is hidden; each strip shows two numeric From / To fields (Plex Mono 10.5, 68 px wide, accept Hz or `1.2k`, same 1/3 octave ordering rule; the outer edges 20 Hz / 20 kHz are read-only). The editor aspect ratio changes with the layout (`setFixedAspectRatio` is updated and the size is re-applied).

## Settings -> storage -> JUCE implementation

Per-user settings live in one `juce::PropertiesFile` (`ApplicationProperties`, file `Pentagrit/settings.xml` in the user app-data folder, shared by all instances; the mockup uses one localStorage key as stand-in). Audio-tab settings are plugin state: extra APVTS parameters / state properties saved with the project, never in the PropertiesFile.

| Setting | Storage (PropertiesFile key / APVTS) | JUCE implementation |
|---|---|---|
| Zoom 75/100/125/150/200 % | `uiZoom` (double, 1.0) | `Component::setScaleFactor` on the editor (or `setTransform(AffineTransform::scale(z))`), editor size = base size x z |
| Theme Dark / Light | `uiTheme` ("dark"/"light") | two `ColourScheme` presets in `PentagritLookAndFeel`; `lookAndFeelChanged()` + repaint; band colours come from the scheme |
| Layout Full / Compact | `uiLayout` ("full"/"compact") | `PluginEditor::resized` switches the layout; hides `CrossoverDisplay`, shows per-strip `Label` fields (`setEditable(true)`) bound to `xover1..4` via `ParameterAttachment`; `setSize` 1100x680 or 1100x540 |
| Tooltips on/off | `tooltips` (bool, true) | `TooltipWindow` member (message-thread owned) created/destroyed or `setMillisecondsBeforeTipAppears`; controls call `setTooltip` with the description |
| Value step (dB knobs): Continuous / 0.1 / 0.5 / 1 | `valueStep` (dB, 0 = continuous) | `Slider::snapValue` override; Shift (fine) ignores it; typed values are not rounded. The legacy `dblClick` key is ignored (double-click always types, Ctrl/Cmd+click always resets) |
| Knob drag: Vertical / Circular | `knobDrag` ("vertical"/"circular") | `Slider::RotaryVerticalDrag` vs `RotaryHorizontalVerticalDrag` / `Rotary` style via `setSliderStyle`; `setMouseDragSensitivity` for vertical |
| Knob sensitivity (Fine..Fast) | `knobSens` (0..100, 50) | `Slider::setMouseDragSensitivity(int(170 / 4^((v-50)/50)))`; circular mode scales the angle delta (`SliderStyle::Rotary` uses `setRotaryParameters`) |
| Meters: peak hold | `meterHold` (ms, 0 = off, 99999 = infinite, 1000) | `LevelMeter` hold-marker timer |
| Meters: release speed | `meterRelease` (dB/s, 20) | `LevelMeter` ballistics (fall = release x dt) |
| Meters: refresh | `meterRefresh` (30/60, 60) | `Timer::startTimerHz` (or `VBlankAttachment` throttled) in the editor |
| Show CPU readout | `showCpu` (bool, true) | footer `Label::setVisible`; value from `MeterState::cpuLoad` |
| User preset folder | `presetFolder` (string, platform default) | `PresetManager::setUserFolder`; `FileChooser::launchAsync` for Choose..., `File::revealToUser()` for Open folder |
| Startup preset | `startupPreset` ("last" / "init" / "factory:Name" / "user:Cat/Name") + `lastPreset` | read in the processor/editor constructor; nested `PopupMenu` reused from the preset browser |
| Check for updates on startup | `checkUpdates` (bool, **false**) | opt-in only; one HTTPS request to the GitHub Releases API (`URL::createInputStream`) on a background thread; "Check now" does the same on demand; no personal data sent |
| Reset all settings | clears the PropertiesFile keys above (not presets, not APVTS) | `AlertWindow::showOkCancelBox`, then `PropertiesFile::removeValue` for every key and re-apply |
| Oversampling filter phase Linear / Minimum | **APVTS parameter** `osPhase` (choice, saved with project, not automatable) | selects the half-band filter set in the oversampler (linear-phase FIR vs minimum-phase IIR/FIR); updates `setLatencySamples` |
| Offline render oversampling Same / 2x / 4x / 8x | **APVTS parameter** `osOffline` (choice, saved with project, not automatable) | in `prepareToPlay`/`processBlock`: `isNonRealtime()` selects this factor instead of `oversampling`; latency readout shows both values from `MeterState::latencySamples` and the engine's `getLatencySamples()` per mode |
| Debug info | none (generated) | text assembled from `JucePlugin_VersionString`, `SystemStats::getOperatingSystemName()`, `PluginHostType`, sample rate, block size, latency, oversampling; `SystemClipboard::copyTextToClipboard`, button label "Copied" for 2 s |

Note: `osPhase` and `osOffline` are new parameter IDs (stable like all others, never rename); they extend the 61-parameter list and must be added to `ParamIDs.h` and `createParameterLayout()` by whoever owns WP0/WP4.
