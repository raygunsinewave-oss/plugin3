// Minimal radix-2 complex FFT. Used only off the audio thread (wavetable
// band-limiting, tests, analysers) so clarity beats speed here.
#pragma once

#include <cmath>
#include <complex>
#include <vector>

namespace mbx::dsp
{
/** In-place FFT. data.size() must be a power of two. inverse=true performs the
    unscaled inverse transform (divide by N yourself). */
inline void fft (std::vector<std::complex<double>>& data, bool inverse)
{
    const size_t n = data.size();

    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap (data[i], data[j]);
    }

    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = 2.0 * 3.14159265358979323846 / static_cast<double> (len) * (inverse ? 1.0 : -1.0);
        const std::complex<double> wlen (std::cos (ang), std::sin (ang));

        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> w (1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k)
            {
                const auto u = data[i + k];
                const auto v = data[i + k + len / 2] * w;
                data[i + k]           = u + v;
                data[i + k + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
}
} // namespace mbx::dsp
