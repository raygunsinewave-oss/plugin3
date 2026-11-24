// Band-limited, mip-mapped wavetables.
//
// Each table holds N frames of kSize samples. For every frame we store kNumMips
// band-limited copies: mip m keeps harmonics 1 .. (kSize/2 >> m). At playback the
// oscillator picks the mip whose top harmonic stays below Nyquist, so playback is
// alias-free at any pitch (oversampling then adds headroom for modulation).
#pragma once

#include "DspCommon.h"
#include "FFT.h"

#include <string>
#include <vector>

namespace mbx::dsp
{
class Wavetable
{
public:
    static constexpr int kSize    = 2048;
    static constexpr int kNumMips = 11; // 1024, 512, ..., 1 harmonics

    Wavetable() = default;

    /** Builds the mip-mapped table from raw single-cycle frames (any length; resampled
        to kSize). Each frame is DC-removed and peak-normalised. NOT real-time safe. */
    void build (const std::vector<std::vector<float>>& frames, std::string tableName)
    {
        name      = std::move (tableName);
        numFrames = std::max (1, static_cast<int> (frames.size()));
        data.assign (static_cast<size_t> (numFrames) * kNumMips * (kSize + 1), 0.0f);

        std::vector<std::complex<double>> spectrum (kSize), work (kSize);

        for (int f = 0; f < static_cast<int> (frames.size()); ++f)
        {
            const auto& src = frames[(size_t) f];
            for (int i = 0; i < kSize; ++i)
            {
                // linear resample to kSize
                const double pos = static_cast<double> (i) * static_cast<double> (src.size()) / kSize;
                const size_t i0  = static_cast<size_t> (pos) % src.size();
                const size_t i1  = (i0 + 1) % src.size();
                const double fr  = pos - std::floor (pos);
                spectrum[(size_t) i] = { src[i0] + fr * (src[i1] - src[i0]), 0.0 };
            }
            fft (spectrum, false);
            spectrum[0] = 0.0; // remove DC

            float frameGain = 1.0f;
            for (int m = 0; m < kNumMips; ++m)
            {
                const int maxHarmonic = (kSize / 2) >> m;
                for (int k = 0; k < kSize; ++k)
                {
                    const int harmonic = k <= kSize / 2 ? k : kSize - k;
                    work[(size_t) k]   = harmonic <= maxHarmonic && harmonic != kSize / 2 ? spectrum[(size_t) k] : 0.0;
                }
                fft (work, true);

                float* dst = mipData (f, m);
                float peak = 0.0f;
                for (int i = 0; i < kSize; ++i)
                {
                    dst[i] = static_cast<float> (work[(size_t) i].real() / kSize);
                    peak   = std::max (peak, std::abs (dst[i]));
                }
                if (m == 0) // normalise every mip with mip-0's gain so levels match across pitch
                    frameGain = peak > 1.0e-9f ? 1.0f / peak : 0.0f;
                for (int i = 0; i < kSize; ++i)
                    dst[i] *= frameGain;
                dst[kSize] = dst[0]; // guard point for interpolation
            }
        }
    }

    /** Mip index for a given phase increment (cycles per sample). */
    static int mipForIncrement (float inc) noexcept
    {
        const float maxHarmonic = 0.5f / std::max (inc, 1.0e-7f);
        int m = 0;
        while (m < kNumMips - 1 && static_cast<float> ((kSize / 2) >> m) > maxHarmonic)
            ++m;
        return m;
    }

    /** Bilinear read: phase in [0,1), position in [0,1] scans across frames. */
    float read (float phase, float position, int mip) const noexcept
    {
        const float fp = std::clamp (position, 0.0f, 1.0f) * static_cast<float> (numFrames - 1);
        const int f0   = static_cast<int> (fp);
        const int f1   = std::min (f0 + 1, numFrames - 1);
        const float ff = fp - static_cast<float> (f0);

        const float idx = phase * static_cast<float> (kSize);
        const int i0    = std::min (static_cast<int> (idx), kSize - 1);
        const float fi  = idx - static_cast<float> (i0);

        const float* a = mipData (f0, mip);
        const float* b = mipData (f1, mip);
        const float va = a[i0] + fi * (a[i0 + 1] - a[i0]);
        const float vb = b[i0] + fi * (b[i0 + 1] - b[i0]);
        return va + ff * (vb - va);
    }

    /** Pre-resolved frame pair for a fixed position (hot loop helper). */
    struct FramePair { const float* a; const float* b; float frac; };
    FramePair frames (float position, int mip) const noexcept
    {
        const float fp = std::clamp (position, 0.0f, 1.0f) * static_cast<float> (numFrames - 1);
        const int f0   = static_cast<int> (fp);
        return { mipData (f0, mip), mipData (std::min (f0 + 1, numFrames - 1), mip), fp - static_cast<float> (f0) };
    }
    static float readPair (const FramePair& fr, float phase) noexcept
    {
        const float idx = phase * static_cast<float> (kSize);
        const int i0    = std::min (static_cast<int> (idx), kSize - 1);
        const float fi  = idx - static_cast<float> (i0);
        const float va  = fr.a[i0] + fi * (fr.a[i0 + 1] - fr.a[i0]);
        const float vb  = fr.b[i0] + fi * (fr.b[i0 + 1] - fr.b[i0]);
        return va + fr.frac * (vb - va);
    }

    int getNumFrames() const noexcept { return numFrames; }
    const std::string& getName() const noexcept { return name; }
    bool isEmpty() const noexcept { return data.empty(); }

    const float* mipData (int frame, int mip) const noexcept
    {
        return data.data() + ((size_t) frame * kNumMips + (size_t) mip) * (kSize + 1);
    }

private:
    float* mipData (int frame, int mip) noexcept
    {
        return data.data() + ((size_t) frame * kNumMips + (size_t) mip) * (kSize + 1);
    }

    std::vector<float> data;
    int numFrames = 0;
    std::string name;
};

} // namespace mbx::dsp
