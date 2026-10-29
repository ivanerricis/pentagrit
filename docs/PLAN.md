# Pentagrit — piano di implementazione

## Context
Plugin audio **Pentagrit**: transient shaper + saturatore/distorsore multibanda (JUCE 8, C++20, CMake), open source AGPL-3.0-or-later, repo GitHub privato `ivanerricis/pentagrit`.
Formati: VST3 + CLAP (Windows/macOS/Linux), AU + universal binary (macOS). Dev machine: Windows 11, git/gh ok, CMake 4.5-rc2 e VS Build Tools 2026 (+2022) installati; Ninja/clang-format non installati.
Piano di dettaglio progettato da un agente Opus; qui la versione operativa. Il lavoro viene poi diviso tra agenti Sonnet 5.5, ognuno in un proprio git worktree.

## Requisiti decisi con l'utente
- 5 bande (numBands selezionabile 2–5), crossover Linkwitz-Riley 4° ordine, 4 frequenze di taglio.
- Per banda: gain, transient shaper (attack/sustain dB + speed), saturazione con curva (Off, Tanh, Soft Clip, Tube, Wavefold, Hard Clip) + drive, mix dry/wet, mute, solo, bypass.
- Globali: in/out gain, mix, **autogain opzionale**, **oversampling** Off/2x/4x/8x (con latenza riportata), bypass.
- **Preset**: factory (read-only, incorporati) + utente (salva, salva con nome, rinomina, elimina, duplica, import/export, categorie, indicatore modificato, prev/next, A/B).
- UI JUCE custom; mockup HTML prima (skill ui-ux-pro-max + design-taste-frontend), logo/icona con logo-design.
- CI (Win/macOS/Linux), test, PR workflow, release automatiche. Niente trailer di attribuzione nei commit/PR.

## Struttura repo (clone in `C:\dev\pentagrit`, cache `CPM_SOURCE_CACHE=C:\dev\.cpm`)
```
CMakeLists.txt  CMakePresets.json  cmake/{CPM,Dependencies,CompilerSettings}.cmake  PluginInfo.h.in
src/ PluginProcessor.* PluginEditor.* PluginEntry.cpp
     params/{ParamIDs.h,Parameters.*}   dsp/{DspTypes,DspUtils,Meters}.h
     dsp/{Crossover,TransientShaper,Saturator,AutoGain,BandProcessor,MultibandEngine}.*
     presets/{PresetManager.*,FactoryPresets.*}   ui/{Theme,PentagritLookAndFeel,TopBar,PresetBar,CrossoverDisplay,BandStrip,CurvePreview,GlobalPanel,LevelMeter}.*
tests/ (Catch2) Crossover/TransientShaper/Saturator/AutoGain/Engine/Parameter/State/Allocation/Editor/Preset Tests
scripts/ci/ scripts/dev/   .github/{workflows,ISSUE_TEMPLATE,PULL_REQUEST_TEMPLATE.md,CODEOWNERS,dependabot.yml}
docs/ .clang-format .clang-tidy .editorconfig LICENSE README CONTRIBUTING CHANGELOG cliff.toml
```
CMake: min 3.25, `CMAKE_POLICY_VERSION_MINIMUM=3.5` (CMake 4), dipendenze via CPM pinnate (JUCE 8.0.x, clap-juce-extensions SHA, Catch2 v3) in `cmake/Dependencies.cmake`. `PentagritCore` INTERFACE lib (tutto `src/` tranne PluginEntry) condivisa da plugin e test. `juce_add_plugin`: VST3 + Standalone (+AU su Apple), codici `Ivnr`/`Pgrt`, `clap_juce_extensions_plugin`. Preset: `win-vs` (VS 2026 generator, locale), `win-ninja`, `mac-universal` (arm64;x86_64, target 11.0), `linux`, `ci-*`.

## Architettura DSP (sintesi)
Flusso: `input → Oversampler.up → inGain → crossover LR4 (cascata SVF TPT) → per banda [TransientShaper → Saturator → mix dry/wet → gain × mute/solo] → somma → AutoGain → mix globale con dry phase-matched (catena allpass) → Oversampler.down → outGain`. Bypass = ritardo di L campioni con crossfade 20 ms.
- Un solo oversampler attorno a tutto il core (latenza uguale per ogni banda); half-band FIR equiripple, latenza intera; fattore limitato a fs·F ≤ 384 kHz; `setLatencySamples` aggiornato al cambio.
- Crossover: LR4 = 2× Butterworth SVF; somma = allpass; ordinamento frequenze imposto con distanza minima 1/3 d'ottava (i valori dei parametri non sono riscritti).
- Transient shaper: 4 envelope follower (2 veloci/2 lenti), differenza in dB → coefficienti attack/sustain, speed ×4…×0.25, guard sul ripple delle basse, gate −70 dBFS, smoothing 1 ms, stereo-linked, tutto in secondi (coerente a ogni rate).
- Saturator: pre-gain drive + curve (tanh, soft clip cubico, tube asimmetrica, sin-fold, hard clip), crossfade 10 ms al cambio curva, DC blocker 5 Hz. Default "Off" = trasparente.
- AutoGain: RMS wet vs riferimento dry (400 ms), clamp ±24 dB, smoothing 200 ms, hold in silenzio.
- Realtime safe: niente allocazioni/lock, `ScopedNoDenormals`, parametri via `std::atomic<float>*`, meter atomici.

## Parametri (63, ID stabili — mai rinominare: CLAP li hasha)
Globali: `inGain`, `outGain`, `mix`, `autoGain`, `oversampling`, `numBands`, `xover1..4` (20–20k log, default 120/1200/3500/8000), `bypass`, `osPhase` (Linear/Minimum, non automatable), `osRender` (Same as live/2x/4x/8x, non automatable). Per banda `b1_`..`b5_`: `gain`, `attack`, `sustain` (±24 dB), `speed`, `satType`, `drive` (0–36 dB), `mix`, `mute`, `solo`, `bypass`. Stato: APVTS XML `PENTAGRIT_STATE`, `stateVersion=1`, + dimensioni UI. Contratti C++ (ParamIDs/Parameters/DspTypes/Meters e API dei moduli DSP) congelati da WP0 e riportati nell'output dell'agente Opus (da copiare in `docs/ARCHITECTURE.md`).

## Preset (WP8)
- `PresetManager` (message thread, indipendente dall'audio thread): carica/salva via `apvts.copyState()/replaceState()`.
- Factory: XML incorporati (JUCE BinaryData o stringhe), read-only, categorie (Drums, Bass, Mix Bus, Creative…), ~10–15 preset iniziali.
- Utente: file `.pgp` (XML + `stateVersion`, nome, categoria, autore) in `%APPDATA%/Pentagrit/Presets` · `~/Library/Application Support/Pentagrit/Presets` · `~/.config/Pentagrit/Presets`; sottocartelle = categorie.
- Operazioni: save, save as, rename, delete, duplicate, import, export; nomi sanitizzati, niente sovrascrittura accidentale, preset corrotti ignorati con messaggio.
- Stato "modificato" (*) sincronizzato con i parametri; prev/next; A/B compare (due slot di stato in memoria).
- Il preset corrente è salvato anche nello stato del DAW.
- UI: `PresetBar` nel TopBar (combo/browser, frecce, menu Save/Rename/Delete/Import/Export, A/B).
- Test: round-trip, nomi non validi, file corrotti, versioning, factory tutti caricabili con parametri nei range.

## Work package (ogni agente: worktree `C:\dev\pt-wpN`, branch dedicato, PR con titolo conventional-commit, squash)
| WP | Branch | File posseduti | Dipende da |
|---|---|---|---|
| **WP0 Scaffold** (per primo, sequenziale) | `main` | CMake, preset, dipendenze pinnate, ParamIDs/Parameters completi, tutti gli header DSP con API congelate + stub, Processor/Editor stub, tests/CMakeLists con tutti i file placeholder, TestSignals.h, config lint, LICENSE, README minimo; `gh repo create ivanerricis/pentagrit --private --source . --push` | — |
| WP1 Crossover | `feat/dsp-crossover` | `dsp/Crossover.*`, `CrossoverTests` | WP0 |
| WP2 Transient shaper | `feat/dsp-transient` | `dsp/TransientShaper.*`, test | WP0 |
| WP3 Saturator + AutoGain | `feat/dsp-saturation` | `dsp/Saturator.*`, `dsp/AutoGain.*`, test | WP0 |
| WP4 Engine + processor | `feat/engine` | `BandProcessor`, `MultibandEngine`, `PluginProcessor`, `Parameters.cpp::snapshot()`, Engine/Parameter/State/Allocation tests | WP0 (merge dopo WP1–3) |
| WP5a Mockup UI + brand | `design/ui` | mockup HTML (Artifact), palette/tipografia, logo e icona (logo-design) | — (può partire subito, non serve il repo) |
| WP5b UI JUCE | `feat/ui` | `PluginEditor.*`, `ui/*` (escluso PresetBar), EditorTests | WP0 + WP5a approvato |
| WP6 CI/CD | `ci/pipelines` | `.github/workflows/{build,ci,release}.yml`, `scripts/ci/*`, `cliff.toml`, dependabot | WP0 |
| WP7 Governance + docs | `docs/governance` | PR/issue template, CODEOWNERS, README, CONTRIBUTING, CHANGELOG, THIRD_PARTY_NOTICES, `docs/*`, `scripts/dev/*` | WP0 |
| WP8 Preset | `feat/presets` | `presets/*`, `ui/PresetBar.*`, PresetTests | WP0 (UI integrata dopo WP5b) |

Ordine: WP0 → (WP1, WP2, WP3, WP4-su-stub, WP5a, WP6, WP7, WP8 in parallelo; WP5b dopo approvazione mockup) → merge WP1–3 → WP4 → WP5b → WP8 → integrazione finale (pluginval strictness 10 su 3 OS, tag `v0.1.0`). Max 3–4 agenti simultanei sulla stessa macchina (build JUCE ~3–6 min ciascuno), `-j` = metà dei core, cache CPM condivisa.

## CI/CD (WP6)
- `build.yml` riusabile: matrix windows-2022 (Ninja+MSVC, sccache), macos-14 (universal, ccache), ubuntu-22.04 (ccache, xvfb); build Release, ctest, pluginval strictness 10 sul VST3, `auval` su macOS, clap-validator (advisory), artifact.
- `ci.yml`: PR + push su main, concurrency cancel, lint (clang-format 18, titolo PR conventional commit), clang-tidy advisory; macOS solo su main / label `ci:macos` (minuti ×10 su piano Free).
- `release.yml`: tag `v*` → verifica versione = CMake → build 3 OS + zip (VST3, CLAP, AU, Standalone, LICENSE) → note da git-cliff → GitHub Release. Azioni pinnate per SHA. macOS: firma ad-hoc ora; firma/notarizzazione Apple in fase 2 (`docs/MACOS_SIGNING.md`, serve account Apple Developer).
- Branch protection: ruleset via `gh api` (PR obbligatoria, check richiesti, squash only); su repo privato Free potrebbe richiedere Pro → documentato.

## Rischi principali
1. AGPL: distribuire binari pubblicamente implica rendere pubblico il sorgente → decidere prima della prima release.
2. CMake 4.5-rc2 locale: mitigato con policy minimum; meglio installare la stable.
3. Pin compatibili JUCE ↔ clap-juce-extensions; ID parametro immutabili.
4. AU: codici con maiuscola, cache auval, firma ad-hoc, `xattr -dr com.apple.quarantine`.
5. pluginval 10: block size strani, rate fino a 192 kHz, fuzzing di `oversampling`/`numBands` (latenza runtime sicura).
6. Cambiare `numBands`/oversampling può fare click (non automatabili, documentato).
7. Minuti CI macOS/Windows su piano Free.

## Verifica
- Unit test Catch2: crossover (somma piatta ±0.01 dB, −6 dB a fc, pendenza), transient (burst, sine stazionaria, speed, consistenza 1x/4x, NaN), saturator (curve, armoniche, DC, continuità), autogain (convergenza, clamp), engine (flatness, latenza = bypass, mix phase-matched, aliasing 8x vs 1x, chunking, fuzz), allocazioni = 0 in processBlock, parametri/stato/preset round-trip, editor lifecycle.
- CI: pluginval 10 (3 OS), auval, clap-validator.
- Manuale (utente): carica VST3 e CLAP in Reaper su Windows, controlla compensazione latenza, automazione, salvataggio progetto, CPU a 8x, preset.
- Dry run release: tag `v0.1.0-rc1`.

## Preset di fabbrica iniziali (WP8; valori da rifinire a orecchio)
Ogni preset = XML dello stato completo; default di partenza trasparente (satType Off, gain 0).
| Categoria | Nome | Idea |
|---|---|---|
| Init | Init | Tutto neutro, 5 bande, OS 2x |
| Drums | Punchy Kick | b1–b2 attack +6/+9 dB, saturazione Tanh lieve sulla b2, autogain on |
| Drums | Snare Snap | b3 attack +9, b4 attack +6, sustain −4 sulle basse |
| Drums | Room Tamer | sustain −9 su b3–b5, speed lenta |
| Drums | Overhead Air | b5 attack +3, Soft Clip leggero, mix 60% |
| Bass | Bass Grit | b2–b3 Tube drive 12 dB, mix 70%, b1 pulita |
| Bass | Sub Tight | b1 sustain −6, Tanh drive 6 |
| Mix Bus | Glue Warmth | Tube drive 3 su tutte le bande, mix 50%, autogain on |
| Mix Bus | Gentle Punch | attack +3 su b2–b4, sustain +1, saturazione Off |
| Creative | Wavefold Shimmer | b4–b5 Wavefold drive 18, mix 40% |
| Creative | Crush Hats | b5 Hard Clip drive 24, attack +6 |
| Creative | Sustain Bloom | sustain +12 su b2–b4, Tanh drive 9, speed lenta |
| Creative | Full Destroy | tutte Hard Clip/Wavefold alti, OS 8x, autogain on |
Implementazione: file `.pgp` sorgente in `resources/presets/*.xml` incorporati via `juce_add_binary_data`; test che li carica tutti e verifica parametri nei range e nessun NaN rendendo un burst di rumore.

## Contratti C++ congelati da WP0 (estratto, da copiare in docs/ARCHITECTURE.md)
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
// dsp/Meters.h  (audio thread scrive relaxed, UI legge)
struct MeterState { std::array<std::atomic<float>,kMaxBands> bandPeakDb, bandTransientGainDb;
  std::atomic<float> inPeakDb{-100}, outPeakDb{-100}, autoGainDb{0};
  std::array<std::atomic<float>,4> effectiveXoverHz; std::atomic<int> latencySamples{0}; };
```
Convenzioni moduli DSP: `prepare()` può allocare (solo in prepareToPlay); `setProcessingRate(double)`, `reset()`, `process()` sono `noexcept` e senza allocazioni. API per modulo:
- `Crossover`: `prepare(baseRate, procRate, maxBlock, numCh)`, `setNumBands`, `setFrequencies(array<float,4>)`, `getEffectiveFrequencies`, `static enforceOrdering(hz, numBands, baseRate)`, `split(in, array<AudioBlock<float>,5>&)`, `processAllpassChain(io)`.
- `TransientShaper`: `setParameters(attackDb, sustainDb, speed01)`, `setLowFrequencyHint(hz)`, `process(block)`, `getLastGainDb()`.
- `Saturator`: `setType`, `setDriveDb`, `process`, `static shape(SatType, float u)` (usata anche dall'anteprima UI).
- `AutoGain`: `setEnabled`, `process(ref, wet)`, `getCurrentGainDb()`.
- `BandProcessor`: `setSettings(BandSettings, audible, lowFreqHintHz)`, `process`, `getTransientGainDb()`.
- `MultibandEngine`: `prepare(sr, maxBlock, numCh)`, `setSettings(EngineSettings)`, `process(AudioBuffer<float>&)` (chunking interno), `getLatencySamples()`, `static effectiveFactor(OSFactor, sr)`, `static computeAudibility(settings)`, `meters()`.
- `PentagritAudioProcessor`: `getAPVTS()`, `getMeters()`, `getPresetManager()`.

## Dove vive il piano (continuità tra sessioni)
- Copia locale: `C:\Users\Utente\.claude\plans\ancient-wobbling-parnas.md` (solo su questa macchina).
- Appena approvato: WP0 crea il repo privato e committa questo file come **`docs/PLAN.md`** (+ `docs/ARCHITECTURE.md` con i contratti sopra). Una sessione cloud o futura clona il repo e riparte da lì; lo stato dei WP si traccia con una issue GitHub per pacchetto (label `wp`) e una milestone `v0.1.0`.
- Nota: in plan mode non posso ancora creare il repo né fare commit; è il primo passo dopo l'approvazione.

## Primi passi dopo l'approvazione
1. Eseguire WP0 (io o un agente Sonnet), creare il repo privato su GitHub, committare `docs/PLAN.md`, creare issue/milestone dei WP.
2. Lanciare in parallelo gli agenti Sonnet 5.5 per WP1, WP2, WP3, WP5a, WP6, WP7 (poi WP4, WP8, WP5b).
