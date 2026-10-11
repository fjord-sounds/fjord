#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace fjord::dsp
{
    constexpr int kMaxVoices  = 16;
    constexpr int kMaxUnison  = 8;
    constexpr int kNumOsc     = 8;
    constexpr int kNumBaseOsc = 4;   // oscillators 5-8 were added later (parameters and mod targets appended)
    constexpr int kControlBlock = 16;   // samples per control update (pitch, glide)

    constexpr float kPi    = 3.14159265358979f;
    constexpr float kTwoPi = 6.28318530717959f;

    inline float noteToHz (float note) { return 440.0f * std::exp2 ((note - 69.0f) / 12.0f); }

    // Soft saturation (Padé approximation of tanh), sounds round and is cheap.
    inline float softClip (float x)
    {
        x = std::clamp (x, -3.0f, 3.0f);
        return x * (27.0f + x * x) / (27.0f + 9.0f * x * x);
    }

    // Small, fast random generator (xorshift32) – separate state per voice.
    struct Random
    {
        uint32_t state = 0x12345678u;

        void seed (uint32_t s) { state = s ? s : 0x9e3779b9u; }

        uint32_t nextInt()
        {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            return state;
        }

        float next01()      { return (float) (nextInt() >> 8) * (1.0f / 16777216.0f); }
        float nextBipolar() { return next01() * 2.0f - 1.0f; }
    };

    // One-pole smoother for parameters (prevents zipper noise).
    struct Smoother
    {
        float value = 0.0f, coef = 0.0f;

        void setTime (float seconds, float sampleRate)
        {
            coef = std::exp (-1.0f / std::max (1.0f, seconds * sampleRate));
        }

        void reset (float v) { value = v; }
        float process (float target) { return value = target + (value - target) * coef; }
    };

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
            const float pos = phase01 * (float) size;
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
