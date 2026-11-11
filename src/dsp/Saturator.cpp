// SPDX-License-Identifier: AGPL-3.0-or-later
#include "dsp/Saturator.h"
#include "dsp/DspUtils.h"

#include <array>
#include <cmath>

namespace pentagrit
{
namespace
{
constexpr double kDriveSmoothSec = 0.020;
constexpr double kFadeSec = 0.010;
constexpr double kDcHz = 5.0;
constexpr double kCompSec = 0.020;

const float kTubeOffset = fastTanh(0.25f);
} // namespace

void Saturator::prepare(double procRate, int, int numCh)
{
    const auto n = static_cast<size_t>(std::max(numCh, 1));
    dcX1_.assign(n, 0.0f);
    dcY1_.assign(n, 0.0f);
    uPrev_.assign(n, 0.0f);
    compSnap_ = true;
    setProcessingRate(procRate);
    reset();
}

void Saturator::updateCoefficients() noexcept
{
    driveCoeff_ = timeToCoeff(kDriveSmoothSec, rate_);
    fadeLen_ = std::max(1, static_cast<int>(std::lround(kFadeSec * rate_)));
    fadePos_ = std::min(fadePos_, fadeLen_);
    dcCoeff_ = static_cast<float>(std::exp(-2.0 * 3.14159265358979323846 * kDcHz / rate_));
    compStep_ = static_cast<float>(1.0 / (kCompSec * rate_));
}

void Saturator::setProcessingRate(double procRate) noexcept
{
    rate_ = procRate > 0.0 ? procRate : 44100.0;
    updateCoefficients();
}

void Saturator::reset() noexcept
{
    std::fill(dcX1_.begin(), dcX1_.end(), 0.0f);
    std::fill(dcY1_.begin(), dcY1_.end(), 0.0f);
    std::fill(uPrev_.begin(), uPrev_.end(), 0.0f);
    smoothGain_ = targetGain_;
    compW_ = compTarget_ ? 1.0f : 0.0f;
    fadePos_ = fadeLen_;
    prevType_ = type_;
}

void Saturator::setType(SatType type) noexcept
{
    if (type == type_)
    {
        return;
    }
    prevType_ = type_;
    type_ = type;
    fadePos_ = 0;
}

void Saturator::setDriveDb(float driveDb) noexcept
{
    driveDb_ = driveDb;
    targetGain_ = dbToGain(driveDb);
    if (driveSnap_)
    {
        smoothGain_ = targetGain_;
        driveSnap_ = false;
    }
}

void Saturator::setDriveComp(bool on) noexcept
{
    compTarget_ = on;
    if (compSnap_)
    {
        compW_ = on ? 1.0f : 0.0f;
        compSnap_ = false;
    }
}

float Saturator::shape(SatType type, float u) noexcept
{
    switch (type)
    {
        case SatType::off:
            return u;
        case SatType::tanh:
            return fastTanh(u);
        case SatType::softClip:
        {
            const float v = clampTo(u, -1.0f, 1.0f);
            return 1.5f * (v - v * v * v * (1.0f / 3.0f));
        }
        case SatType::tube:
            return fastTanh(u + 0.25f) - kTubeOffset;
        case SatType::wavefold:
            return fastSinHalfPi(u);
        case SatType::hardClip:
            return clampTo(u, -1.0f, 1.0f);
    }
    return u;
}

namespace
{
constexpr int kChunk = 256;

// ---- First-order antiderivative antialiasing (ADAA1) of hard clip and wavefold ----
//
// y[n] = (F(u[n]) - F(u[n-1])) / (u[n] - u[n-1]) with F' = f. This is the exact integral average of the
// curve over the segment between two consecutive input samples, which removes most of the aliasing of the
// kinks (hard clip) and of the folds (wavefold). Known and accepted trade-off: it acts as a two-tap
// average, so these two curves lose some high end (about -2 dB at 10 kHz at 1x, -0.5 dB at 2x) and are
// delayed by half a sample at the processing rate. It is deliberately not compensated.
//
// The differences F(a) - F(b) are evaluated in closed form instead of subtracting two antiderivative
// values, so there is no catastrophic cancellation for nearby samples (low-level signals):
//  - hard clip: integral of clamp(u, -1, 1) over [lo, hi], summed per region with positive pieces;
//  - wavefold: cos(A) - cos(B) = -2 sin((A+B)/2) sin((A-B)/2) gives
//    y = sin(pi/2 * (a+b)/2) * sinc(pi/4 * (a-b)), which tends to f((a+b)/2) as a -> b.
// When |a-b| is below a small threshold the output is f((a+b)/2) (the limit), which also avoids 0/0.
constexpr float kAdaaClampU = 1.0e5f; // keeps +-inf finite; fastSinHalfPi clamps at the same value
constexpr float kHardClipEps = 1.0e-6f;
constexpr float kWavefoldEps = 1.0e-5f;
constexpr float kPiOver4 = 0.78539816339744831f;

inline float hardClipAdaa(float a, float b) noexcept
{
    const float lo = a < b ? a : b, hi = a < b ? b : a;
    const float d = hi - lo;
    const float ml = clampTo(lo, -1.0f, 1.0f), mh = clampTo(hi, -1.0f, 1.0f);
    const float mid = (mh - ml) * (mh + ml) * 0.5f;
    const float up = (hi > 1.0f ? hi : 1.0f) - (lo > 1.0f ? lo : 1.0f);
    const float dn = (hi < -1.0f ? hi : -1.0f) - (lo < -1.0f ? lo : -1.0f);
    const float y = (mid + up - dn) / d;
    return d < kHardClipEps ? clampTo(0.5f * (a + b), -1.0f, 1.0f) : y;
}

inline float wavefoldAdaa(float a, float b) noexcept
{
    const float d = a - b;
    const float avg = fastSinHalfPi(0.5f * (a + b));
    const float q = std::abs(d) < kWavefoldEps ? 1.0f : fastSinHalfPi(0.5f * d) / (kPiOver4 * d);
    return avg * q;
}

// ADAA over a block; `uPrev` is the (clamped) input sample before u[0].
template <float (*Kernel)(float, float)>
void adaaBlock(const float* __restrict u, float uPrev, float* __restrict s, int n) noexcept
{
    s[0] = Kernel(clampTo(u[0], -kAdaaClampU, kAdaaClampU), clampTo(uPrev, -kAdaaClampU, kAdaaClampU));
    for (int i = 1; i < n; ++i)
    {
        s[i] = Kernel(clampTo(u[i], -kAdaaClampU, kAdaaClampU), clampTo(u[i - 1], -kAdaaClampU, kAdaaClampU));
    }
}

// Vectorizable block version of Saturator::shape (u -> s), same maths except hard clip and wavefold, which
// are antialiased (see above). `uPrev` is the input sample preceding u[0].
void shapeBlock(SatType type, const float* __restrict u, float uPrev, float* __restrict s, int n) noexcept
{
    switch (type)
    {
        case SatType::off:
            std::copy(u, u + n, s);
            break;
        case SatType::tanh:
            for (int i = 0; i < n; ++i)
            {
                s[i] = fastTanh(u[i]);
            }
            break;
        case SatType::softClip:
            for (int i = 0; i < n; ++i)
            {
                const float v = clampTo(u[i], -1.0f, 1.0f);
                s[i] = 1.5f * (v - v * v * v * (1.0f / 3.0f));
            }
            break;
        case SatType::tube:
            for (int i = 0; i < n; ++i)
            {
                s[i] = fastTanh(u[i] + 0.25f) - kTubeOffset;
            }
            break;
        case SatType::wavefold:
            adaaBlock<wavefoldAdaa>(u, uPrev, s, n);
            break;
        case SatType::hardClip:
            adaaBlock<hardClipAdaa>(u, uPrev, s, n);
            break;
    }
}

// s *= comp[i] (the per-sample compensation factor of the chunk); the Off curve is never compensated.
void applyComp(SatType type, const float* __restrict comp, float* __restrict s, int n) noexcept
{
    if (type == SatType::off)
    {
        return;
    }
    for (int i = 0; i < n; ++i)
    {
        s[i] *= comp[i];
    }
}
} // namespace

void Saturator::process(const juce::dsp::AudioBlock<float>& block) noexcept
{
    const bool fading = fadePos_ < fadeLen_;
    if (type_ == SatType::off && !fading)
    {
        // Fully transparent; drop DC blocker state so re-enabling starts clean.
        std::fill(dcX1_.begin(), dcX1_.end(), 0.0f);
        std::fill(dcY1_.begin(), dcY1_.end(), 0.0f);
        std::fill(uPrev_.begin(), uPrev_.end(), 0.0f);
        smoothGain_ = targetGain_;
        compW_ = compTarget_ ? 1.0f : 0.0f;
        return;
    }

    const auto numCh = std::min(block.getNumChannels(), dcX1_.size());
    const auto numSamples = static_cast<int>(block.getNumSamples());
    if (numCh == 0 || numSamples == 0)
    {
        return;
    }

    const float invFade = 1.0f / static_cast<float>(fadeLen_);
    float g = smoothGain_;
    int fp = fadePos_;
    float w = compW_;
    float gain[kChunk], u[kChunk], sb[2][kChunk], comp[kChunk];

    for (int start = 0; start < numSamples; start += kChunk)
    {
        const int len = std::min(kChunk, numSamples - start);

        // Drive smoothing and crossfade position are shared by all channels. Once the smoother has settled on
        // the target the gain is constant and the recursion is skipped.
        const bool constGain = g == targetGain_;
        if (!constGain)
        {
            for (int i = 0; i < len; ++i)
            {
                g = targetGain_ + driveCoeff_ * (g - targetGain_);
                gain[i] = g;
            }
        }
        const float tg = targetGain_;
        const bool chunkFading = fp < fadeLen_;

        // Drive compensation: 1 / drive gain (the same smoothed gain as the pre-gain) blended in with a
        // ~20 ms linear ramp of w, so toggling it is click free. Skipped entirely (bit-exact with the
        // uncompensated path) while it is off and settled.
        const bool compActive = w > 0.0f || compTarget_;
        if (compActive)
        {
            const float wTarget = compTarget_ ? 1.0f : 0.0f;
            for (int i = 0; i < len; ++i)
            {
                if (w < wTarget)
                {
                    w = std::min(w + compStep_, wTarget);
                }
                else if (w > wTarget)
                {
                    w = std::max(w - compStep_, wTarget);
                }
                // 1 + w * (1 / drive - 1): no compensation at w = 0, 1 / drive at w = 1.
                comp[i] = 1.0f + w * (1.0f / std::max(constGain ? tg : gain[i], 1.0e-6f) - 1.0f);
            }
        }

        // Channels are processed in pairs so the two serial DC-blocker recursions overlap.
        for (size_t ch0 = 0; ch0 < numCh; ch0 += 2)
        {
            const size_t np = std::min<size_t>(2, numCh - ch0);
            for (size_t k = 0; k < np; ++k)
            {
                float* d = block.getChannelPointer(ch0 + k) + start;
                float* s = sb[k];
                if (constGain)
                {
                    for (int i = 0; i < len; ++i)
                    {
                        u[i] = d[i] * tg;
                    }
                }
                else
                {
                    for (int i = 0; i < len; ++i)
                    {
                        u[i] = d[i] * gain[i];
                    }
                }

                if (chunkFading)
                {
                    std::array<float, kChunk> sPrev, sCur;
                    shapeBlock(prevType_, u, uPrev_[ch0 + k], sPrev.data(), len);
                    shapeBlock(type_, u, uPrev_[ch0 + k], sCur.data(), len);
                    if (compActive)
                    {
                        applyComp(prevType_, comp, sPrev.data(), len);
                        applyComp(type_, comp, sCur.data(), len);
                    }
                    for (int i = 0; i < len; ++i)
                    {
                        const int p = fp + i;
                        const float b = type_ == SatType::off ? d[i] : sCur[static_cast<size_t>(i)];
                        if (p < fadeLen_)
                        {
                            const float t = static_cast<float>(p + 1) * invFade;
                            const float a = prevType_ == SatType::off ? d[i] : sPrev[static_cast<size_t>(i)];
                            s[i] = a * (1.0f - t) + b * t;
                        }
                        else
                        {
                            s[i] = b;
                        }
                    }
                }
                else if (type_ == SatType::off)
                {
                    std::copy(d, d + len, s);
                }
                else
                {
                    shapeBlock(type_, u, uPrev_[ch0 + k], s, len);
                    if (compActive)
                    {
                        applyComp(type_, comp, s, len);
                    }
                }
                uPrev_[ch0 + k] = u[len - 1];
            }

            // Branch-free DC blocker; denormals are flushed once per chunk (the pole is so slow that the
            // state cannot cross the denormal range within a chunk).
            const float dc = dcCoeff_;
            if (np == 2)
            {
                float* d0 = block.getChannelPointer(ch0) + start;
                float* d1 = block.getChannelPointer(ch0 + 1) + start;
                float xa = dcX1_[ch0], ya = dcY1_[ch0], xb = dcX1_[ch0 + 1], yb = dcY1_[ch0 + 1];
                for (int i = 0; i < len; ++i)
                {
                    const float sa = sb[0][i], sc = sb[1][i];
                    const float y0 = sa - xa + dc * ya;
                    const float y1 = sc - xb + dc * yb;
                    xa = sa;
                    xb = sc;
                    ya = y0;
                    yb = y1;
                    d0[i] = y0;
                    d1[i] = y1;
                }
                dcX1_[ch0] = std::abs(xa) < 1.0e-20f ? 0.0f : xa;
                dcY1_[ch0] = std::abs(ya) < 1.0e-20f ? 0.0f : ya;
                dcX1_[ch0 + 1] = std::abs(xb) < 1.0e-20f ? 0.0f : xb;
                dcY1_[ch0 + 1] = std::abs(yb) < 1.0e-20f ? 0.0f : yb;
            }
            else
            {
                float* d0 = block.getChannelPointer(ch0) + start;
                float xa = dcX1_[ch0], ya = dcY1_[ch0];
                for (int i = 0; i < len; ++i)
                {
                    const float sa = sb[0][i];
                    const float y0 = sa - xa + dc * ya;
                    xa = sa;
                    ya = y0;
                    d0[i] = y0;
                }
                dcX1_[ch0] = std::abs(xa) < 1.0e-20f ? 0.0f : xa;
                dcY1_[ch0] = std::abs(ya) < 1.0e-20f ? 0.0f : ya;
            }
        }
        if (chunkFading)
        {
            fp = std::min(fp + len, fadeLen_);
        }
    }
    smoothGain_ = g;
    compW_ = w;
    fadePos_ = fp;
}
} // namespace pentagrit
