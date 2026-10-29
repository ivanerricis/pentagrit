// SPDX-License-Identifier: AGPL-3.0-or-later
// Counts heap allocations made on the audio (test) thread between ScopedCounter construction and destruction.
// Replaces the global operator new for the whole test executable.
#include "PluginProcessor.h"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <new>
#include <random>

namespace
{
thread_local bool g_counting = false;
thread_local std::size_t g_allocations = 0;

struct ScopedCounter
{
    ScopedCounter()
    {
        g_allocations = 0;
        g_counting = true;
    }
    ~ScopedCounter() { g_counting = false; }
    static std::size_t count() { return g_allocations; }
};

void* countedAlloc(std::size_t size)
{
    if (g_counting)
        ++g_allocations;
    if (void* p = std::malloc(size == 0 ? 1 : size))
        return p;
    throw std::bad_alloc();
}

void* countedAlignedAlloc(std::size_t size, std::size_t align)
{
    if (g_counting)
        ++g_allocations;
#if defined(_WIN32)
    if (void* p = _aligned_malloc(size == 0 ? 1 : size, align))
        return p;
#else
    void* p = nullptr;
    if (posix_memalign(&p, std::max(align, sizeof(void*)), size == 0 ? 1 : size) == 0)
        return p;
#endif
    throw std::bad_alloc();
}

void countedAlignedFree(void* p)
{
#if defined(_WIN32)
    _aligned_free(p);
#else
    std::free(p);
#endif
}
} // namespace

void* operator new(std::size_t size)
{
    return countedAlloc(size);
}
void* operator new[](std::size_t size)
{
    return countedAlloc(size);
}
void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
    try
    {
        return countedAlloc(size);
    }
    catch (...)
    {
        return nullptr;
    }
}
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept
{
    try
    {
        return countedAlloc(size);
    }
    catch (...)
    {
        return nullptr;
    }
}
void* operator new(std::size_t size, std::align_val_t align)
{
    return countedAlignedAlloc(size, static_cast<std::size_t>(align));
}
void* operator new[](std::size_t size, std::align_val_t align)
{
    return countedAlignedAlloc(size, static_cast<std::size_t>(align));
}
void operator delete(void* p) noexcept
{
    std::free(p);
}
void operator delete[](void* p) noexcept
{
    std::free(p);
}
void operator delete(void* p, std::size_t) noexcept
{
    std::free(p);
}
void operator delete[](void* p, std::size_t) noexcept
{
    std::free(p);
}
void operator delete(void* p, const std::nothrow_t&) noexcept
{
    std::free(p);
}
void operator delete[](void* p, const std::nothrow_t&) noexcept
{
    std::free(p);
}
void operator delete(void* p, std::align_val_t) noexcept
{
    countedAlignedFree(p);
}
void operator delete[](void* p, std::align_val_t) noexcept
{
    countedAlignedFree(p);
}
void operator delete(void* p, std::size_t, std::align_val_t) noexcept
{
    countedAlignedFree(p);
}
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept
{
    countedAlignedFree(p);
}

using namespace pentagrit;

namespace
{
void setParam(PentagritAudioProcessor& p, const juce::String& id, float normalised)
{
    p.getAPVTS().getParameter(id)->setValueNotifyingHost(normalised);
}

void randomiseParams(PentagritAudioProcessor& p, std::mt19937& gen, bool includeNumBands)
{
    std::uniform_real_distribution<float> u(0.0f, 1.0f);
    for (auto* param : p.getParameters())
    {
        auto* withID = dynamic_cast<juce::HostedAudioProcessorParameter*>(param);
        if (withID == nullptr)
            continue;
        const auto id = withID->getParameterID();
        if (id == pid::oversampling || id == pid::osPhase || id == pid::osRender || id == pid::bypass)
            continue;
        if (!includeNumBands && id == pid::numBands)
            continue;
        param->setValueNotifyingHost(u(gen));
    }
}
} // namespace

TEST_CASE("The allocation counter sees heap allocations", "[allocation]")
{
    std::size_t seen = 0;
    {
        ScopedCounter counter;
        // Go through the sized operator directly and publish the pointer, so the compiler cannot elide the
        // allocation (clang on macOS removes a bare `new int(3)` whose result is only deleted).
        static void* volatile sink;
        void* p = ::operator new(sizeof(int));
        sink = p;
        seen = ScopedCounter::count();
        ::operator delete(sink);
    }
    CHECK(seen >= 1);
}

TEST_CASE("processBlock does not allocate at any oversampling factor", "[allocation]")
{
    for (int osChoice = 0; osChoice < 4; ++osChoice)
        for (int phase = 0; phase < 2; ++phase)
        {
            DYNAMIC_SECTION("oversampling choice " << osChoice << " phase " << phase)
            {
                PentagritAudioProcessor proc;
                setParam(proc, pid::oversampling, static_cast<float>(osChoice) / 3.0f);
                setParam(proc, pid::osPhase, static_cast<float>(phase));
                proc.setPlayConfigDetails(2, 2, 48000.0, 512);
                proc.prepareToPlay(48000.0, 512);

                juce::AudioBuffer<float> buffer(2, 512);
                juce::MidiBuffer midi;
                std::mt19937 gen(42);
                std::uniform_real_distribution<float> noise(-0.5f, 0.5f);
                auto fill = [&]
                {
                    for (int c = 0; c < 2; ++c)
                        for (int i = 0; i < 512; ++i)
                            buffer.setSample(c, i, noise(gen));
                };

                for (int i = 0; i < 20; ++i) // warm-up
                {
                    randomiseParams(proc, gen, true);
                    fill();
                    proc.processBlock(buffer, midi);
                }

                std::size_t total = 0;
                for (int i = 0; i < 100; ++i)
                {
                    randomiseParams(proc, gen, false); // automation, outside the counted region
                    fill();
                    {
                        ScopedCounter counter;
                        proc.processBlock(buffer, midi);
                        total += ScopedCounter::count();
                    }
                }
                CHECK(total == 0);
            }
        }
}

TEST_CASE("Engine reconfiguration (factor, phase, band count) does not allocate", "[allocation]")
{
    MultibandEngine engine;
    engine.prepare(48000.0, 512, 2);
    EngineSettings s;
    juce::AudioBuffer<float> buffer(2, 512);
    std::mt19937 gen(7);
    std::uniform_real_distribution<float> noise(-0.5f, 0.5f);

    std::size_t total = 0;
    for (int i = 0; i < 64; ++i)
    {
        for (int c = 0; c < 2; ++c)
            for (int n = 0; n < 512; ++n)
                buffer.setSample(c, n, noise(gen));
        s.os = static_cast<OSFactor>(i % 4);
        s.osPhase = (i / 4) % 2 == 0 ? OSPhase::linear : OSPhase::minimum;
        s.numBands = 2 + i % 4;
        s.bypass = (i % 7) == 0;
        engine.setNonRealtime((i % 5) == 0);
        {
            ScopedCounter counter;
            engine.setSettings(s);
            engine.process(buffer);
            total += ScopedCounter::count();
        }
    }
    CHECK(total == 0);
}
