#include "Wavetable.h"
#include <juce_dsp/juce_dsp.h>
#include <complex>
#include <functional>

namespace fjord::dsp
{
    namespace
    {
        constexpr int kSize = 2048;
        constexpr int kMaxHarmonics = 1024;
        using Spectrum = std::vector<std::complex<double>>;   // index = harmonic (0 = DC)

        float hash01 (uint32_t n)
        {
            n = (n ^ 61u) ^ (n >> 16);
            n *= 9u;
            n ^= n >> 4;
            n *= 0x27d4eb2du;
            n ^= n >> 15;
            return (float) (n & 0xffffff) / 16777215.0f;
        }

        float sine (double x) { return (float) std::sin (juce::MathConstants<double>::twoPi * x); }

        // A table is described either in the time domain (one cycle) or as a spectrum.
        struct TableDef
        {
            const char* name;
            std::function<float (double x, float t)> wave;          // x: Phase 0..1, t: Frame 0..1
            std::function<double (int k, float t)> amp;             // amplitude of the k-th harmonic
            std::function<double (int k)> phase = nullptr;          // optional: phase of the harmonics
        };

        // Vowel formants (Hz) for A, E, I, O, U
        constexpr float vowels[5][3] = { { 730, 1090, 2440 }, { 530, 1840, 2480 }, { 270, 2290, 3010 },
                                         { 570, 840, 2410 },  { 300, 870, 2240 } };

        double formantAmp (int k, float t, double f0, float sharp)
        {
            const float pos = t * 4.0f;
            const int a = std::min (3, (int) pos);
            const float fr = pos - (float) a;
            double amp = 0.0;
            for (int f = 0; f < 3; ++f)
            {
                const double fc = vowels[a][f] + (vowels[a + 1][f] - vowels[a][f]) * fr;
                const double bw = 80.0 + 40.0 * f;
                const double d = (k * f0 - fc) / (bw * sharp);
                amp += std::exp (-0.5 * d * d) / (1.0 + f);
            }
            return amp + 0.02 / k;
        }

        std::vector<TableDef> definitions()
        {
            using namespace std;
            return {
                { "Basic Shapes", [] (double x, float t)
                  {
                      const float s = sine (x);
                      const float tri = 1.0f - 4.0f * (float) std::abs (x - 0.5);
                      const float saw = (float) (2.0 * x - 1.0);
                      const float sq = x < 0.5 ? 1.0f : -1.0f;
                      const float p = t * 3.0f;
                      if (p < 1.0f) return s + (tri - s) * p;
                      if (p < 2.0f) return tri + (saw - tri) * (p - 1.0f);
                      return saw + (sq - saw) * (p - 2.0f);
                  }, nullptr },
                { "Pulse Width", [] (double x, float t) { return x < 0.5 - 0.46 * t ? 1.0f : -1.0f; }, nullptr },
                { "Soft Saw", nullptr, [] (int k, float t) { return (1.0 / k) * std::exp (-(k - 1) / (1.0 + 60.0 * t * t)); } },
                { "Hollow", nullptr, [] (int k, float t)
                  { return (k % 2 == 1) ? (1.0 / k) * std::exp (-(k - 1) / (2.0 + 80.0 * t * t)) : 0.0; } },
                { "Odd To Even", nullptr, [] (int k, float t) { return (1.0 / k) * ((k % 2 == 1) ? 1.0 : t); } },
                { "Bright Spectrum", nullptr, [] (int k, float t) { return std::pow ((double) k, -(2.2 - 1.4 * t)); } },
                { "Organ", nullptr, [] (int k, float t)
                  {
                      static const int bars[] = { 1, 2, 3, 4, 6, 8, 10, 12, 16 };
                      for (int i = 0; i < 9; ++i)
                          if (bars[i] == k)
                              return juce::jlimit (0.0, 1.0, t * 9.0 - i + 1.0) * (i == 0 ? 1.0 : 0.7);
                      return 0.0;
                  } },
                { "Glass", nullptr, [] (int k, float t)
                  {
                      if (k == 1) return 1.0;
                      if ((k & (k - 1)) == 0) return std::pow (0.55, std::log2 ((double) k)) * (0.3 + 0.7 * t);
                      return k % 3 == 0 ? 0.08 * t / k : 0.0;
                  } },
                { "Vowels", nullptr, [] (int k, float t) { return formantAmp (k, t, 110.0, 1.0f); } },
                { "Choir", nullptr, [] (int k, float t) { return formantAmp (k, t, 220.0, 1.8f) * (1.0 + 0.3 / k); } },
                { "Harmonic Sweep", nullptr, [] (int k, float t)
                  {
                      const double c = 1.0 + 31.0 * t * t, d = (k - c) / (1.5 + c * 0.15);
                      return std::exp (-0.5 * d * d) + 0.25 / k;
                  } },
                { "Comb Spectrum", nullptr, [] (int k, float t)
                  { return (1.0 / k) * (0.5 + 0.5 * std::cos (juce::MathConstants<double>::pi * k * (0.05 + 0.45 * t))); } },
                { "Breath", nullptr, [] (int k, float t)
                  {
                      const double a = hash01 ((uint32_t) k * 7919u), b = hash01 ((uint32_t) k * 104729u + 17u);
                      return (a + (b - a) * t) * std::exp (-k / 40.0) / std::sqrt ((double) k);
                  },
                  [] (int k) { return hash01 ((uint32_t) k * 31337u) * juce::MathConstants<double>::twoPi; } },
                { "Sync Saw", [] (double x, float t)
                  {
                      const double r = 1.0 + 7.0 * t;
                      const double p = x * r;
                      return (float) (2.0 * (p - std::floor (p)) - 1.0);
                  }, nullptr },
                { "Sync Square", [] (double x, float t)
                  {
                      const double p = x * (1.0 + 5.0 * t);
                      return (p - std::floor (p)) < 0.5 ? 1.0f : -1.0f;
                  }, nullptr },
                { "Wavefold", [] (double x, float t) { return std::sin ((1.0f + 5.0f * t) * sine (x) * 1.57f); }, nullptr },
                { "Saturate", [] (double x, float t)
                  {
                      const float g = 1.0f + 14.0f * t * t;
                      return std::tanh (g * sine (x)) / std::tanh (g);
                  }, nullptr },
                { "FM One", [] (double x, float t) { return sine (x + 0.9 * t * std::sin (juce::MathConstants<double>::twoPi * x)); }, nullptr },
                { "FM Two", [] (double x, float t) { return sine (x + 0.7 * t * std::sin (juce::MathConstants<double>::twoPi * 2.0 * x)); }, nullptr },
                { "FM Bell", [] (double x, float t) { return sine (2.0 * x + 0.8 * t * std::sin (juce::MathConstants<double>::twoPi * 5.0 * x)); }, nullptr },
                { "Phase Distort", [] (double x, float t)
                  {
                      const double d = 0.5 - 0.48 * t;
                      const double w = x < d ? x * 0.5 / d : 0.5 + (x - d) * 0.5 / (1.0 - d);
                      return -std::cos ((float) (juce::MathConstants<double>::twoPi * w));
                  }, nullptr },
                { "Resonant", [] (double x, float t)
                  {
                      const double ratio = 1.0 + 15.0 * t;
                      return (float) ((1.0 - x) * std::cos (juce::MathConstants<double>::twoPi * x * ratio) * 2.0 - (1.0 - x));
                  }, nullptr },
                { "Bit Steps", [] (double x, float t)
                  {
                      const float steps = 2.0f + 30.0f * (1.0f - t) * (1.0f - t);
                      return std::round (sine (x) * steps) / steps;
                  }, nullptr },
                { "Tri Fold", [] (double x, float t)
                  {
                      float y = (1.0f - 4.0f * (float) std::abs (x - 0.5)) * (1.0f + 3.0f * t);
                      while (std::abs (y) > 1.0f) y = (y > 0.0f ? 2.0f : -2.0f) - y;
                      return y;
                  }, nullptr },
            };
        }

        // Time domain -> harmonics (magnitude + phase) via an FFT
        Spectrum analyse (const std::vector<float>& cycle)
        {
            juce::dsp::FFT fft (11);
            std::vector<std::complex<float>> in ((size_t) kSize), out ((size_t) kSize);
            for (int i = 0; i < kSize; ++i) in[(size_t) i] = cycle[(size_t) i];
            fft.perform (in.data(), out.data(), false);

            Spectrum s ((size_t) kMaxHarmonics + 1);
            for (int k = 1; k <= kMaxHarmonics - 1; ++k)
                s[(size_t) k] = std::complex<double> (out[(size_t) k].real(), out[(size_t) k].imag()) * (2.0 / kSize);
            return s;
        }

        // Harmonics -> one cycle with `size` samples, only up to maxHarmonic.
        // Amplitudes are explicit: result = Σ |a_k| cos(2πkx + φ_k)
        std::vector<float> synthesise (const Spectrum& s, int size, int maxHarmonic)
        {
            const int order = (int) std::round (std::log2 ((double) size));
            juce::dsp::FFT fft (order);
            std::vector<std::complex<float>> in ((size_t) size), out ((size_t) size);
            const int top = std::min (maxHarmonic, size / 2 - 1);
            for (int k = 1; k <= top; ++k)
            {
                const auto c = s[(size_t) k] * 0.5;
                in[(size_t) k] = { (float) c.real(), (float) c.imag() };
                in[(size_t) (size - k)] = { (float) c.real(), (float) -c.imag() };
            }
            fft.perform (in.data(), out.data(), true);   // inverse; JUCE scales by 1/size

            std::vector<float> result ((size_t) size);
            for (int i = 0; i < size; ++i)
                result[(size_t) i] = out[(size_t) i].real() * (float) size;
            return result;
        }
    }

    const juce::StringArray& WavetableBank::names()
    {
        static const juce::StringArray list = []
        {
            juce::StringArray n;
            for (const auto& d : definitions())
                n.add (d.name);
            return n;
        }();
        return list;
    }

    const WavetableBank& WavetableBank::get()
    {
        static const WavetableBank instance;
        return instance;
    }

    WavetableBank::WavetableBank()
    {
        const auto defs = definitions();
        tables.resize (defs.size());

        for (size_t t = 0; t < defs.size(); ++t)
        {
            auto& levels = tables[t];
            for (int L = 0; L < kLevels; ++L)
            {
                const int harmonics = kMaxHarmonics >> L;
                levels[(size_t) L].size = juce::jlimit (256, 2048, juce::nextPowerOfTwo (harmonics * 8));
                levels[(size_t) L].mask = levels[(size_t) L].size - 1;
                levels[(size_t) L].data.assign ((size_t) (kFrames * levels[(size_t) L].size), 0.0f);
            }

            for (int f = 0; f < kFrames; ++f)
            {
                const float tf = (float) f / (float) (kFrames - 1);
                Spectrum spec;

                if (defs[t].wave)
                {
                    std::vector<float> cycle ((size_t) kSize);
                    for (int i = 0; i < kSize; ++i)
                        cycle[(size_t) i] = defs[t].wave ((double) i / kSize, tf);
                    spec = analyse (cycle);
                }
                else
                {
                    spec.assign ((size_t) kMaxHarmonics + 1, {});
                    for (int k = 1; k < kMaxHarmonics; ++k)
                    {
                        // sine phase (−90°) so the shapes match the classic drawings
                        const double ph = defs[t].phase ? defs[t].phase (k) : -juce::MathConstants<double>::halfPi;
                        spec[(size_t) k] = std::polar (defs[t].amp (k, tf), ph);
                    }
                }

                // Normalise each frame to a peak of 1 (measured at the full level)
                const auto full = synthesise (spec, levels[0].size, kMaxHarmonics);
                float peak = 1.0e-6f;
                for (float v : full) peak = std::max (peak, std::abs (v));
                const float norm = 0.95f / peak;

                for (int L = 0; L < kLevels; ++L)
                {
                    auto& lvl = levels[(size_t) L];
                    const auto cycle = L == 0 ? full : synthesise (spec, lvl.size, kMaxHarmonics >> L);
                    float* dst = lvl.data.data() + (size_t) f * (size_t) lvl.size;
                    for (int i = 0; i < lvl.size; ++i)
                        dst[i] = cycle[(size_t) i] * norm;
                }
            }
        }
    }

    float WavetableBank::sampleForDisplay (int table, float position, float phase) const
    {
        const auto& lvl = level (std::clamp (table, 0, numTables() - 1), 3);
        const float fp = std::clamp (position, 0.0f, 1.0f) * (kFrames - 1);
        const int fa = std::min ((int) fp, kFrames - 2);
        const float ff = fp - (float) fa;
        const int i = (int) (phase * (float) lvl.size) & lvl.mask;
        const float a = lvl.frame (fa)[i], b = lvl.frame (fa + 1)[i];
        return a + (b - a) * ff;
    }
}
