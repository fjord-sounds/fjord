#pragma once
#include <juce_dsp/juce_dsp.h>
#include <vector>
#include <complex>
#include "FxParams.h"
#include "../dsp/DspCommon.h"

namespace fjord::fx
{
    using dsp::Smoother;
    using dsp::softClip;
    using dsp::kPi;
    using dsp::kTwoPi;

    // ---------------------------------------------------------------------------------------------
    // Helpers

    // Delay line with Hermite interpolation (for modulated effects without noise/zipper).
    class DelayLine
    {
    public:
        void prepare (int maxSamples)
        {
            int size = 1;
            while (size < maxSamples + 4) size <<= 1;
            buffer.assign ((size_t) size, 0.0f);
            mask = size - 1;
            write = 0;
        }

        void clear() { std::fill (buffer.begin(), buffer.end(), 0.0f); }
        void push (float x) { buffer[(size_t) write] = x; write = (write + 1) & mask; }

        // delay in samples (>= 1): 1 = the most recently written sample
        float read (float delay) const
        {
            const float pos = (float) write - delay;
            const int i = (int) std::floor (pos);
            const float f = pos - (float) i;
            const float xm1 = at (i - 1), x0 = at (i), x1 = at (i + 1), x2 = at (i + 2);
            const float c1 = 0.5f * (x1 - xm1);
            const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
            const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
            return ((c3 * f + c2) * f + c1) * f + x0;
        }

        int maxDelay() const { return mask - 4; }

    private:
        float at (int i) const { return buffer[(size_t) (i & mask)]; }
        std::vector<float> buffer;
        int mask = 0, write = 0;
    };

    // Biquad (RBJ cookbook), transposed direct form II
    struct Biquad
    {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        std::array<float, 2> z1 {}, z2 {};

        float process (float x, int ch)
        {
            const float y = b0 * x + z1[(size_t) ch];
            z1[(size_t) ch] = b1 * x - a1 * y + z2[(size_t) ch];
            z2[(size_t) ch] = b2 * x - a2 * y;
            return y;
        }

        void reset() { z1.fill (0.0f); z2.fill (0.0f); }

        void set (double nb0, double nb1, double nb2, double na0, double na1, double na2)
        {
            b0 = (float) (nb0 / na0); b1 = (float) (nb1 / na0); b2 = (float) (nb2 / na0);
            a1 = (float) (na1 / na0); a2 = (float) (na2 / na0);
        }

        void highpass (double sr, double f, double q)
        {
            const double w = 2.0 * juce::MathConstants<double>::pi * f / sr, c = std::cos (w), a = std::sin (w) / (2.0 * q);
            set ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + a, -2 * c, 1 - a);
        }

        void lowpass (double sr, double f, double q)
        {
            const double w = 2.0 * juce::MathConstants<double>::pi * f / sr, c = std::cos (w), a = std::sin (w) / (2.0 * q);
            set ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + a, -2 * c, 1 - a);
        }

        void peak (double sr, double f, double q, double gainDb)
        {
            const double A = std::pow (10.0, gainDb / 40.0);
            const double w = 2.0 * juce::MathConstants<double>::pi * f / sr, c = std::cos (w), a = std::sin (w) / (2.0 * q);
            set (1 + a * A, -2 * c, 1 - a * A, 1 + a / A, -2 * c, 1 - a / A);
        }

        void shelf (double sr, double f, double gainDb, bool high)
        {
            const double A = std::pow (10.0, gainDb / 40.0);
            const double w = 2.0 * juce::MathConstants<double>::pi * f / sr, c = std::cos (w), s = std::sin (w);
            const double a = s / 2.0 * std::sqrt (2.0), sq = 2.0 * std::sqrt (A) * a;
            if (high)
                set (A * ((A + 1) + (A - 1) * c + sq), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sq),
                     (A + 1) - (A - 1) * c + sq, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sq);
            else
                set (A * ((A + 1) - (A - 1) * c + sq), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - sq),
                     (A + 1) + (A - 1) * c + sq, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sq);
        }
    };

    // Dry/wet with constant loudness (equal power)
    inline void equalPowerMix (float mix, float& dryGain, float& wetGain)
    {
        dryGain = std::cos (mix * kPi * 0.5f);
        wetGain = std::sin (mix * kPi * 0.5f);
    }

    struct StereoBuffer
    {
        float* l;
        float* r;
        int n;
    };

    // ---------------------------------------------------------------------------------------------
    // Drive: tape / tube / fold / hard, with 2x oversampling against aliasing

    class Drive
    {
    public:
        void prepare (double sr, int maxBlock)
        {
            sampleRate = (float) sr;
            oversampling.initProcessing ((size_t) maxBlock);
            dry.setSize (2, maxBlock);
            mix.setTime (0.02f, sampleRate);
            reset();
        }

        void reset() { oversampling.reset(); tone.fill (0.0f); lastGain = 1.0f; }

        void process (StereoBuffer b, const FxParams& p)
        {
            dry.copyFrom (0, 0, b.l, b.n);
            dry.copyFrom (1, 0, b.r, b.n);

            const float driveGain = std::pow (10.0f, p.driveAmount * 36.0f / 20.0f);
            const float compensation = 1.0f / (1.0f + p.driveAmount * 2.5f);

            float* chans[] = { b.l, b.r };
            juce::dsp::AudioBlock<float> block (chans, 2, (size_t) b.n);
            auto up = oversampling.processSamplesUp (block);

            // Ramp the gain linearly across the block (no zipper when turning)
            const int type = p.driveType;
            const auto n = up.getNumSamples();
            const float step = (driveGain - lastGain) / (float) n;
            for (size_t ch = 0; ch < 2; ++ch)
            {
                auto* d = up.getChannelPointer (ch);
                float g = lastGain;
                for (size_t i = 0; i < n; ++i)
                {
                    g += step;
                    d[i] = shape (type, d[i] * g) * compensation;
                }
            }
            lastGain = driveGain;
            oversampling.processSamplesDown (block);

            // Tone control (gentle low-pass after the saturation)
            const float fc = 1000.0f * std::pow (20.0f, p.driveTone);
            const float c = 1.0f - std::exp (-kTwoPi * std::min (fc, sampleRate * 0.45f) / sampleRate);
            for (int i = 0; i < b.n; ++i)
            {
                const float m = mix.process (p.driveMix);
                tone[0] += (b.l[i] - tone[0]) * c;
                tone[1] += (b.r[i] - tone[1]) * c;
                b.l[i] = dry.getSample (0, i) * (1.0f - m) + tone[0] * m;
                b.r[i] = dry.getSample (1, i) * (1.0f - m) + tone[1] * m;
            }
        }

        static float shape (int type, float x)
        {
            switch (type)
            {
                case 0:  return std::tanh (x + 0.15f) - 0.148885f;                       // Tape: soft, slightly asymmetric
                case 1:  return x >= 0.0f ? 1.0f - std::exp (-x) : -(1.0f - std::exp (1.3f * x)) / 1.3f;   // Tube
                case 2:  return std::sin (x * 1.3f);                                      // Fold
                default: return std::clamp (x, -1.0f, 1.0f);                              // Hard
            }
        }

    private:
        float sampleRate = 44100.0f;
        juce::dsp::Oversampling<float> oversampling { 2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true };
        juce::AudioBuffer<float> dry;
        std::array<float, 2> tone {};
        float lastGain = 1.0f;
        Smoother mix;
    };

    // ---------------------------------------------------------------------------------------------
    // Bitcrusher: bit depth and sample rate reduction

    class Crush
    {
    public:
        void prepare (double sr) { mix.setTime (0.02f, (float) sr); reset(); }
        void reset() { hold = { 0.0f, 0.0f }; phase = 1.0f; }

        void process (StereoBuffer b, const FxParams& p)
        {
            const float q = std::pow (2.0f, p.crushBits - 1.0f);
            const float step = 1.0f / std::max (1.0f, p.crushRate);
            for (int i = 0; i < b.n; ++i)
            {
                phase += step;
                if (phase >= 1.0f)
                {
                    phase -= 1.0f;
                    hold[0] = std::round (b.l[i] * q) / q;
                    hold[1] = std::round (b.r[i] * q) / q;
                }
                const float m = mix.process (p.crushMix);
                b.l[i] += (hold[0] - b.l[i]) * m;
                b.r[i] += (hold[1] - b.r[i]) * m;
            }
        }

    private:
        std::array<float, 2> hold {};
        float phase = 1.0f;
        Smoother mix;
    };

    // ---------------------------------------------------------------------------------------------
    // EQ: Low Cut, Low Shelf, two Mid Peaks, High Shelf, High Cut.
    // The design is shared with the UI so the graph shows exactly what the DSP does.

    enum class EqBand { LowCut, Low, Mid, Mid2, High, HighCut, Count };
    constexpr int kNumEqBands = (int) EqBand::Count;
    constexpr int kNumEqFilters = 8;   // the cuts use two biquads each at 24 dB/oct

    struct EqDesign
    {
        std::array<Biquad, kNumEqFilters> filters;
        std::array<bool, kNumEqFilters> use {};
        std::array<int, kNumEqFilters> band {};   // which band each filter belongs to

        void design (double sr, const FxParams::EqSettings& e)
        {
            const double nyquistSafe = sr * 0.45;
            // Butterworth Q values for one 12 dB stage and for the two stages of a 24 dB cut
            constexpr double q12 = 0.7071, q24a = 0.5412, q24b = 1.3066;

            filters[0].highpass (sr, std::min ((double) e.lowCut, nyquistSafe), e.lowCutSlope == 1 ? q24a : q12);
            filters[1].highpass (sr, std::min ((double) e.lowCut, nyquistSafe), q24b);
            filters[2].shelf (sr, std::min ((double) e.lowFreq, nyquistSafe), e.low, false);
            filters[3].peak (sr, std::min ((double) e.midFreq, nyquistSafe), e.midQ, e.mid);
            filters[4].peak (sr, std::min ((double) e.mid2Freq, nyquistSafe), e.mid2Q, e.mid2);
            filters[5].shelf (sr, std::min ((double) e.highFreq, nyquistSafe), e.high, true);
            filters[6].lowpass (sr, std::min ((double) e.highCut, nyquistSafe), e.highCutSlope == 1 ? q24a : q12);
            filters[7].lowpass (sr, std::min ((double) e.highCut, nyquistSafe), q24b);

            const bool lowCut = e.lowCut > 20.5f, highCut = e.highCut < 19900.0f;
            use = { lowCut, lowCut && e.lowCutSlope == 1, std::abs (e.low) > 0.01f, std::abs (e.mid) > 0.01f,
                    std::abs (e.mid2) > 0.01f, std::abs (e.high) > 0.01f, highCut, highCut && e.highCutSlope == 1 };
            band = { 0, 0, 1, 2, 3, 4, 5, 5 };
        }

        // Magnitude in dB at a frequency (for the graph). band < 0: all bands together.
        double magnitudeDb (double freq, double sr, int onlyBand = -1) const
        {
            const std::complex<double> z = std::polar (1.0, -2.0 * juce::MathConstants<double>::pi * freq / sr);
            double db = 0.0;
            for (size_t k = 0; k < filters.size(); ++k)
            {
                if (! use[k] || (onlyBand >= 0 && band[k] != onlyBand))
                    continue;
                const auto& b = filters[k];
                const auto num = (double) b.b0 + (double) b.b1 * z + (double) b.b2 * z * z;
                const auto den = 1.0 + (double) b.a1 * z + (double) b.a2 * z * z;
                db += 20.0 * std::log10 (std::max (1.0e-9, std::abs (num / den)));
            }
            return db;
        }
    };

    class Eq
    {
    public:
        void prepare (double sr) { sampleRate = sr; reset(); designed = false; }
        void reset() { for (auto& f : eq.filters) f.reset(); }

        void process (StereoBuffer b, const FxParams& p)
        {
            if (! designed || ! (p.eq == last))
            {
                last = p.eq;
                designed = true;
                eq.design (sampleRate, p.eq);
            }

            for (int i = 0; i < b.n; ++i)
            {
                float l = b.l[i], r = b.r[i];
                for (size_t k = 0; k < eq.filters.size(); ++k)
                    if (eq.use[k]) { l = eq.filters[k].process (l, 0); r = eq.filters[k].process (r, 1); }
                b.l[i] = l;
                b.r[i] = r;
            }
        }

    private:
        double sampleRate = 44100.0;
        EqDesign eq;
        FxParams::EqSettings last;
        bool designed = false;
    };

    // ---------------------------------------------------------------------------------------------
    // Compressor: stereo linked, soft knee, parallel mix, optional sidechain high-pass and automatic makeup
    class Comp
    {
    public:
        void prepare (double sr) { sampleRate = (float) sr; mix.setTime (0.02f, sampleRate); reset(); }
        void reset() { envDb = 0.0f; hpX = { 0.0f, 0.0f }; hpY = { 0.0f, 0.0f }; }   // gain reduction in dB (0 = none)

        float getGainReduction() const { return lastReduction; }
        float getInputPeak() const     { return lastInput; }     // linear peak of the last block
        float getOutputPeak() const    { return lastOutput; }

        // Static curve: gain reduction in dB for an input level (shared with the UI graph)
        static float reduction (float levelDb, float threshold, float ratio, float knee)
        {
            const float over = levelDb - threshold;
            const float slope = 1.0f - 1.0f / std::max (1.0f, ratio);
            if (knee < 0.01f || over >= knee * 0.5f) return std::max (0.0f, over) * slope;
            if (over <= -knee * 0.5f) return 0.0f;
            const float k = over + knee * 0.5f;
            return slope * k * k / (2.0f * knee);
        }

        // Automatic makeup: gives back half of the reduction a full-scale signal would get
        static float autoMakeup (float threshold, float ratio)
        {
            return 0.5f * reduction (0.0f, threshold, ratio, 0.0f);
        }

        void process (StereoBuffer b, const FxParams& p)
        {
            const float att = std::exp (-1.0f / (std::max (0.0001f, p.compAttack) * sampleRate));
            const float rel = std::exp (-1.0f / (std::max (0.005f, p.compRelease) * sampleRate));
            const float makeupDb = p.compMakeup + (p.compAutoGain ? autoMakeup (p.compThreshold, p.compRatio) : 0.0f);
            const float makeup = juce::Decibels::decibelsToGain (makeupDb);
            const bool hpf = p.compScHpf > 20.5f;
            const float hpA = 1.0f / (1.0f + dsp::kTwoPi * p.compScHpf / sampleRate);
            float maxRed = 0.0f, inPeak = 0.0f, outPeak = 0.0f;

            for (int i = 0; i < b.n; ++i)
            {
                float dl = b.l[i], dr = b.r[i];
                inPeak = std::max (inPeak, std::max (std::abs (dl), std::abs (dr)));
                if (hpf)
                {
                    // the detector ignores the lows, so kicks and basses do not pump the whole mix
                    hpY[0] = hpA * (hpY[0] + dl - hpX[0]); hpX[0] = dl; dl = hpY[0];
                    hpY[1] = hpA * (hpY[1] + dr - hpX[1]); hpX[1] = dr; dr = hpY[1];
                }
                const float level = juce::Decibels::gainToDecibels (std::max (std::abs (dl), std::abs (dr)), -120.0f);
                const float red = reduction (level, p.compThreshold, p.compRatio, p.compKnee);

                envDb = red > envDb ? red + (envDb - red) * att : red + (envDb - red) * rel;
                maxRed = std::max (maxRed, envDb);
                const float g = juce::Decibels::decibelsToGain (-envDb) * makeup;
                const float m = mix.process (p.compMix);
                b.l[i] = b.l[i] * (1.0f - m) + b.l[i] * g * m;
                b.r[i] = b.r[i] * (1.0f - m) + b.r[i] * g * m;
                outPeak = std::max (outPeak, std::max (std::abs (b.l[i]), std::abs (b.r[i])));
            }
            lastReduction = maxRed;
            lastInput = inPeak;
            lastOutput = outPeak;
        }

    private:
        float sampleRate = 44100.0f, envDb = 0.0f, lastReduction = 0.0f, lastInput = 0.0f, lastOutput = 0.0f;
        std::array<float, 2> hpX {}, hpY {};
        Smoother mix;
    };

    // ---------------------------------------------------------------------------------------------
    // Juno-style chorus: two modulated taps, right channel in anti-phase – wide and soft

    class Chorus
    {
    public:
        void prepare (double sr)
        {
            sampleRate = (float) sr;
            for (auto& d : lines) d.prepare ((int) (0.03 * sr));
            mix.setTime (0.02f, sampleRate);
            depth.setTime (0.05f, sampleRate);
            reset();
        }

        void reset() { for (auto& d : lines) d.clear(); }

        void process (StereoBuffer b, const FxParams& p)
        {
            const float inc = p.chorusRate / sampleRate;
            const float base = 0.0075f * sampleRate;
            for (int i = 0; i < b.n; ++i)
            {
                const float dep = depth.process (p.chorusDepth) * 0.0055f * sampleRate;
                lines[0].push (b.l[i]);
                lines[1].push (b.r[i]);

                auto tri = [] (float ph) { ph -= std::floor (ph); return ph < 0.5f ? 4.0f * ph - 1.0f : 3.0f - 4.0f * ph; };
                const float a = tri (phase), c = tri (phase + 0.33f);
                const float wl = 0.5f * (lines[0].read (base + dep * a) + lines[0].read (base * 1.3f + dep * c));
                const float wr = 0.5f * (lines[1].read (base - dep * a + dep) + lines[1].read (base * 1.3f - dep * c + dep));

                phase += inc;
                if (phase >= 1.0f) phase -= 1.0f;

                const float m = mix.process (p.chorusMix);
                b.l[i] = b.l[i] * (1.0f - 0.5f * m) + wl * m * 0.8f;
                b.r[i] = b.r[i] * (1.0f - 0.5f * m) + wr * m * 0.8f;
            }
        }

    private:
        float sampleRate = 44100.0f, phase = 0.0f;
        std::array<DelayLine, 2> lines;
        Smoother mix, depth;
    };

    // ---------------------------------------------------------------------------------------------
    // Phaser: 6 allpass stages per channel with feedback

    class Phaser
    {
    public:
        void prepare (double sr) { sampleRate = (float) sr; mix.setTime (0.02f, sampleRate); reset(); }
        void reset() { for (auto& ch : state) ch.fill (0.0f); feedback = { 0.0f, 0.0f }; }

        void process (StereoBuffer b, const FxParams& p)
        {
            const float inc = p.phaserRate / sampleRate;
            const float fb = p.phaserFeedback * 0.85f;
            float* io[] = { b.l, b.r };

            for (int i = 0; i < b.n; ++i)
            {
                const float m = mix.process (p.phaserMix);
                for (int ch = 0; ch < 2; ++ch)
                {
                    const float lfo = 0.5f - 0.5f * std::cos (kTwoPi * (phase + (ch == 0 ? 0.0f : 0.25f)));
                    const float fc = std::min (180.0f * std::exp2 (lfo * p.phaserDepth * 4.5f), sampleRate * 0.4f);
                    const float t = std::tan (kPi * fc / sampleRate);
                    const float coef = (t - 1.0f) / (t + 1.0f);

                    const float x = io[ch][i];
                    float y = x + feedback[(size_t) ch] * fb;
                    for (auto& s : state[(size_t) ch])
                    {
                        const float out = coef * y + s;
                        s = y - coef * out;
                        y = out;
                    }
                    feedback[(size_t) ch] = softClip (y);
                    io[ch][i] = x * (1.0f - 0.5f * m) + y * 0.5f * m;
                }
                phase += inc;
                if (phase >= 1.0f) phase -= 1.0f;
            }
        }

    private:
        float sampleRate = 44100.0f, phase = 0.0f;
        std::array<std::array<float, 6>, 2> state {};
        std::array<float, 2> feedback {};
        Smoother mix;
    };

    // ---------------------------------------------------------------------------------------------
    // Flanger

    class Flanger
    {
    public:
        void prepare (double sr)
        {
            sampleRate = (float) sr;
            for (auto& d : lines) d.prepare ((int) (0.02 * sr));
            mix.setTime (0.02f, sampleRate);
            reset();
        }

        void reset() { for (auto& d : lines) d.clear(); last = { 0.0f, 0.0f }; }

        void process (StereoBuffer b, const FxParams& p)
        {
            const float inc = p.flangerRate / sampleRate;
            const float fb = p.flangerFeedback * 0.9f;
            float* io[] = { b.l, b.r };

            for (int i = 0; i < b.n; ++i)
            {
                const float m = mix.process (p.flangerMix);
                for (int ch = 0; ch < 2; ++ch)
                {
                    const float lfo = 0.5f - 0.5f * std::cos (kTwoPi * (phase + (ch == 0 ? 0.0f : 0.25f)));
                    const float delay = (0.0004f + p.flangerDepth * 0.0055f * lfo) * sampleRate + 1.0f;
                    const float x = io[ch][i];
                    lines[(size_t) ch].push (softClip (x + last[(size_t) ch] * fb));
                    const float y = lines[(size_t) ch].read (delay);
                    last[(size_t) ch] = y;
                    io[ch][i] = x * (1.0f - 0.5f * m) + y * 0.5f * m;
                }
                phase += inc;
                if (phase >= 1.0f) phase -= 1.0f;
            }
        }

    private:
        float sampleRate = 44100.0f, phase = 0.0f;
        std::array<DelayLine, 2> lines;
        std::array<float, 2> last {};
        Smoother mix;
    };

    // ---------------------------------------------------------------------------------------------
    // Delay: stereo or ping-pong, tempo synced, band limiting in the feedback, ducking

    class Delay
    {
    public:
        void prepare (double sr)
        {
            sampleRate = (float) sr;
            for (auto& d : lines) d.prepare ((int) (4.1 * sr));
            mix.setTime (0.03f, sampleRate);
            time.setTime (0.08f, sampleRate);   // time changes glide like a tape echo
            reset();
        }

        void reset()
        {
            for (auto& d : lines) d.clear();
            lp.fill (0.0f);
            hp.fill (0.0f);
            duckEnv = 0.0f;
            time.reset (-1.0f);
        }

        void process (StereoBuffer b, const FxParams& p, double bpm)
        {
            const float seconds = p.delaySync ? (float) (p.delayBeats * 60.0 / std::max (20.0, bpm)) : p.delayTimeMs * 0.001f;
            const float target = std::clamp (seconds * sampleRate, 16.0f, (float) lines[0].maxDelay() - 4.0f);
            if (time.value < 0.0f) time.reset (target);

            const float lpc = 1.0f - std::exp (-kTwoPi * (18000.0f * std::pow (0.07f, p.delayDamp)) / sampleRate);
            const float hpc = 1.0f - std::exp (-kTwoPi * 90.0f / sampleRate);
            const float duckAtt = 1.0f - std::exp (-1.0f / (0.005f * sampleRate));
            const float duckRel = 1.0f - std::exp (-1.0f / (0.3f * sampleRate));
            const float fb = p.delayFeedback;

            for (int i = 0; i < b.n; ++i)
            {
                const float d = time.process (target);
                float wl = lines[0].read (d), wr = lines[1].read (d);

                // Feedback colouring: low-pass (damp) + gentle high-pass + soft saturation
                auto colour = [&] (float x, int ch)
                {
                    lp[(size_t) ch] += (x - lp[(size_t) ch]) * lpc;
                    hp[(size_t) ch] += (lp[(size_t) ch] - hp[(size_t) ch]) * hpc;
                    return softClip (lp[(size_t) ch] - hp[(size_t) ch]);
                };
                const float cl = colour (wl, 0), cr = colour (wr, 1);

                const float inL = b.l[i], inR = b.r[i];
                if (p.delayPingPong)
                {
                    lines[0].push ((inL + inR) * 0.5f + cr * fb);
                    lines[1].push (cl * fb);
                }
                else
                {
                    lines[0].push (inL + cl * fb);
                    lines[1].push (inR + cr * fb);
                }

                const float level = std::max (std::abs (inL), std::abs (inR));
                duckEnv += (level - duckEnv) * (level > duckEnv ? duckAtt : duckRel);
                const float duck = 1.0f - p.delayDuck * std::min (1.0f, duckEnv * 4.0f);

                float dg, wg;
                equalPowerMix (mix.process (p.delayMix), dg, wg);
                b.l[i] = inL * dg + wl * wg * duck;
                b.r[i] = inR * dg + wr * wg * duck;
            }
        }

    private:
        float sampleRate = 44100.0f, duckEnv = 0.0f;
        std::array<DelayLine, 2> lines;
        std::array<float, 2> lp {}, hp {};
        Smoother mix, time;
    };

    // ---------------------------------------------------------------------------------------------
    // Reverb: 8-channel feedback delay network with a Hadamard matrix, input diffusion,
    // frequency-dependent decay and slowly modulated lines (no metallic ringing)

    class Reverb
    {
    public:
        void prepare (double sr)
        {
            sampleRate = (float) sr;
            const float scale = sampleRate / 44100.0f;
            for (size_t i = 0; i < 8; ++i)
                lines[i].prepare ((int) (baseLengths[i] * 2.8f * scale) + 64);
            for (size_t c = 0; c < 2; ++c)
                for (size_t k = 0; k < 4; ++k)
                    diffusers[c][k].prepare ((int) (diffLengths[c][k] * scale) + 4);
            predelay.prepare ((int) (0.3 * sr));
            mix.setTime (0.03f, sampleRate);
            size.setTime (0.25f, sampleRate);
            size.reset (-1.0f);
            reset();
        }

        void reset()
        {
            for (auto& l : lines) l.clear();
            for (auto& c : diffusers) for (auto& d : c) d.clear();
            predelay.clear();
            damp.fill (0.0f);
        }

        void process (StereoBuffer b, const FxParams& p)
        {
            const float scale = sampleRate / 44100.0f;
            if (size.value < 0.0f) size.reset (p.reverbSize);
            const float dampC = 1.0f - std::exp (-kTwoPi * (16000.0f * std::pow (0.08f, p.reverbDamp)) / sampleRate);
            const float preSamples = std::max (1.0f, p.reverbPredelay * sampleRate);
            const float t60 = std::max (0.1f, p.reverbDecay);

            // Decay factor per line once per block (from the current size)
            std::array<float, 8> decayGain {};
            for (size_t k = 0; k < 8; ++k)
                decayGain[k] = std::pow (10.0f, -3.0f * baseLengths[k] * (0.45f + 2.2f * size.value) * scale / (t60 * sampleRate));

            for (int i = 0; i < b.n; ++i)
            {
                const float sz = size.process (p.reverbSize);
                const float lenScale = (0.45f + 2.2f * sz) * scale;

                // Pre-delay + input diffusion (two differently tuned chains -> stereo)
                predelay.push ((b.l[i] + b.r[i]) * 0.5f);
                const float pre = predelay.read (preSamples);
                float in[2] = { pre, pre };
                for (size_t c = 0; c < 2; ++c)
                    for (size_t k = 0; k < 4; ++k)
                        in[c] = diffusers[c][k].process (in[c]);

                // Read the lines (modulated)
                std::array<float, 8> y {};
                for (size_t k = 0; k < 8; ++k)
                {
                    const float len = baseLengths[k] * lenScale;
                    const float mod = 7.0f * scale * std::sin (modPhase[k]);
                    modPhase[k] += modRates[k] * kTwoPi / sampleRate;
                    if (modPhase[k] > kTwoPi) modPhase[k] -= kTwoPi;
                    const float out = lines[k].read (len + mod);
                    damp[k] += (out - damp[k]) * dampC;
                    y[k] = damp[k] * decayGain[k];
                }

                // Hadamard mixing (fast Walsh-Hadamard transform)
                std::array<float, 8> h = y;
                for (int len = 1; len < 8; len <<= 1)
                    for (int s = 0; s < 8; s += len * 2)
                        for (int j = s; j < s + len; ++j)
                        {
                            const float a = h[(size_t) j], c = h[(size_t) (j + len)];
                            h[(size_t) j] = a + c;
                            h[(size_t) (j + len)] = a - c;
                        }

                constexpr float norm = 0.35355339f;   // 1/sqrt(8)
                for (size_t k = 0; k < 8; ++k)
                    lines[k].push (h[k] * norm + in[k % 2] * 0.5f);

                const float outL = (y[0] + y[2] + y[4] + y[6] - y[1] * 0.5f) * 0.45f;
                const float outR = (y[1] + y[3] + y[5] + y[7] - y[0] * 0.5f) * 0.45f;
                const float mid = 0.5f * (outL + outR), side = 0.5f * (outL - outR) * p.reverbWidth;

                float dg, wg;
                equalPowerMix (mix.process (p.reverbMix), dg, wg);
                b.l[i] = b.l[i] * dg + (mid + side) * wg;
                b.r[i] = b.r[i] * dg + (mid - side) * wg;
            }
        }

    private:
        // Schroeder allpass for the input diffusion
        struct Allpass
        {
            DelayLine line;
            int length = 1;
            void prepare (int len) { line.prepare (len + 4); length = len; }
            void clear() { line.clear(); }
            float process (float x)
            {
                constexpr float g = 0.62f;
                const float d = line.read ((float) length);
                const float v = x + g * d;
                line.push (v);
                return d - g * v;
            }
        };

        static constexpr float baseLengths[8] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
        static constexpr float diffLengths[2][4] = { { 142, 107, 379, 277 }, { 151, 113, 367, 283 } };
        static constexpr float modRates[8] = { 0.11f, 0.17f, 0.23f, 0.29f, 0.31f, 0.37f, 0.41f, 0.47f };

        float sampleRate = 44100.0f;
        std::array<DelayLine, 8> lines;
        std::array<std::array<Allpass, 4>, 2> diffusers;
        DelayLine predelay;
        std::array<float, 8> damp {}, modPhase { 0.0f, 0.8f, 1.6f, 2.4f, 3.2f, 4.0f, 4.8f, 5.6f };
        Smoother mix, size;
    };

    // ---------------------------------------------------------------------------------------------
    // Stereo width (mid/side)

    class Width
    {
    public:
        void prepare (double sr) { width.setTime (0.03f, (float) sr); width.reset (1.0f); }
        void reset() {}

        void process (StereoBuffer b, const FxParams& p)
        {
            for (int i = 0; i < b.n; ++i)
            {
                const float w = width.process (p.width);
                const float mid = 0.5f * (b.l[i] + b.r[i]), side = 0.5f * (b.l[i] - b.r[i]) * w;
                b.l[i] = mid + side;
                b.r[i] = mid - side;
            }
        }

    private:
        Smoother width;
    };
}
