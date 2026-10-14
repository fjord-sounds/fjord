#pragma once
#include <fjord_base/core/Dsp.h>
#include <array>
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace fjord::dsp
{
    using namespace fjord::base::dsp;   // softClip, Random, Smoother, Svf, panGains, DelayLine, Biquad ...

    constexpr int kMaxVoices  = 16;
    constexpr int kMaxUnison  = 8;
    constexpr int kNumOsc     = 8;
    constexpr int kNumBaseOsc = 4;   // oscillators 5-8 were added later (parameters and mod targets appended)
    constexpr int kControlBlock = 16;   // samples per control update (pitch, glide)

    // Sine table with linear interpolation.
    struct SineTable
    {
        static constexpr int size = 2048;
        std::array<float, size + 1> table {};

        SineTable()
        {
            for (int i = 0; i <= size; ++i)
                table[(size_t) i] = std::sin (kTwoPi * (float) i / (float) size);
        }

        float lookup (float phase01) const
        {
            // phase01 may round up to exactly 1.0 (or arrive slightly off): keep the index inside the table
            const float pos = std::clamp (phase01 * (float) size, 0.0f, (float) size - 1.0e-3f);
            const int i = (int) pos;
            const float frac = pos - (float) i;
            return table[(size_t) i] + frac * (table[(size_t) i + 1] - table[(size_t) i]);
        }

        static const SineTable& get()
        {
            static const SineTable instance;
            return instance;
        }
    };
}
