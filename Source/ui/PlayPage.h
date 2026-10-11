#pragma once
#include <juce_dsp/juce_dsp.h>
#include <optional>
#include "SynthPage.h"
#include "PresetBrowser.h"

namespace fjord::ui
{
    // ---------------------------------------------------------------------------------------------
    // Level and spectrum of the output, computed on the UI thread from the scope buffer.
    class OutputAnalyser
    {
    public:
        static constexpr int fftOrder = 11, fftSize = 1 << fftOrder;

        explicit OutputAnalyser (ScopeBuffer& b) : buffer (b) { spectrum.fill (-120.0f); }

        // Call once per frame.
        void update()
        {
            const int w = buffer.writePos.load (std::memory_order_acquire);
            const bool fresh = w != lastWrite;
            lastWrite = w;

            if (! fresh)
            {
                for (auto& s : spectrum) s = std::max (-120.0f, s - 1.5f);
                for (auto& b : bands) b *= 0.9f;
                level *= 0.9f;
                return;
            }

            float sum = 0.0f;
            for (int i = 0; i < fftSize; ++i)
            {
                const float v = sample (w - fftSize + i);
                fftData[(size_t) i] = v;
                sum += v * v;
            }
            std::fill (fftData.begin() + fftSize, fftData.end(), 0.0f);
            const float rms = std::sqrt (sum / (float) fftSize);
            level += (std::min (1.0f, rms * 4.0f) - level) * 0.25f;

            window.multiplyWithWindowingTable (fftData.data(), (size_t) fftSize);
            fft.performFrequencyOnlyForwardTransform (fftData.data());
            const double sr = std::max (8000.0, buffer.sampleRate.load());

            std::array<float, 3> energy {};
            for (int i = 1; i < fftSize / 2; ++i)
            {
                const float mag = fftData[(size_t) i] / (float) fftSize * 4.0f;
                const float freq = (float) i * (float) sr / (float) fftSize;
                const float tilt = 3.0f * std::log2 (std::max (20.0f, freq) / 1000.0f);
                const float db = juce::Decibels::gainToDecibels (mag, -120.0f) + tilt;
                auto& s = spectrum[(size_t) i];
                s = db > s ? s + (db - s) * 0.6f : std::max (db, s - 1.5f);
                energy[freq < 250.0f ? 0 : (freq < 2000.0f ? 1 : 2)] += mag * mag;
            }
            for (size_t b = 0; b < 3; ++b)
            {
                const float e = std::clamp ((juce::Decibels::gainToDecibels (std::sqrt (energy[b]), -80.0f) + 60.0f) / 50.0f, 0.0f, 1.0f);
                bands[b] += (e - bands[b]) * (e > bands[b] ? 0.4f : 0.08f);
            }
        }

        float sample (int i) const { return buffer.data[(size_t) ((i + ScopeBuffer::size * 4) & (ScopeBuffer::size - 1))]; }
        int writePosition() const { return lastWrite; }
        double sampleRate() const { return std::max (8000.0, buffer.sampleRate.load()); }

        // dB (tilted) at a frequency
        float spectrumAt (float freq) const
        {
            const float bin = freq / (float) sampleRate() * (float) fftSize;
            const int i = std::clamp ((int) bin, 1, fftSize / 2 - 2);
            const float t = std::clamp (bin - (float) i, 0.0f, 1.0f);
            return spectrum[(size_t) i] * (1.0f - t) + spectrum[(size_t) i + 1] * t;
        }

        float level = 0.0f;                  // 0..1, smoothed
        std::array<float, 3> bands {};       // low / mid / high energy 0..1, smoothed

    private:
        ScopeBuffer& buffer;
        juce::dsp::FFT fft { fftOrder };
        juce::dsp::WindowingFunction<float> window { (size_t) fftSize, juce::dsp::WindowingFunction<float>::hann };
        std::array<float, fftSize * 2> fftData {};
        std::array<float, fftSize / 2> spectrum {};
        int lastWrite = -1;
    };

    // ---------------------------------------------------------------------------------------------
    // The landscape: a fjord at night (or at dawn in the light theme). The northern lights follow the
    // waveform and the level, the three mountain ridges rise with the lows, mids and highs, the water
    // mirrors everything. Click to switch to a classic oscilloscope or a spectrum.
    class FjordVisual : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
    {
    public:
        FjordVisual (ScopeBuffer& b, APVTS& s) : analyser (b)
        {
            for (int i = 0; i < dsp::kNumOsc; ++i)
                oscOn[(size_t) i] = s.getRawParameterValue (params::osc (i, "on"));
            juce::Random r (7);
            for (auto& star : stars) star = { r.nextFloat(), r.nextFloat() * 0.55f, 0.6f + r.nextFloat() * 1.2f, r.nextFloat() * 6.28f };
            // Ridge profiles: a fjord – steep walls at the sides, a valley in the middle, jagged crests
            for (size_t k = 0; k < ridges.size(); ++k)
            {
                float ph[4];
                for (auto& v : ph) v = r.nextFloat() * 6.28f;
                for (size_t i = 0; i < ridges[k].size(); ++i)
                {
                    const float x = (float) i / (float) (ridges[k].size() - 1);
                    const float wall = std::pow (std::abs (x - 0.5f) * 2.0f, k == 0 ? 0.9f : 1.6f);   // far ridge: gentler
                    const float detail = 0.5f + 0.25f * std::sin (x * 9.0f + ph[0]) + 0.15f * std::sin (x * 23.0f + ph[1])
                                         + 0.07f * std::sin (x * 51.0f + ph[2]) + 0.03f * std::sin (x * 97.0f + ph[3]);
                    ridges[k][i] = std::clamp ((k == 0 ? 0.45f : 0.12f) + wall * 0.75f + (detail - 0.5f) * 0.45f, 0.05f, 1.1f);
                }
            }
            setTooltip ("Click to switch: fjord / oscilloscope / spectrum");
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
            startTimerHz (30);
        }

        void mouseDown (const juce::MouseEvent&) override { mode = (mode + 1) % 3; repaint(); }

        // For offscreen screenshots (no timer runs there): advance the animation by some frames.
        void advance (int frames)
        {
            for (int i = 0; i < frames; ++i) { analyser.update(); time += 1.0f / 30.0f; }
        }

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            const auto r = getLocalBounds().toFloat();
            const bool dark = Theme::get().isDark();

            juce::Path clip;
            clip.addRoundedRectangle (r, 6.0f);
            g.saveState();
            g.reduceClipRegion (clip);

            // Sky
            const auto skyTop = dark ? juce::Colour (0xff070d16) : juce::Colour (0xffe9f1f4);
            const auto skyBottom = dark ? juce::Colour (0xff15253a) : juce::Colour (0xffcfe0e6);
            g.setGradientFill (juce::ColourGradient (skyTop, 0.0f, r.getY(), skyBottom, 0.0f, r.getBottom(), false));
            g.fillRect (r);

            if (mode == 0)      paintFjord (g, r, p, dark);
            else if (mode == 1) paintScope (g, r, p);
            else                paintSpectrum (g, r, p);

            g.restoreState();

            g.setColour (dark ? p.text.withAlpha (0.55f) : p.textDim);
            drawSpacedText (g, mode == 0 ? "FJORD" : (mode == 1 ? "SCOPE" : "SPECTRUM"), r.reduced (14.0f, 10.0f), 9.5f,
                            juce::Justification::topLeft, 0.3f);
        }

    private:
        void timerCallback() override
        {
            if (! isShowing()) return;
            analyser.update();
            time += 1.0f / 30.0f;
            repaint();
        }

        void paintFjord (juce::Graphics& g, juce::Rectangle<float> r, const Palette& p, bool dark)
        {
            const float lvl = analyser.level;
            const float horizon = r.getY() + r.getHeight() * 0.72f;

            // Stars (night) or a low sun (dawn)
            if (dark)
            {
                for (const auto& s : stars)
                {
                    const float tw = 0.45f + 0.55f * (0.5f + 0.5f * std::sin (time * 1.3f + s.phase));
                    g.setColour (juce::Colours::white.withAlpha (0.35f * tw));
                    g.fillEllipse (r.getX() + s.x * r.getWidth(), r.getY() + s.y * r.getHeight(), s.size, s.size);
                }
            }
            else
            {
                const auto sun = juce::Point<float> (r.getX() + r.getWidth() * 0.72f, horizon - r.getHeight() * 0.12f);
                const float rad = r.getHeight() * (0.16f + 0.04f * lvl);
                const auto sunColour = juce::Colour (0xffbfe3dc);   // pale arctic sun (independent of the accent)
                g.setGradientFill (juce::ColourGradient (sunColour.withAlpha (0.5f), sun, sunColour.withAlpha (0.0f),
                                                         sun.translated (rad * 2.4f, 0.0f), true));
                g.fillEllipse (juce::Rectangle<float> (rad * 4.8f, rad * 4.8f).withCentre (sun));
                g.setColour (sunColour.withAlpha (0.8f));
                g.fillEllipse (juce::Rectangle<float> (rad, rad).withCentre (sun));
            }

            // Northern lights: one curtain per active oscillator, shaped by the waveform and lifted by the level
            const int n = 96;
            const auto base = p.accent;
            // Aurora colours: green, teal, violet, cyan
            const juce::Colour colours[] = { juce::Colour (dark ? 0xff58d1a4 : 0xff1f9a7c), juce::Colour (dark ? 0xff3fb8c8 : 0xff1d7f93),
                                             juce::Colour (dark ? 0xff8a7be0 : 0xff5d55b0), juce::Colour (dark ? 0xff6fe0d0 : 0xff2a8c80) };
            juce::ignoreUnused (base);
            const int w = analyser.writePosition();
            int active = 0;
            for (auto* on : oscOn) active += on->load() >= 0.5f ? 1 : 0;
            // more than four curtains share the same sky instead of reaching further down
            const float layerStep = active > 4 ? 4.0f / (float) active : 1.0f;
            int index = -1;
            for (int osc = 0; osc < dsp::kNumOsc; ++osc)
            {
                if (oscOn[(size_t) osc]->load() < 0.5f) continue;
                const float layer = (float) ++index * layerStep;
                const auto colour = colours[osc % 4];
                juce::Path curtain;
                const float yBase = r.getY() + r.getHeight() * (0.18f + 0.06f * layer);
                const float amp = r.getHeight() * (0.05f + 0.10f * lvl);
                const float depth = r.getHeight() * (0.18f + 0.22f * lvl + 0.05f * layer);
                std::array<juce::Point<float>, n + 1> top;
                for (int i = 0; i <= n; ++i)
                {
                    const float x = (float) i / (float) n;
                    const float wave = analyser.sample (w - 2048 + i * 16 + index * 300) * 0.8f;
                    const float y = yBase + amp * std::sin (x * (5.0f + layer) + time * (0.35f + 0.12f * layer) + (float) index)
                                    + r.getHeight() * 0.06f * wave;
                    top[(size_t) i] = { r.getX() + x * r.getWidth(), y };
                }
                curtain.startNewSubPath (top[0]);
                for (int i = 1; i <= n; ++i) curtain.lineTo (top[(size_t) i]);
                for (int i = n; i >= 0; --i) curtain.lineTo (top[(size_t) i].translated (0.0f, depth));
                curtain.closeSubPath();

                const float alpha = (dark ? 0.26f : 0.16f) + 0.3f * lvl;
                g.setGradientFill (juce::ColourGradient (colour.withAlpha (alpha), 0.0f, yBase - amp,
                                                         colour.withAlpha (0.0f), 0.0f, yBase + depth, false));
                g.fillPath (curtain);
            }

            // Mountains: far (highs), middle (mids), near (lows)
            const auto mountain = dark ? juce::Colour (0xff060b12) : juce::Colour (0xff8ea4ae);
            juce::Path reflections;
            for (int k = 0; k < 3; ++k)
            {
                const float energy = analyser.bands[(size_t) (2 - k)];
                const float height = r.getHeight() * (0.26f + 0.12f * (float) k) * (0.85f + 0.35f * energy);
                juce::Path m;
                m.startNewSubPath (r.getX(), horizon);
                const auto& prof = ridges[(size_t) k];
                const int pts = (int) prof.size();
                for (int i = 0; i < pts; ++i)
                {
                    const float x = (float) i / (float) (pts - 1);
                    m.lineTo (r.getX() + x * r.getWidth(), horizon - height * prof[(size_t) i]);
                }
                m.lineTo (r.getRight(), horizon);
                m.closeSubPath();
                const float shade = 1.0f - 0.18f * (float) (2 - k);
                const auto c = dark ? mountain.interpolatedWith (juce::Colour (0xff2a3d58), 0.4f * (float) (2 - k)) : mountain.darker (0.25f * (float) k).withMultipliedBrightness (shade + 0.15f);
                g.setColour (c);
                g.fillPath (m);
                reflections.addPath (m, juce::AffineTransform::verticalFlip (horizon * 2.0f));
            }

            // Water: reflection and shimmer lines that react to the highs
            const auto water = juce::Rectangle<float> (r.getX(), horizon, r.getWidth(), r.getBottom() - horizon);
            g.setColour (dark ? juce::Colour (0xff0a121c) : juce::Colour (0xffcadbe1));
            g.fillRect (water);
            g.saveState();
            g.reduceClipRegion (water.toNearestInt());
            g.setColour ((dark ? juce::Colour (0xff060b12) : juce::Colour (0xff8ea4ae)).withAlpha (0.45f));
            g.fillPath (reflections);
            for (int i = 0; i < 9; ++i)
            {
                const float y = horizon + water.getHeight() * ((float) i + 0.5f) / 9.0f;
                const float len = r.getWidth() * (0.08f + 0.22f * analyser.bands[2]) * (1.0f - (float) i / 12.0f);
                const float x = r.getX() + r.getWidth() * (0.5f + 0.35f * std::sin (time * 0.4f + (float) i * 1.7f)) - len * 0.5f;
                g.setColour (p.accent.withAlpha (0.12f + 0.3f * lvl));
                g.fillRoundedRectangle (x, y, len, 1.5f, 0.75f);
            }
            g.restoreState();
        }

        void paintScope (juce::Graphics& g, juce::Rectangle<float> r, const Palette& p)
        {
            const auto area = r.reduced (14.0f, 28.0f);
            g.setColour (p.line.withAlpha (0.5f));
            g.fillRect (area.getX(), area.getCentreY(), area.getWidth(), 1.0f);

            constexpr int window = 1024;
            const int end = analyser.writePosition();
            int start = end - window - 1;
            for (int i = end - window - 1; i > end - ScopeBuffer::size + window; --i)
                if (analyser.sample (i - 1) <= 0.0f && analyser.sample (i) > 0.0f) { start = i; break; }
            float peak = 0.05f;
            for (int i = 0; i < window; ++i) peak = std::max (peak, std::abs (analyser.sample (start + i)));
            scopePeak = std::max (peak, scopePeak * 0.92f);

            juce::Path path;
            for (int i = 0; i < window; ++i)
            {
                const float x = area.getX() + area.getWidth() * (float) i / (float) (window - 1);
                const float y = area.getCentreY() - analyser.sample (start + i) / scopePeak * area.getHeight() * 0.45f;
                if (i == 0) path.startNewSubPath (x, y); else path.lineTo (x, y);
            }
            g.setColour (p.accent.withAlpha (0.25f));
            g.strokePath (path, juce::PathStrokeType (6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (p.accent);
            g.strokePath (path, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        void paintSpectrum (juce::Graphics& g, juce::Rectangle<float> r, const Palette& p)
        {
            const auto area = r.reduced (14.0f, 26.0f);
            constexpr int bars = 48;
            const float bw = area.getWidth() / (float) bars;
            for (int i = 0; i < bars; ++i)
            {
                const float f0 = 30.0f * std::pow (600.0f, (float) i / (float) bars);
                const float f1 = 30.0f * std::pow (600.0f, (float) (i + 1) / (float) bars);
                const float db = std::max (analyser.spectrumAt (f0), analyser.spectrumAt (std::sqrt (f0 * f1)));
                const float h = std::clamp ((db + 84.0f) / 84.0f, 0.0f, 1.0f);
                auto& hold = peaks[(size_t) i];
                hold = std::max (h, hold - 0.008f);
                const auto bar = juce::Rectangle<float> (area.getX() + (float) i * bw + 1.5f, area.getBottom() - area.getHeight() * h,
                                                         bw - 3.0f, area.getHeight() * h);
                g.setGradientFill (juce::ColourGradient (p.accent, 0.0f, area.getBottom(), p.accent.withRotatedHue (0.1f).withAlpha (0.6f),
                                                         0.0f, area.getY(), false));
                g.fillRoundedRectangle (bar, 2.0f);
                g.setColour (p.text.withAlpha (0.5f));
                g.fillRect (bar.getX(), area.getBottom() - area.getHeight() * hold - 2.0f, bar.getWidth(), 1.5f);
            }
        }

        struct Star { float x, y, size, phase; };
        OutputAnalyser analyser;
        std::array<std::atomic<float>*, dsp::kNumOsc> oscOn {};
        std::array<Star, 70> stars {};
        std::array<std::array<float, 64>, 3> ridges {};
        std::array<float, 48> peaks {};
        float time = 0.0f, scopePeak = 0.1f;
        int mode = 0;
    };

    // ---------------------------------------------------------------------------------------------
    // XY pad: two targets (macros, cutoff, resonance) on one surface. The dot glides after the mouse,
    // leaves a trail and can drift on its own ("Drift"), slowly breathing life into a held chord.
    class XYPad : public juce::Component, private juce::Timer
    {
    public:
        struct Target { juce::String paramId, fallbackName; int macro; };

        XYPad (APVTS& s) : state (s)
        {
            for (int i = 0; i < dsp::kNumMacros; ++i)
                targets.push_back ({ params::macro (i), "Macro " + juce::String (i + 1), i });
            targets.push_back ({ params::cutoff, "Cutoff", -1 });
            targets.push_back ({ params::reso, "Resonance", -1 });
            targets.push_back ({ params::glide, "Glide", -1 });
            targets.push_back ({ params::analog, "Analog", -1 });
            setMouseCursor (juce::MouseCursor::CrosshairCursor);
            startTimerHz (60);
        }

        const std::vector<Target>& getTargets() const { return targets; }
        juce::String targetName (int index) const
        {
            const auto& t = targets[(size_t) juce::jlimit (0, (int) targets.size() - 1, index)];
            if (t.macro < 0) return t.fallbackName;
            const auto names = juce::StringArray::fromTokens (state.state.getProperty ("macroNames").toString(), "|", "");
            const auto n = t.macro < names.size() ? names[t.macro].trim() : juce::String();
            return n.isNotEmpty() ? n : t.fallbackName;
        }

        int axisTarget (bool yAxis) const
        {
            return juce::jlimit (0, (int) targets.size() - 1, (int) state.state.getProperty (yAxis ? "xyY" : "xyX", yAxis ? 1 : 0));
        }

        void setAxisTarget (bool yAxis, int index)
        {
            endGestures();
            state.state.setProperty (yAxis ? "xyY" : "xyX", index, nullptr);
            syncFromParameters();
        }

        bool isDrifting() const { return state.state.getProperty ("xyDrift", false); }
        void setDrifting (bool d) { state.state.setProperty ("xyDrift", d, nullptr); if (! d) endGestures(); }

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            const auto r = getLocalBounds().toFloat();
            const auto area = padArea();

            g.setColour (p.background.withAlpha (0.6f));
            g.fillRoundedRectangle (r, 6.0f);

            // Soft glow under the dot
            const auto dot = toScreen (pos);
            g.setGradientFill (juce::ColourGradient (p.accent.withAlpha (0.28f), dot, p.accent.withAlpha (0.0f),
                                                     dot.translated (area.getWidth() * 0.35f, 0.0f), true));
            g.fillRect (r);

            // Dot grid
            for (int ix = 0; ix <= 8; ++ix)
                for (int iy = 0; iy <= 8; ++iy)
                {
                    const auto q = toScreen ({ (float) ix / 8.0f, (float) iy / 8.0f });
                    const float d = q.getDistanceFrom (dot) / area.getWidth();
                    g.setColour (p.textDim.withAlpha (std::clamp (0.5f - d, 0.12f, 0.5f)));
                    g.fillEllipse (q.x - 1.2f, q.y - 1.2f, 2.4f, 2.4f);
                }

            // Crosshair to the axes
            g.setColour (p.accent.withAlpha (0.35f));
            g.drawDashedLine ({ dot.x, dot.y, dot.x, area.getBottom() }, dashes, 2, 1.0f);
            g.drawDashedLine ({ area.getX(), dot.y, dot.x, dot.y }, dashes, 2, 1.0f);

            // Trail
            for (size_t i = 1; i < trail.size(); ++i)
            {
                const float a = (float) i / (float) trail.size();
                g.setColour (p.accent.withAlpha (0.45f * a));
                g.drawLine ({ toScreen (trail[i - 1]), toScreen (trail[i]) }, 1.0f + 2.5f * a);
            }

            // The dot
            const float pulse = 1.0f + 0.12f * std::sin (phase * 4.0f);
            g.setColour (p.accent.withAlpha (0.25f));
            g.fillEllipse (juce::Rectangle<float> (26.0f * pulse, 26.0f * pulse).withCentre (dot));
            g.setColour (p.accent);
            g.fillEllipse (juce::Rectangle<float> (13.0f, 13.0f).withCentre (dot));
            g.setColour (p.panel);
            g.fillEllipse (juce::Rectangle<float> (4.0f, 4.0f).withCentre (dot));

            // Axis names and values
            g.setColour (p.textDim);
            drawSpacedText (g, targetName (axisTarget (false)).toUpperCase() + "  " + percent (pos.x), r.reduced (12.0f, 6.0f),
                            9.5f, juce::Justification::bottomRight, 0.16f);
            g.saveState();
            g.addTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi, r.getX() + 16.0f, r.getBottom() - 12.0f));
            drawSpacedText (g, targetName (axisTarget (true)).toUpperCase() + "  " + percent (pos.y),
                            { r.getX() + 16.0f, r.getBottom() - 22.0f, r.getHeight() - 30.0f, 14.0f }, 9.5f,
                            juce::Justification::centredLeft, 0.16f);
            g.restoreState();
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            dragging = true;
            beginGestures();
            target = fromScreen (e.position);
        }

        void mouseDrag (const juce::MouseEvent& e) override { target = fromScreen (e.position); }

        void mouseUp (const juce::MouseEvent&) override
        {
            dragging = false;
            if (! isDrifting()) endGestures();
        }

        void mouseDoubleClick (const juce::MouseEvent&) override { target = { 0.5f, 0.5f }; }

    private:
        static inline const float dashes[] = { 3.0f, 4.0f };

        juce::Rectangle<float> padArea() const { return getLocalBounds().toFloat().reduced (30.0f, 18.0f).withTrimmedBottom (8.0f); }

        juce::Point<float> toScreen (juce::Point<float> v) const
        {
            const auto a = padArea();
            return { a.getX() + v.x * a.getWidth(), a.getBottom() - v.y * a.getHeight() };
        }

        juce::Point<float> fromScreen (juce::Point<float> s) const
        {
            const auto a = padArea();
            return { std::clamp ((s.x - a.getX()) / a.getWidth(), 0.0f, 1.0f), std::clamp ((a.getBottom() - s.y) / a.getHeight(), 0.0f, 1.0f) };
        }

        static juce::String percent (float v) { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; }

        juce::RangedAudioParameter* param (bool yAxis) const
        {
            return state.getParameter (targets[(size_t) axisTarget (yAxis)].paramId);
        }

        void beginGestures()
        {
            if (gestures) return;
            gestures = true;
            for (bool y : { false, true }) if (auto* p = param (y)) p->beginChangeGesture();
        }

        void endGestures()
        {
            if (! gestures) return;
            gestures = false;
            for (bool y : { false, true }) if (auto* p = param (y)) p->endChangeGesture();
        }

        void syncFromParameters()
        {
            if (auto* px = param (false)) pos.x = target.x = px->getValue();
            if (auto* py = param (true))  pos.y = target.y = py->getValue();
        }

        void timerCallback() override
        {
            if (! isShowing()) return;
            phase += 1.0f / 60.0f;

            if (isDrifting() && ! dragging)
            {
                beginGestures();
                driftTime += 1.0f / 60.0f;
                target = { 0.5f + 0.32f * std::sin (driftTime * 0.31f) + 0.12f * std::sin (driftTime * 0.83f + 1.0f),
                           0.5f + 0.32f * std::sin (driftTime * 0.23f + 2.0f) + 0.12f * std::sin (driftTime * 0.71f) };
            }

            if (dragging || isDrifting())
            {
                // Glide after the target (a little inertia feels like a real instrument)
                pos += (target - pos) * 0.18f;
                if (auto* px = param (false)) if (std::abs (px->getValue() - pos.x) > 1.0e-4f) px->setValueNotifyingHost (pos.x);
                if (auto* py = param (true))  if (std::abs (py->getValue() - pos.y) > 1.0e-4f) py->setValueNotifyingHost (pos.y);
            }
            else
            {
                syncFromParameters();   // follow presets and automation
            }

            trail.push_back (pos);
            while (trail.size() > 36) trail.erase (trail.begin());
            repaint();
        }

        APVTS& state;
        std::vector<Target> targets;
        juce::Point<float> pos { 0.5f, 0.5f }, target { 0.5f, 0.5f };
        std::vector<juce::Point<float>> trail;
        bool dragging = false, gestures = false;
        float phase = 0.0f, driftTime = 0.0f;
    };

    // Small pill switch that is not a parameter (UI state).
    class ChipButton : public juce::Component
    {
    public:
        explicit ChipButton (juce::String t) : text (std::move (t)) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }

        std::function<void()> onClick;
        std::function<bool()> isOn;

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            const bool on = isOn && isOn();
            auto r = getLocalBounds().toFloat().reduced (0.5f);
            if (on) { g.setColour (p.accent); g.fillRoundedRectangle (r, r.getHeight() * 0.5f); }
            else    { g.setColour (hover ? p.textDim : p.line); g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f); }
            g.setColour (on ? p.panel : (hover ? p.text : p.textDim));
            drawSpacedText (g, text.toUpperCase(), r, 9.5f, juce::Justification::centred, 0.14f, on);
        }

        void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
        void mouseExit (const juce::MouseEvent&) override  { hover = false; repaint(); }
        void mouseUp (const juce::MouseEvent& e) override  { if (getLocalBounds().contains (e.getPosition()) && onClick) { onClick(); repaint(); } }

    private:
        juce::String text;
        bool hover = false;
    };

    // ---------------------------------------------------------------------------------------------
    // Snapshots of the eight macros: store A and B, then morph between them with the slider.
    // "Vary" nudges all macros a little (a happy accident generator), "Reset" goes back to the preset.
    class MacroMorph : public juce::Component, private juce::Timer
    {
    public:
        explicit MacroMorph (APVTS& s) : state (s), storeA ("Store A", false), storeB ("Store B", false),
                                         vary ("Vary", false), reset ("Reset", false)
        {
            for (auto* b : { &storeA, &storeB, &vary, &reset }) addAndMakeVisible (b);
            storeA.onClick = [this] { store ("snapA"); };
            storeB.onClick = [this] { store ("snapB"); };
            vary.onClick = [this] { variation(); };
            reset.onClick = [this] { restorePreset(); };
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
            startTimerHz (15);
        }

        // Called when a preset was loaded: its macro values become the "Reset" point.
        void rememberPresetValues() { presetValues = current(); }

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            const auto track = trackArea();
            const bool hasA = state.state.hasProperty ("snapA"), hasB = state.state.hasProperty ("snapB");

            g.setColour (p.textDim);
            drawSpacedText (g, "MORPH", getLocalBounds().removeFromTop (16).toFloat(), 9.5f, juce::Justification::centredLeft, 0.25f);

            // Track with the A and B ends
            g.setColour (p.track);
            g.fillRoundedRectangle (track, track.getHeight() * 0.5f);
            const float x = track.getX() + track.getWidth() * morph;
            g.setGradientFill (juce::ColourGradient (p.accent.withAlpha (0.25f), track.getX(), 0.0f, p.accent, x, 0.0f, false));
            g.fillRoundedRectangle (track.withRight (x), track.getHeight() * 0.5f);
            for (auto [label, has, px] : { std::tuple<const char*, bool, float> { "A", hasA, track.getX() - 16.0f },
                                           { "B", hasB, track.getRight() + 16.0f } })
            {
                const auto c = juce::Rectangle<float> (22.0f, 22.0f).withCentre ({ px, track.getCentreY() });
                g.setColour (has ? p.accent : p.line);
                if (has) g.fillEllipse (c); else g.drawEllipse (c.reduced (0.5f), 1.2f);
                g.setColour (has ? p.panel : p.textDim);
                drawSpacedText (g, label, c, 10.0f, juce::Justification::centred, 0.0f, true);
            }
            g.setColour (p.panel);
            g.fillEllipse (juce::Rectangle<float> (18.0f, 18.0f).withCentre ({ x, track.getCentreY() }));
            g.setColour (p.accent);
            g.drawEllipse (juce::Rectangle<float> (18.0f, 18.0f).withCentre ({ x, track.getCentreY() }), 2.0f);

            if (! hasA || ! hasB)
            {
                g.setColour (p.textDim);
                g.setFont (font (11.0f));
                g.drawText ("Store two macro settings, then slide between them.",
                            track.translated (0.0f, 18.0f).withHeight (16.0f).expanded (20.0f, 0.0f), juce::Justification::centred);
            }

            // The eight macros at a glance: A (left tick), B (right tick) and the current value (bar)
            const auto bars = barsArea();
            const auto now = current();
            const auto a = load ("snapA"), b = load ("snapB");
            const float cw = bars.getWidth() / (float) dsp::kNumMacros;
            for (int i = 0; i < dsp::kNumMacros; ++i)
            {
                const auto col = juce::Rectangle<float> (bars.getX() + (float) i * cw, bars.getY(), cw, bars.getHeight()).reduced (cw * 0.22f, 0.0f);
                g.setColour (p.track);
                g.fillRoundedRectangle (col, 3.0f);
                const float h = col.getHeight() * now[(size_t) i];
                g.setColour (p.accent.withAlpha (0.75f));
                g.fillRoundedRectangle (col.withTop (col.getBottom() - h), 3.0f);
                auto tick = [&] (float v, bool left, bool has)
                {
                    if (! has) return;
                    const float y = col.getBottom() - col.getHeight() * v;
                    g.setColour (p.text.withAlpha (0.7f));
                    g.fillRect (left ? col.getX() - 4.0f : col.getCentreX(), y - 0.75f, col.getWidth() * 0.5f + 4.0f, 1.5f);
                };
                tick (a[(size_t) i], true, hasA);
                tick (b[(size_t) i], false, hasB);
            }
        }

        void resized() override
        {
            auto r = getLocalBounds();
            auto buttons = r.removeFromBottom (30);
            const int w = (buttons.getWidth() - 3 * 8) / 4;
            for (auto* b : { &storeA, &storeB, &vary, &reset })
            {
                b->setBounds (buttons.removeFromLeft (w));
                buttons.removeFromLeft (8);
            }
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            if (! trackArea().expanded (8.0f, 14.0f).contains (e.position)) return;
            ensureSnapshots();
            setMorph (e.position.x);
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (trackArea().expanded (40.0f, 30.0f).contains (e.mouseDownPosition)) setMorph (e.position.x);
        }

    private:
        juce::Rectangle<float> barsArea() const
        {
            auto r = getLocalBounds().toFloat().withTrimmedBottom (44.0f);
            return r.withTop (trackArea().getBottom() + 34.0f).reduced (28.0f, 0.0f);
        }

        juce::Rectangle<float> trackArea() const
        {
            auto r = getLocalBounds().toFloat().withTrimmedTop (22.0f).withTrimmedBottom (40.0f);
            return r.reduced (32.0f, 0.0f).withSizeKeepingCentre (r.getWidth() - 64.0f, 8.0f).withY (r.getY() + 14.0f);
        }

        std::array<float, dsp::kNumMacros> current() const
        {
            std::array<float, dsp::kNumMacros> v {};
            for (int i = 0; i < dsp::kNumMacros; ++i)
                v[(size_t) i] = state.getParameter (params::macro (i))->getValue();
            return v;
        }

        void apply (const std::array<float, dsp::kNumMacros>& v)
        {
            for (int i = 0; i < dsp::kNumMacros; ++i)
            {
                auto* p = state.getParameter (params::macro (i));
                p->beginChangeGesture();
                p->setValueNotifyingHost (std::clamp (v[(size_t) i], 0.0f, 1.0f));
                p->endChangeGesture();
            }
        }

        std::array<float, dsp::kNumMacros> load (const char* prop) const
        {
            std::array<float, dsp::kNumMacros> v = current();
            const auto tokens = juce::StringArray::fromTokens (state.state.getProperty (prop).toString(), ",", "");
            for (int i = 0; i < std::min (tokens.size(), dsp::kNumMacros); ++i)
                v[(size_t) i] = tokens[i].getFloatValue();
            return v;
        }

        void store (const char* prop)
        {
            juce::StringArray a;
            for (float v : current()) a.add (juce::String (v, 4));
            state.state.setProperty (prop, a.joinIntoString (","), nullptr);
            morph = juce::String (prop) == "snapA" ? 0.0f : 1.0f;
            repaint();
        }

        void ensureSnapshots()
        {
            if (! state.state.hasProperty ("snapA")) store ("snapA");
            if (! state.state.hasProperty ("snapB")) store ("snapB");
        }

        void setMorph (float x)
        {
            const auto t = trackArea();
            morph = std::clamp ((x - t.getX()) / t.getWidth(), 0.0f, 1.0f);
            const auto a = load ("snapA"), b = load ("snapB");
            std::array<float, dsp::kNumMacros> v {};
            for (size_t i = 0; i < v.size(); ++i)
                v[i] = a[i] + (b[i] - a[i]) * morph;
            apply (v);
            repaint();
        }

        void variation()
        {
            auto v = current();
            for (auto& x : v)
                x = std::clamp (x + (rng.nextFloat() - 0.5f) * 0.3f, 0.0f, 1.0f);
            apply (v);
        }

        void restorePreset()
        {
            if (presetValues) apply (*presetValues);
        }

        void timerCallback() override
        {
            if (! isShowing()) return;
            juce::String key;
            for (float v : current()) key << juce::String (v, 3) << ",";
            key << state.state.getProperty ("snapA").toString() << state.state.getProperty ("snapB").toString()
                << (Theme::get().isDark() ? "d" : "l");
            if (key != shownKey) { shownKey = key; repaint(); }
        }

        juce::String shownKey;

        APVTS& state;
        PillButton storeA, storeB, vary, reset;
        float morph = 0.0f;
        std::optional<std::array<float, dsp::kNumMacros>> presetValues;
        juce::Random rng;
    };

    // ---------------------------------------------------------------------------------------------
    // Performance page: the fjord visual, an XY pad, the eight macros and the macro morph.
    class PlayPage : public juce::Component, private juce::Timer
    {
    public:
        PlayPage (APVTS& s, ScopeBuffer& scopeBuffer)
            : state (s), visualSection ("Output"), xySection ("XY Pad"), macroSection ("Macros"), morphSection ("Snapshots"),
              visual (scopeBuffer, s), xy (s), morph (s), drift ("Drift"), macroLock (s, locks::kMacros, false)
        {
            macroSection.addAndMakeVisible (macroLock);
            for (auto* c : { &visualSection, &xySection, &macroSection, &morphSection })
                addAndMakeVisible (c);
            visualSection.addAndMakeVisible (visual);
            xySection.addAndMakeVisible (xy);
            morphSection.addAndMakeVisible (morph);

            for (int i = 0; i < dsp::kNumMacros; ++i)
            {
                macros[(size_t) i] = std::make_unique<Knob> (s, params::macro (i), "Macro " + juce::String (i + 1));
                macros[(size_t) i]->setLabelHeight (20);
                macroSection.addAndMakeVisible (*macros[(size_t) i]);
            }

            for (bool yAxis : { false, true })
            {
                auto& box = yAxis ? yTarget : xTarget;
                box.setTooltip (yAxis ? "Vertical axis" : "Horizontal axis");
                xySection.addAndMakeVisible (box);
                box.onChange = [this, yAxis, &box] { xy.setAxisTarget (yAxis, box.getSelectedId() - 1); };
            }
            drift.isOn = [this] { return xy.isDrifting(); };
            drift.onClick = [this] { xy.setDrifting (! xy.isDrifting()); };
            xySection.addAndMakeVisible (drift);

            updateNames();
            presetName = state.state.getProperty ("presetName").toString();
            morph.rememberPresetValues();
            startTimerHz (4);
        }

        void prepareSnapshot() { visual.advance (40); }

        void resized() override
        {
            constexpr int gap = 10;
            auto r = getLocalBounds();
            auto top = r.removeFromTop ((r.getHeight() - gap) * 58 / 100);
            r.removeFromTop (gap);
            const int rightWidth = r.getWidth() * 36 / 100;   // XY pad and snapshots share one column

            xySection.setBounds (top.removeFromRight (rightWidth));
            top.removeFromRight (gap);
            visualSection.setBounds (top);
            visual.setBounds (visualSection.content().withTrimmedTop (-4).reduced (2, 0));

            auto t = xySection.titleArea();
            drift.setBounds (t.removeFromRight (64).withSizeKeepingCentre (64, 22));
            t.removeFromRight (8);
            yTarget.setBounds (t.removeFromRight (100).withSizeKeepingCentre (100, 24));
            t.removeFromRight (6);
            xTarget.setBounds (t.removeFromRight (100).withSizeKeepingCentre (100, 24));
            xy.setBounds (xySection.content().withTrimmedTop (-4).reduced (2, 0));

            morphSection.setBounds (r.removeFromRight (rightWidth));
            r.removeFromRight (gap);
            macroSection.setBounds (r);
            macroSection.placeTitleLock (macroLock);
            morph.setBounds (morphSection.content().reduced (8, 4));

            auto m = macroSection.content().reduced (4, 6);
            const int w = m.getWidth() / 4, h = m.getHeight() / 2;
            for (int i = 0; i < dsp::kNumMacros; ++i)
                macros[(size_t) i]->setBounds (juce::Rectangle<int> (m.getX() + (i % 4) * w, m.getY() + (i / 4) * h, w, h).reduced (10, 2));
        }

    private:
        void timerCallback() override
        {
            updateNames();
            const auto name = state.state.getProperty ("presetName").toString();
            if (name != presetName)
            {
                presetName = name;
                morph.rememberPresetValues();
            }
        }

        void updateNames()
        {
            const auto names = juce::StringArray::fromTokens (state.state.getProperty ("macroNames").toString(), "|", "");
            juce::StringArray labels;
            for (int i = 0; i < dsp::kNumMacros; ++i)
            {
                const auto n = i < names.size() ? names[i].trim() : juce::String();
                labels.add (n.isNotEmpty() ? n : "Macro " + juce::String (i + 1));
                macros[(size_t) i]->setLabel (labels[i]);
            }

            // Axis menus show the current macro names
            const auto key = labels.joinIntoString ("|") + juce::String (xy.axisTarget (false)) + juce::String (xy.axisTarget (true));
            if (key == menuKey) return;
            menuKey = key;
            for (bool yAxis : { false, true })
            {
                auto& box = yAxis ? yTarget : xTarget;
                box.clear (juce::dontSendNotification);
                for (int i = 0; i < (int) xy.getTargets().size(); ++i)
                    box.addItem ((yAxis ? "Y: " : "X: ") + xy.targetName (i), i + 1);
                box.setSelectedId (xy.axisTarget (yAxis) + 1, juce::dontSendNotification);
            }
        }

        APVTS& state;
        Section visualSection, xySection, macroSection, morphSection;
        FjordVisual visual;
        XYPad xy;
        MacroMorph morph;
        ChipButton drift;
        LockButton macroLock;
        juce::ComboBox xTarget, yTarget;
        std::array<std::unique_ptr<Knob>, dsp::kNumMacros> macros;
        juce::String presetName, menuKey;
    };
}
