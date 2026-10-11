#pragma once
#include "DspCommon.h"
#include "Wavetable.h"

namespace fjord::dsp
{
    enum class Wave { Sine, Triangle, Saw, Square, Table };

    // Band-limited corrections (PolyBLEP / PolyBLAMP) against aliasing.
    inline float polyBlep (float t, float dt)
    {
        if (t < dt)        { t /= dt;               return t + t - t * t - 1.0f; }
        if (t > 1.0f - dt) { t = (t - 1.0f) / dt;   return t * t + t + t + 1.0f; }
        return 0.0f;
    }

    inline float polyBlamp (float t, float dt)
    {
        if (t < dt)        { t = t / dt - 1.0f;          return -(1.0f / 3.0f) * t * t * t; }
        if (t > 1.0f - dt) { t = (t - 1.0f) / dt + 1.0f; return  (1.0f / 3.0f) * t * t * t; }
        return 0.0f;
    }

    inline float renderVA (Wave wave, float t, float dt, float pw)
    {
        switch (wave)
        {
            case Wave::Sine:
                return SineTable::get().lookup (t);

            case Wave::Triangle:
            {
                float y = 1.0f - 4.0f * std::abs (t - 0.5f);
                float t2 = t + 0.5f;
                if (t2 >= 1.0f) t2 -= 1.0f;
                y += 4.0f * dt * (polyBlamp (t, dt) - polyBlamp (t2, dt));
                return y;
            }

            case Wave::Saw:
                return 2.0f * t - 1.0f - polyBlep (t, dt);

            case Wave::Square:
            {
                float y = t < pw ? 1.0f : -1.0f;
                y += polyBlep (t, dt);
                float t2 = t - pw + 1.0f;
                if (t2 >= 1.0f) t2 -= 1.0f;
                y -= polyBlep (t2, dt);
                return y - (2.0f * pw - 1.0f);   // remove DC caused by asymmetric pulse width
            }

            case Wave::Table:
                break;
        }
        return 0.0f;
    }

    struct OscSettings
    {
        Wave wave = Wave::Saw;
        float pitchNote = 60.0f;   // including octave, semi, fine, bend
        float pw = 0.5f;
        int unison = 1;
        float detune = 0.25f;      // 0..1
        float spread = 0.5f;       // stereo width 0..1
        float blend = 0.75f;       // level of the outer unison voices
        float pan = 0.0f;
        int table = 0;
        float wtPos = 0.0f;        // 0..1 morph through the frames
    };

    // Analog-modelled oscillator with up to 8 unison voices (supersaw capable).
    class UnisonOscillator
    {
    public:
        void reset (bool randomPhase, Random& rng)
        {
            for (auto& p : phase)
                p = randomPhase ? rng.next01() : 0.0f;
        }

        // Called per control block: frequencies, pan, levels of the voices.
        void update (const OscSettings& s, float sampleRate)
        {
            settings = s;
            const int n = std::clamp (s.unison, 1, kMaxUnison);
            const float baseHz = noteToHz (s.pitchNote);
            const float maxCents = 52.0f * std::pow (s.detune, 1.5f);

            // Small fixed asymmetries so unison beating does not repeat statically.
            static constexpr float jitter[kMaxUnison] = { 0.0f, 0.031f, -0.047f, 0.019f, -0.023f, 0.043f, -0.011f, 0.037f };

            float gainSum = 0.0f;
            for (int i = 0; i < n; ++i)
            {
                const float pos = n == 1 ? 0.0f : -1.0f + 2.0f * (float) i / (float) (n - 1);
                const float cents = (pos + (n > 1 ? jitter[i] : 0.0f)) * maxCents;
                inc[(size_t) i] = std::min (0.45f, baseHz * std::exp2 (cents / 1200.0f) / sampleRate);

                const float g = 1.0f - (1.0f - s.blend) * std::abs (pos);
                gain[(size_t) i] = g;
                gainSum += g * g;

                const float side = (i % 2 == 0) ? -1.0f : 1.0f;
                const float pan = std::clamp (s.pan + s.spread * std::abs (pos) * side, -1.0f, 1.0f);
                const float angle = (pan + 1.0f) * kPi * 0.25f;
                panL[(size_t) i] = std::cos (angle) * 1.41421356f;
                panR[(size_t) i] = std::sin (angle) * 1.41421356f;
            }

            const float norm = 1.0f / std::sqrt (std::max (gainSum, 1.0e-6f));
            for (int i = 0; i < n; ++i)
                gain[(size_t) i] *= norm;

            voices = n;

            if (s.wave == Wave::Table)
            {
                const auto& bank = WavetableBank::get();
                const int table = std::clamp (s.table, 0, bank.numTables() - 1);
                const float maxInc = baseHz * std::exp2 (maxCents / 1200.0f) / sampleRate;
                wtLevel = &bank.level (table, WavetableBank::levelForIncrement (maxInc));
                const float fp = std::clamp (s.wtPos, 0.0f, 1.0f) * (WavetableBank::kFrames - 1);
                const int fa = std::min ((int) fp, WavetableBank::kFrames - 2);
                frameA = wtLevel->frame (fa);
                frameB = wtLevel->frame (fa + 1);
                // smooth the frame crossfade so morph modulation does not click
                frameMixTarget = fp - (float) fa;
                if (frameIndex != fa) { frameIndex = fa; frameMix = frameMixTarget; }
            }
        }

        // Adds the signal (with level) to L/R.
        void process (float* L, float* R, int numSamples, float level)
        {
            if (level <= 0.0f)
            {
                advance (numSamples);
                return;
            }

            if (settings.wave == Wave::Table && wtLevel != nullptr)
                return processTable (L, R, numSamples, level);

            for (int v = 0; v < voices; ++v)
            {
                float p = phase[(size_t) v];
                const float dt = inc[(size_t) v];
                const float gl = gain[(size_t) v] * panL[(size_t) v] * level;
                const float gr = gain[(size_t) v] * panR[(size_t) v] * level;

                for (int i = 0; i < numSamples; ++i)
                {
                    const float y = renderVA (settings.wave, p, dt, settings.pw);
                    L[i] += y * gl;
                    R[i] += y * gr;
                    p += dt;
                    if (p >= 1.0f) p -= 1.0f;
                }
                phase[(size_t) v] = p;
            }
        }

    private:
        void processTable (float* L, float* R, int numSamples, float level)
        {
            const float size = (float) wtLevel->size;
            const int mask = wtLevel->mask;
            const float mixStart = frameMix;
            const float mixStep = (frameMixTarget - frameMix) / (float) numSamples;

            for (int v = 0; v < voices; ++v)
            {
                float p = phase[(size_t) v];
                const float dt = inc[(size_t) v];
                const float gl = gain[(size_t) v] * panL[(size_t) v] * level;
                const float gr = gain[(size_t) v] * panR[(size_t) v] * level;
                float mix = mixStart;

                for (int i = 0; i < numSamples; ++i)
                {
                    const float pos = p * size;
                    const int i0 = (int) pos;
                    const int i1 = (i0 + 1) & mask;
                    const float fr = pos - (float) i0;
                    const float a = frameA[i0] + fr * (frameA[i1] - frameA[i0]);
                    const float b = frameB[i0] + fr * (frameB[i1] - frameB[i0]);
                    const float y = a + mix * (b - a);
                    mix += mixStep;
                    L[i] += y * gl;
                    R[i] += y * gr;
                    p += dt;
                    if (p >= 1.0f) p -= 1.0f;
                }
                phase[(size_t) v] = p;
            }
            frameMix = frameMixTarget;
        }

        void advance (int numSamples)
        {
            for (int v = 0; v < voices; ++v)
            {
                float p = phase[(size_t) v] + inc[(size_t) v] * (float) numSamples;
                phase[(size_t) v] = p - std::floor (p);
            }
        }

        OscSettings settings;
        int voices = 1;
        const WavetableBank::Level* wtLevel = nullptr;
        const float* frameA = nullptr;
        const float* frameB = nullptr;
        int frameIndex = -1;
        float frameMix = 0.0f, frameMixTarget = 0.0f;
        std::array<float, kMaxUnison> phase {}, inc {}, gain {}, panL {}, panR {};
    };
}
