#pragma once
#include "DspCommon.h"
#include <vector>

namespace fjord::dsp
{
    enum class FilterType { Ladder24, Ladder12, SvfLow, SvfBand, SvfHigh, SvfNotch, Comb, Formant };

    // Stereo filter with zero-delay feedback (topology-preserving transform).
    // Ladder: Moog-like, warm, with saturation in the feedback path. SVF: clean, versatile.
    // Comb: comb filter resonance tuned to the cutoff. Formant: the cutoff morphs through the vowels A-E-I-O-U.
    class Filter
    {
    public:
        void prepare (float sr)
        {
            sampleRate = sr;
            const int needed = (int) (sr / 18.0f) + 4;
            int size = 1;
            while (size < needed) size <<= 1;
            for (auto& c : comb) c.buffer.assign ((size_t) size, 0.0f);
            combMask = size - 1;
            reset();
        }

        void reset()
        {
            for (auto& ch : ladder) ch.fill (0.0f);
            for (auto& ch : svf)    ch.fill (0.0f);
            for (auto& ch : formant)
                for (auto& f : ch) f.fill (0.0f);
            for (auto& c : comb) { std::fill (c.buffer.begin(), c.buffer.end(), 0.0f); c.damp = 0.0f; }
        }

        void setType (FilterType t)
        {
            if (t != type)
                reset();
            type = t;
        }

        // cutoffHz/reso/drive per sample (the filter envelope runs at audio rate).
        void process (float& l, float& r, float cutoffHz, float reso, float drive)
        {
            cutoffHz = std::clamp (cutoffHz, 16.0f, sampleRate * 0.45f);

            if (type == FilterType::Comb)
            {
                const float driveGain = 1.0f + drive * 5.0f;
                const float fb = reso * 0.96f;
                const float delay = std::clamp (sampleRate / cutoffHz, 2.0f, (float) combMask - 2.0f);
                const float out = 1.0f - 0.75f * fb;
                l = processComb (comb[0], softClip (l * driveGain), delay, fb) * out / std::sqrt (driveGain);
                r = processComb (comb[1], softClip (r * driveGain), delay, fb) * out / std::sqrt (driveGain);
                combWrite = (combWrite + 1) & combMask;
                return;
            }

            if (type == FilterType::Formant)
            {
                processFormant (l, r, cutoffHz, reso, drive);
                return;
            }

            const float g = std::tan (kPi * cutoffHz / sampleRate);

            if (isLadder())
            {
                const float k = reso * 4.0f;
                const float driveGain = 1.0f + drive * 7.0f;
                const float makeup = (1.0f + 0.5f * k) / (1.0f + 0.25f * (driveGain - 1.0f));
                l = processLadder (ladder[0], l * driveGain, g, k) * makeup;
                r = processLadder (ladder[1], r * driveGain, g, k) * makeup;
            }
            else
            {
                const float k = 2.0f - 1.96f * reso;
                const float driveGain = 1.0f + drive * 7.0f;
                const float makeup = 1.0f / (1.0f + 0.25f * (driveGain - 1.0f));
                l = processSvf (svf[0], softClip (l * driveGain), g, k) * makeup;
                r = processSvf (svf[1], softClip (r * driveGain), g, k) * makeup;
            }
        }

    private:
        bool isLadder() const { return type == FilterType::Ladder24 || type == FilterType::Ladder12; }

        float processLadder (std::array<float, 4>& s, float x, float g, float k)
        {
            const float G = g / (1.0f + g);
            const float inv = 1.0f / (1.0f + g);
            const float G2 = G * G, G4 = G2 * G2;

            // Linear prediction of the output, then a non-linearly saturated input.
            const float S = (G2 * G * s[0] + G2 * s[1] + G * s[2] + s[3]) * inv;
            const float y4est = (G4 * x + S) / (1.0f + k * G4);
            float u = softClip (x - k * y4est);

            float y[4];
            for (int i = 0; i < 4; ++i)
            {
                const float v = (u - s[(size_t) i]) * G;
                y[i] = v + s[(size_t) i];
                s[(size_t) i] = y[i] + v;
                u = y[i];
            }

            return type == FilterType::Ladder12 ? y[1] : y[3];
        }

        struct CombLine { std::vector<float> buffer; float damp = 0.0f; };

        float processComb (CombLine& c, float x, float delay, float fb)
        {
            float rp = (float) combWrite - delay;
            if (rp < 0.0f) rp += (float) (combMask + 1);
            const int i0 = (int) rp;
            const float fr = rp - (float) i0;
            const float d = c.buffer[(size_t) (i0 & combMask)] * (1.0f - fr) + c.buffer[(size_t) ((i0 + 1) & combMask)] * fr;
            c.damp += (d - c.damp) * 0.55f;                    // gentle high damping in the loop
            const float y = x + fb * softClip (c.damp);
            c.buffer[(size_t) combWrite] = y;
            return y;
        }

        void processFormant (float& l, float& r, float cutoffHz, float reso, float drive)
        {
            // Vowel formants (Hz) for A, E, I, O, U
            static constexpr float vowels[5][3] = { { 730, 1090, 2440 }, { 530, 1840, 2480 }, { 270, 2290, 3010 },
                                                    { 570, 840, 2410 },  { 300, 870, 2240 } };
            static constexpr float gains[3] = { 1.0f, 0.7f, 0.35f };

            const float pos = std::clamp (std::log (cutoffHz / 60.0f) / std::log (250.0f), 0.0f, 1.0f) * 4.0f;
            const int a = std::min (3, (int) pos);
            const float fr = pos - (float) a;
            const float k = 1.0f / (3.0f + 17.0f * reso);
            const float driveGain = 1.0f + drive * 5.0f;
            const float inL = softClip (l * driveGain), inR = softClip (r * driveGain);
            float outL = 0.0f, outR = 0.0f;

            for (int f = 0; f < 3; ++f)
            {
                const float fc = std::min (vowels[a][f] + (vowels[a + 1][f] - vowels[a][f]) * fr, sampleRate * 0.45f);
                const float g = std::tan (kPi * fc / sampleRate);
                outL += bandpass (formant[0][(size_t) f], inL, g, k) * gains[f];
                outR += bandpass (formant[1][(size_t) f], inR, g, k) * gains[f];
            }

            const float makeup = 1.6f / std::sqrt (driveGain);
            l = outL * makeup;
            r = outR * makeup;
        }

        static float bandpass (std::array<float, 2>& s, float x, float g, float k)
        {
            const float a1 = 1.0f / (1.0f + g * (g + k));
            const float a2 = g * a1;
            const float v3 = x - s[1];
            const float v1 = a1 * s[0] + a2 * v3;
            const float v2 = s[1] + a2 * s[0] + g * a2 * v3;
            s[0] = 2.0f * v1 - s[0];
            s[1] = 2.0f * v2 - s[1];
            return v1 * k;
        }

        float processSvf (std::array<float, 2>& s, float x, float g, float k)
        {
            const float a1 = 1.0f / (1.0f + g * (g + k));
            const float a2 = g * a1;
            const float a3 = g * a2;
            const float v3 = x - s[1];
            const float v1 = a1 * s[0] + a2 * v3;
            const float v2 = s[1] + a2 * s[0] + a3 * v3;
            s[0] = 2.0f * v1 - s[0];
            s[1] = 2.0f * v2 - s[1];

            switch (type)
            {
                case FilterType::SvfBand:  return v1 * k;   // normalised to unity gain
                case FilterType::SvfHigh:  return x - k * v1 - v2;
                case FilterType::SvfNotch: return x - k * v1;
                case FilterType::Ladder24:
                case FilterType::Ladder12:
                case FilterType::SvfLow:
                case FilterType::Comb:
                case FilterType::Formant:
                default:                   return v2;
            }
        }

        float sampleRate = 44100.0f;
        FilterType type = FilterType::Ladder24;
        std::array<std::array<float, 4>, 2> ladder {};
        std::array<std::array<float, 2>, 2> svf {};
        std::array<std::array<std::array<float, 2>, 3>, 2> formant {};
        std::array<CombLine, 2> comb;
        int combMask = 0, combWrite = 0;
    };
}
