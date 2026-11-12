// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Generators for the README assets (screenshots and audio examples). Every case is hidden ([.docs]) so it
// never runs in CI or in the default ctest run. Use scripts/dev/make_docs_assets.sh, or run
//   PentagritTests "[.docs]"
// with these environment variables:
//   PENTAGRIT_DOCS_DIR  output root (required; images/ and audio/ are created inside it)
//   PENTAGRIT_DOCS_SRC  optional folder with extra source loops (*.wav) rendered like the synthetic one
#include "PluginEditor.h"
#include "TestSignals.h"
#include "presets/PresetManager.h"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <juce_audio_formats/juce_audio_formats.h>

using namespace pentagrit;

namespace
{
constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;
constexpr double kClipSeconds = 10.0;

juce::File docsDir(const juce::String& sub)
{
    const auto root = juce::SystemStats::getEnvironmentVariable("PENTAGRIT_DOCS_DIR", {});
    if (root.isEmpty())
        return {};
    auto dir = juce::File(root).getChildFile(sub);
    dir.createDirectory();
    return dir;
}

void setValue(PentagritAudioProcessor& p, const juce::String& id, float value)
{
    auto* param = p.getAPVTS().getParameter(id);
    param->setValueNotifyingHost(param->convertTo0to1(value));
}

bool loadFactory(PentagritAudioProcessor& p, const juce::String& name)
{
    auto& pm = p.getPresetManager();
    for (const auto& e : pm.getFactoryPresets())
    {
        if (e.name == name)
            return pm.loadPreset(e).wasOk();
    }
    return false;
}

// ---- Screenshots ----

void writePng(const juce::Image& img, const juce::File& out)
{
    out.deleteFile();
    juce::FileOutputStream stream(out);
    REQUIRE(stream.openedOk());
    REQUIRE(juce::PNGImageFormat().writeImageToStream(img, stream));
}

// Values that make every meter of the editor look alive (as if audio were playing).
void seedMeters(MeterState& m, int bands)
{
    constexpr std::array<float, kMaxBands> peaks{-9.0f, -4.5f, -7.0f, -13.0f, -19.0f};
    constexpr std::array<float, kMaxBands> gain{5.0f, 3.0f, -2.0f, 4.0f, 1.5f};
    constexpr std::array<float, kMaxBands> gainMin{-2.5f, -1.0f, -6.0f, -1.5f, 0.0f};
    constexpr std::array<float, kMaxBands> gainMax{7.5f, 4.5f, 1.0f, 6.0f, 3.0f};
    for (int b = 0; b < bands; ++b)
    {
        const auto i = static_cast<size_t>(b);
        m.bandPeakDb[i].store(peaks[i]);
        m.bandTransientGainDb[i].store(gain[i]);
        m.bandTransientGainMinDb[i].store(gainMin[i]);
        m.bandTransientGainMaxDb[i].store(gainMax[i]);
    }
    m.inPeakDb.store(-5.5f);
    m.outPeakDb.store(-3.0f);
    m.autoGainDb.store(-2.4f);
    m.latencySamples.store(240);
    m.cpuLoad.store(0.021f);
}

void renderEditor(const juce::File& dir, ui::Style style, int bands, const juce::String& preset)
{
    PentagritAudioProcessor processor;
    // The editor is created first and the preset loaded afterwards: the editor only re-lays out its
    // crossover display when the band count changes while it is open.
    PentagritAudioProcessorEditor editor(processor);
    REQUIRE(loadFactory(processor, preset));
    REQUIRE(static_cast<int>(processor.getAPVTS().getRawParameterValue(pid::numBands)->load()) == bands);
    if (bands == 5)
    {
        // Knobs that agree with the seeded gain-change meter ranges (see seedMeters).
        constexpr std::array<float, kMaxBands> attackDb{6.0f, 4.5f, 0.0f, 6.0f, 3.0f};
        constexpr std::array<float, kMaxBands> sustainDb{-3.0f, 0.0f, -6.0f, 0.0f, 0.0f};
        for (int b = 0; b < bands; ++b)
            for (auto bp : {BandParam::attack, BandParam::sustain})
            {
                auto* param = processor.getAPVTS().getParameter(bandParamID(b, bp));
                REQUIRE(param != nullptr);
                const auto v = (bp == BandParam::attack ? attackDb : sustainDb)[static_cast<size_t>(b)];
                param->setValueNotifyingHost(param->convertTo0to1(v));
            }
    }
    // The engine publishes the crossover frequencies in use; there is no audio here, so mirror the
    // parameters.
    for (size_t i = 0; i < pid::xover.size(); ++i)
        processor.getMeters().effectiveXoverHz[i].store(
            processor.getAPVTS().getRawParameterValue(pid::xover[i])->load());
    ui::UiOptions o;
    o.style = style;
    o.layout = ui::Layout::full;
    o.meterHoldSec = -1.0f; // keep the hold markers
    editor.setUiOptions(o);
    seedMeters(processor.getMeters(), bands);
    // Several refreshes so the smoothed meters settle on the seeded values.
    for (int i = 0; i < 4; ++i)
        editor.refreshNow();

    for (float scale : {1.0f, 2.0f})
    {
        const auto img = editor.createComponentSnapshot(editor.getLocalBounds(), false, scale);
        REQUIRE(img.getWidth() == static_cast<int>(ui::kBaseWidth * scale));
        const juce::String name = "editor-" + juce::String(ui::styleId(style)) + "-" + juce::String(bands) +
                                  "bands" + (scale > 1.5f ? "@2x" : "") + ".png";
        writePng(img, dir.getChildFile(name));
    }
}

// ---- Audio ----

using Stereo = std::array<test::Signal, 2>;

void addDecay(test::Signal& dst, const test::Signal& hit, size_t at, float gain)
{
    for (size_t i = 0; i < hit.size() && at + i < dst.size(); ++i)
        dst[at + i] += gain * hit[i];
}

test::Signal makeKick(size_t n)
{
    test::Signal s(n);
    double phase = 0.0;
    for (size_t i = 0; i < n; ++i)
    {
        const double t = static_cast<double>(i) / kSampleRate;
        const double f = 45.0 + 110.0 * std::exp(-t * 28.0);
        phase += 2.0 * test::kPi * f / kSampleRate;
        const double click = std::exp(-t * 900.0) * 0.4;
        s[i] = static_cast<float>(std::sin(phase) * std::exp(-t * 9.0) + click);
    }
    return s;
}

test::Signal makeSnare(size_t n)
{
    const auto nz = test::noise(n, 77);
    test::Signal s(n);
    float prev = 0.0f;
    for (size_t i = 0; i < n; ++i)
    {
        const double t = static_cast<double>(i) / kSampleRate;
        const float hp = nz[i] - prev * 0.6f; // crude high-pass
        prev = nz[i];
        const double tone = std::sin(2.0 * test::kPi * 190.0 * t) * std::exp(-t * 30.0) * 0.5;
        s[i] = static_cast<float>(hp * std::exp(-t * 22.0) * 0.7 + tone);
    }
    return s;
}

test::Signal makeHat(size_t n, unsigned seed)
{
    const auto nz = test::noise(n, seed);
    test::Signal s(n);
    float prev = 0.0f;
    for (size_t i = 0; i < n; ++i)
    {
        const double t = static_cast<double>(i) / kSampleRate;
        const float hp = nz[i] - prev; // strong high-pass
        prev = nz[i];
        s[i] = static_cast<float>(hp * std::exp(-t * 90.0) * 0.35);
    }
    return s;
}

test::Signal makeBassNote(size_t n, double freq)
{
    test::Signal s(n);
    for (size_t i = 0; i < n; ++i)
    {
        const double t = static_cast<double>(i) / kSampleRate;
        const double env = std::min(1.0, t * 200.0) * std::exp(-t * 3.5);
        const double ph = 2.0 * test::kPi * freq * t;
        // Sine plus a few harmonics: gives the saturator something to bite into.
        s[i] = static_cast<float>(env * (std::sin(ph) + 0.4 * std::sin(2.0 * ph) + 0.2 * std::sin(3.0 * ph)));
    }
    return s;
}

// Deterministic 120 BPM loop: kick, snare, hats and a short bass line, `kClipSeconds` long.
Stereo makeDrumLoop()
{
    const auto total = static_cast<size_t>(kClipSeconds * kSampleRate);
    const double step = 60.0 / 120.0 / 4.0 * kSampleRate; // a sixteenth note
    test::Signal l(total, 0.0f), r(total, 0.0f);

    const auto kick = makeKick(static_cast<size_t>(0.45 * kSampleRate));
    const auto snare = makeSnare(static_cast<size_t>(0.30 * kSampleRate));
    const auto hatClosed = makeHat(static_cast<size_t>(0.08 * kSampleRate), 11);
    const auto hatOpen = makeHat(static_cast<size_t>(0.25 * kSampleRate), 12);
    constexpr std::array<double, 4> bassHz{55.0, 55.0, 65.41, 49.0};
    std::vector<test::Signal> bass;
    for (double f : bassHz)
        bass.push_back(makeBassNote(static_cast<size_t>(0.45 * kSampleRate), f));

    // Kick pattern over a 16-step bar.
    constexpr std::array<int, 16> kicks{1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0};
    constexpr std::array<int, 16> snares{0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1};
    for (int s = 0;; ++s)
    {
        const auto at = static_cast<size_t>(static_cast<double>(s) * step);
        if (at >= total)
            break;
        const auto k = static_cast<size_t>(s % 16);
        const int bar = (s / 16) % 4;
        if (kicks[k] != 0)
        {
            addDecay(l, kick, at, 0.9f);
            addDecay(r, kick, at, 0.9f);
        }
        if (snares[k] != 0)
        {
            const float g = (k == 15) ? 0.35f : 0.8f; // ghost note
            addDecay(l, snare, at, g);
            addDecay(r, snare, at, g * 0.95f);
        }
        const bool offbeat = (s % 4) == 2;
        const auto& hat = offbeat ? hatOpen : hatClosed;
        addDecay(l, hat, at, (s % 2 == 0) ? 0.8f : 0.5f);
        addDecay(r, hat, at, (s % 2 == 0) ? 1.0f : 0.65f);
        if (k == 0 || k == 6 || k == 10)
        {
            const auto& b = bass[static_cast<size_t>(bar)];
            addDecay(l, b, at, 0.45f);
            addDecay(r, b, at, 0.45f);
        }
    }
    return {std::move(l), std::move(r)};
}

// Loads a WAV/AIFF/FLAC file as a stereo signal (mono is duplicated), resampled to kSampleRate (linear), and
// trimmed to kClipSeconds.
bool loadSource(const juce::File& file, Stereo& out)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(fm.createReaderFor(file));
    if (reader == nullptr || reader->lengthInSamples < 1000)
        return false;

    const auto srcLen = static_cast<int>(
        std::min<juce::int64>(reader->lengthInSamples, static_cast<juce::int64>(reader->sampleRate * 60.0)));
    juce::AudioBuffer<float> buf(2, srcLen);
    reader->read(&buf, 0, srcLen, 0, true, true);
    if (reader->numChannels < 2)
        buf.copyFrom(1, 0, buf, 0, 0, srcLen);

    const double ratio = reader->sampleRate / kSampleRate;
    const auto dstLen = std::min(static_cast<size_t>(static_cast<double>(srcLen) / ratio),
                                 static_cast<size_t>(kClipSeconds * kSampleRate));
    for (int c = 0; c < 2; ++c)
    {
        auto& dst = out[static_cast<size_t>(c)];
        dst.assign(dstLen, 0.0f);
        const auto* in = buf.getReadPointer(c);
        for (size_t i = 0; i < dstLen; ++i)
        {
            const double pos = static_cast<double>(i) * ratio;
            const auto i0 = static_cast<int>(pos);
            const auto i1 = std::min(i0 + 1, srcLen - 1);
            const auto frac = static_cast<float>(pos - i0);
            dst[i] = in[i0] + (in[i1] - in[i0]) * frac;
        }
    }
    return true;
}

// Renders `in` through the real processor (offline, in host-sized chunks) and removes the reported latency.
Stereo renderThroughProcessor(const juce::String& preset, const Stereo& in)
{
    PentagritAudioProcessor processor;
    REQUIRE(loadFactory(processor, preset));
    processor.setPlayConfigDetails(2, 2, kSampleRate, kBlockSize);
    processor.setRateAndBufferSizeDetails(kSampleRate, kBlockSize);
    processor.prepareToPlay(kSampleRate, kBlockSize);
    processor.setNonRealtime(true);

    const auto latency = static_cast<size_t>(std::max(0, processor.getLatencySamples()));
    const size_t n = in[0].size();
    const size_t total = n + latency; // flush the delay line with silence
    Stereo out{test::Signal(total), test::Signal(total)};

    juce::AudioBuffer<float> block(2, kBlockSize);
    juce::MidiBuffer midi;
    for (size_t pos = 0; pos < total; pos += kBlockSize)
    {
        const auto len = static_cast<int>(std::min<size_t>(kBlockSize, total - pos));
        block.clear();
        block.setSize(2, len, false, false, true);
        for (int c = 0; c < 2; ++c)
        {
            auto* w = block.getWritePointer(c);
            for (int i = 0; i < len; ++i)
            {
                const auto idx = pos + static_cast<size_t>(i);
                w[i] = idx < n ? in[static_cast<size_t>(c)][idx] : 0.0f;
            }
        }
        processor.processBlock(block, midi);
        for (int c = 0; c < 2; ++c)
            std::copy_n(block.getReadPointer(c), len,
                        out[static_cast<size_t>(c)].begin() + static_cast<std::ptrdiff_t>(pos));
    }
    processor.releaseResources();

    // Align with the dry signal.
    for (auto& ch : out)
    {
        ch.erase(ch.begin(), ch.begin() + static_cast<std::ptrdiff_t>(latency));
        ch.resize(n);
    }
    return out;
}

double stereoRms(const Stereo& s)
{
    const double l = test::rms(s[0]);
    const double r = test::rms(s[1]);
    return std::sqrt(0.5 * (l * l + r * r));
}

float stereoPeak(const Stereo& s)
{
    float p = 0.0f;
    for (const auto& ch : s)
        for (float v : ch)
            p = std::max(p, std::abs(v));
    return p;
}

void scale(Stereo& s, double g)
{
    for (auto& ch : s)
        for (auto& v : ch)
            v = static_cast<float>(v * g);
}

// Short fade-out so the clips do not end with a click.
void fadeOut(Stereo& s, double seconds)
{
    const auto len = static_cast<size_t>(seconds * kSampleRate);
    for (auto& ch : s)
        for (size_t i = 0; i < len && i < ch.size(); ++i)
            ch[ch.size() - 1 - i] *= static_cast<float>(i) / static_cast<float>(len);
}

juce::File writeAudio(const Stereo& s, const juce::File& dir, const juce::String& baseName)
{
    juce::OggVorbisAudioFormat ogg;
    juce::AudioFormat* format = &ogg;
    juce::FlacAudioFormat flac;
    juce::String ext = ".ogg";
    if (ogg.getQualityOptions().isEmpty()) // Ogg support compiled out
    {
        format = &flac;
        ext = ".flac";
    }
    const auto file = dir.getChildFile(baseName + ext);
    file.deleteFile();
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream>(file);
    REQUIRE(static_cast<juce::FileOutputStream&>(*stream).openedOk());

    const int quality = (format == &ogg) ? 3 : 0; // Vorbis ~110 kbit/s keeps the clips small
    auto writer = format->createWriterFor(stream, juce::AudioFormatWriterOptions{}
                                                      .withSampleRate(kSampleRate)
                                                      .withNumChannels(2)
                                                      .withBitsPerSample(16)
                                                      .withQualityOptionIndex(quality));
    REQUIRE(writer != nullptr);

    const auto n = static_cast<int>(s[0].size());
    juce::AudioBuffer<float> buf(2, n);
    for (int c = 0; c < 2; ++c)
        std::copy(s[static_cast<size_t>(c)].begin(), s[static_cast<size_t>(c)].end(), buf.getWritePointer(c));
    REQUIRE(writer->writeFromAudioSampleBuffer(buf, 0, n));
    writer.reset();
    return file;
}

void renderExamples(const juce::File& dir, const juce::String& sourceName, const Stereo& source,
                    const juce::StringArray& presets)
{
    for (const auto& preset : presets)
    {
        INFO(sourceName << " / " << preset);
        auto dry = source;
        auto wet = renderThroughProcessor(preset, source);

        // Match loudness: the processed clip gets the RMS of the dry one, then both share a common
        // gain so neither clips.
        const double wetRms = stereoRms(wet);
        REQUIRE(wetRms > 1.0e-4);
        scale(wet, stereoRms(dry) / wetRms);
        const float peak = std::max(stereoPeak(dry), stereoPeak(wet));
        if (peak > 0.95f)
        {
            scale(dry, 0.95 / peak);
            scale(wet, 0.95 / peak);
        }
        fadeOut(dry, 0.05);
        fadeOut(wet, 0.05);

        const auto slug = preset.toLowerCase().replaceCharacters(" ", "-");
        writeAudio(dry, dir, sourceName + "-" + slug + "-dry");
        writeAudio(wet, dir, sourceName + "-" + slug + "-processed");
    }
}

const juce::StringArray kExamplePresets{"Punchy Kick", "Snare Snap", "Bass Grit", "Glue Warmth",
                                        "Wavefold Shimmer"};
} // namespace

TEST_CASE("Docs: editor screenshots", "[.docs][screenshots]")
{
    const auto dir = docsDir("images");
    if (dir == juce::File())
        SKIP("PENTAGRIT_DOCS_DIR is not set");

    for (auto style : {ui::Style::studioDark, ui::Style::paperLight})
    {
        renderEditor(dir, style, 3, "Punchy Kick");
        renderEditor(dir, style, 5, "Glue Warmth");
    }
    CHECK(dir.getNumberOfChildFiles(juce::File::findFiles, "*.png") >= 8);
}

TEST_CASE("Docs: dry vs preset audio examples", "[.docs][audio]")
{
    const auto dir = docsDir("audio");
    if (dir == juce::File())
        SKIP("PENTAGRIT_DOCS_DIR is not set");

    renderExamples(dir, "drumloop", makeDrumLoop(), kExamplePresets);

    // Optional extra material, for example a CC0 loop added by the maintainer.
    const auto src = juce::SystemStats::getEnvironmentVariable("PENTAGRIT_DOCS_SRC", {});
    if (src.isNotEmpty())
    {
        for (const auto& f : juce::File(src).findChildFiles(juce::File::findFiles, false, "*.wav"))
        {
            Stereo s;
            if (!loadSource(f, s))
            {
                WARN("Skipping unreadable source " << f.getFullPathName());
                continue;
            }
            renderExamples(dir, f.getFileNameWithoutExtension().toLowerCase().replaceCharacters(" _", "--"),
                           s, kExamplePresets);
        }
    }
    CHECK(dir.getNumberOfChildFiles(juce::File::findFiles, "drumloop-*") >= 2 * kExamplePresets.size());
}
