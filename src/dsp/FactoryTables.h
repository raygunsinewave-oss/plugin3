// Procedurally generated factory wavetables. Generated (not sampled) so they are
// 100% original, tiny in the repo, and easy to extend.
#pragma once

#include "Wavetable.h"

#include <array>
#include <memory>
#include <string_view>

namespace mbx::dsp
{
/** Order here defines the "Table" parameter's choice list — append only, never reorder,
    or saved projects will recall the wrong table. */
inline constexpr std::array<std::string_view, 6> kFactoryTableNames {
    "Basic Shapes", "Harmonic Sweep", "PWM Sweep", "Vowel Formants", "Fold Drive", "Sync Sweep"
};

class WavetableBank
{
public:
    static constexpr int kFramesPerTable = 64;

    WavetableBank() { buildFactoryTables(); }

    int size() const noexcept { return static_cast<int> (tables.size()); }
    const Wavetable& get (int index) const noexcept
    {
        return tables[(size_t) std::clamp (index, 0, size() - 1)];
    }

private:
    using Frame = std::vector<float>;
    static constexpr int N = Wavetable::kSize;

    template <typename Fn>
    static std::vector<Frame> makeFrames (Fn&& perFrame)
    {
        std::vector<Frame> frames ((size_t) kFramesPerTable, Frame ((size_t) N));
        for (int f = 0; f < kFramesPerTable; ++f)
        {
            const double t = static_cast<double> (f) / (kFramesPerTable - 1);
            for (int i = 0; i < N; ++i)
                frames[(size_t) f][(size_t) i] = static_cast<float> (perFrame (t, static_cast<double> (i) / N));
        }
        return frames;
    }

    void buildFactoryTables()
    {
        constexpr double tau = 6.283185307179586;
        tables.resize (kFactoryTableNames.size());

        // 0: sine -> triangle -> saw -> square morph
        tables[0].build (makeFrames ([&] (double t, double p) {
            const double sine = std::sin (tau * p);
            const double tri  = 1.0 - 4.0 * std::abs (p - 0.5);
            const double saw  = 2.0 * p - 1.0;
            const double sq   = p < 0.5 ? 1.0 : -1.0;
            const double x    = t * 3.0;
            if (x < 1.0) return sine + (tri - sine) * x;
            if (x < 2.0) return tri + (saw - tri) * (x - 1.0);
            return saw + (sq - saw) * (x - 2.0);
        }), std::string (kFactoryTableNames[0]));

        // 1: harmonic sweep — 1 to 96 harmonics with 1/h rolloff
        tables[1].build (makeFrames ([&] (double t, double p) {
            const int h = 1 + static_cast<int> (t * 95.0);
            double s = 0.0;
            for (int k = 1; k <= h; ++k)
                s += std::sin (tau * k * p) / k;
            return s;
        }), std::string (kFactoryTableNames[1]));

        // 2: pulse width sweep 50% -> 3%
        tables[2].build (makeFrames ([&] (double t, double p) {
            const double width = 0.5 - t * 0.47;
            return p < width ? 1.0 : -1.0;
        }), std::string (kFactoryTableNames[2]));

        // 3: vowel formants a -> e -> i -> o -> u over a 110 Hz harmonic grid
        tables[3].build (makeFrames ([&] (double t, double p) {
            static constexpr double f1[] = { 800, 400, 300, 450, 325 };
            static constexpr double f2[] = { 1150, 1700, 2300, 800, 700 };
            const double x  = t * 4.0;
            const int v     = std::min (3, static_cast<int> (x));
            const double fr = x - v;
            const double F1 = f1[v] + (f1[v + 1] - f1[v]) * fr;
            const double F2 = f2[v] + (f2[v + 1] - f2[v]) * fr;
            double s = 0.0;
            for (int k = 1; k <= 40; ++k)
            {
                const double hz = 110.0 * k;
                const double a  = std::exp (-std::pow ((hz - F1) / 130.0, 2.0))
                                + 0.6 * std::exp (-std::pow ((hz - F2) / 180.0, 2.0)) + 0.02 / k;
                s += a * std::sin (tau * k * p);
            }
            return s;
        }), std::string (kFactoryTableNames[3]));

        // 4: sine through a wavefolder with rising drive
        tables[4].build (makeFrames ([&] (double t, double p) {
            const double drive = 1.0 + t * 7.0;
            return std::sin (drive * std::sin (tau * p) * 1.5707963267948966);
        }), std::string (kFactoryTableNames[4]));

        // 5: hard-sync sweep — slave saw at 1x -> 8x the master
        tables[5].build (makeFrames ([&] (double t, double p) {
            const double ratio = 1.0 + t * 7.0;
            const double slave = std::fmod (p * ratio, 1.0);
            return (2.0 * slave - 1.0) * (1.0 - 0.3 * p); // slight window softens the reset
        }), std::string (kFactoryTableNames[5]));
    }

    std::vector<Wavetable> tables;
};

} // namespace mbx::dsp
