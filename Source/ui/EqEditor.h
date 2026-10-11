#pragma once
#include <juce_dsp/juce_dsp.h>
#include "Controls.h"
#include "Displays.h"
#include "PresetBrowser.h"
#include "../fx/Effects.h"

namespace fjord::ui
{
    // Parameter handles of the EQ, shared by the graph and the band controls.
    struct EqParams
    {
        struct Band
        {
            const char* name;
            const char* freqId;
            const char* gainId;     // nullptr for the cuts
            const char* qId;        // only the peaks
            const char* slopeId;    // only the cuts
        };

        static constexpr Band bands[] = {
            { "Low Cut",  "lowcut",   nullptr,  nullptr, "lcslope" },
            { "Low",      "lowfreq",  "low",    nullptr, nullptr },
            { "Mid 1",    "midfreq",  "mid",    "midq",  nullptr },
            { "Mid 2",    "mid2freq", "mid2",   "mid2q", nullptr },
            { "High",     "highfreq", "high",   nullptr, nullptr },
            { "High Cut", "highcut",  nullptr,  nullptr, "hcslope" },
        };

        explicit EqParams (APVTS& s) : state (s)
        {
            on = s.getRawParameterValue (params::fx (2, "on"));
        }

        juce::RangedAudioParameter* param (const char* id) const
        {
            return id == nullptr ? nullptr : state.getParameter (params::fx (2, id));
        }

        float get (const char* id) const
        {
            auto* p = param (id);
            return p == nullptr ? 0.0f : p->convertFrom0to1 (p->getValue());
        }

        void set (const char* id, float plain) const
        {
            if (auto* p = param (id))
                p->setValueNotifyingHost (p->convertTo0to1 (plain));
        }

        void begin (const char* id) const { if (auto* p = param (id)) p->beginChangeGesture(); }
        void end (const char* id) const   { if (auto* p = param (id)) p->endChangeGesture(); }

        void setAsGesture (const char* id, float plain) const { begin (id); set (id, plain); end (id); }

        void resetBand (int b) const
        {
            for (auto* id : { bands[b].freqId, bands[b].gainId, bands[b].qId, bands[b].slopeId })
                if (auto* p = param (id))
                {
                    p->beginChangeGesture();
                    p->setValueNotifyingHost (p->getDefaultValue());
                    p->endChangeGesture();
                }
        }

        fx::FxParams::EqSettings settings() const
        {
            fx::FxParams::EqSettings e;
            e.lowCut = get ("lowcut");     e.lowCutSlope = juce::roundToInt (get ("lcslope"));
            e.lowFreq = get ("lowfreq");   e.low = get ("low");
            e.midFreq = get ("midfreq");   e.mid = get ("mid");   e.midQ = get ("midq");
            e.mid2Freq = get ("mid2freq"); e.mid2 = get ("mid2"); e.mid2Q = get ("mid2q");
            e.highFreq = get ("highfreq"); e.high = get ("high");
            e.highCut = get ("highcut");   e.highCutSlope = juce::roundToInt (get ("hcslope"));
            return e;
        }

        // A band counts as active when it changes the sound.
        bool isActive (int b) const
        {
            switch (b)
            {
                case 0: return get ("lowcut") > 20.5f;
                case 5: return get ("highcut") < 19900.0f;
                default: return std::abs (get (bands[b].gainId)) > 0.01f;
            }
        }

        bool isOn() const { return on->load() > 0.5f; }

        APVTS& state;
        std::atomic<float>* on = nullptr;
    };

    // ---------------------------------------------------------------------------------------------
    // Interactive EQ graph:
    //   drag a node        frequency (horizontal) and gain (vertical); Shift = fine
    //   mouse wheel        Q of the peaks, slope of the cuts
    //   double-click node  reset the band
    //   double-click space place a free peak band there
    //   right-click node   band menu (reset, slope, Q presets)
    // In the background: spectrum of the plugin output.
    class EqGraph : public juce::Component, private juce::Timer
    {
    public:
        EqGraph (EqParams& eqParams, ScopeBuffer* scopeBuffer) : eq (eqParams), scope (scopeBuffer)
        {
            spectrum.fill (-120.0f);
            startTimerHz (30);
        }

        std::function<void (int)> onBandSelected;

        void selectBand (int b)
        {
            if (b != selected) { selected = b; repaint(); }
        }

        int getSelectedBand() const { return selected; }

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            const auto r = plotArea();
            const bool on = eq.isOn();
            const auto accent = on ? p.accent : p.textDim;

            g.setColour (p.background.withAlpha (0.55f));
            g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);

            // Spectrum of the output
            if (scope != nullptr && spectrumVisible)
            {
                juce::Path spec;
                spec.startNewSubPath (r.getX(), r.getBottom());
                const int steps = (int) r.getWidth() / 2;
                for (int i = 0; i <= steps; ++i)
                {
                    const float x = (float) i / (float) steps;
                    const float db = spectrumAt (freqFor (x));
                    const float y = r.getBottom() - std::clamp ((db + 84.0f) / 84.0f, 0.0f, 1.0f) * r.getHeight();
                    spec.lineTo (r.getX() + x * r.getWidth(), y);
                }
                spec.lineTo (r.getRight(), r.getBottom());
                spec.closeSubPath();
                g.setColour (p.textDim.withAlpha (0.13f));
                g.fillPath (spec);
            }

            // Grid: frequencies and dB lines with labels
            g.setFont (font (9.5f, Weight::Medium));
            for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
            {
                const float x = xFor (f);
                g.setColour (p.line.withAlpha (0.55f));
                g.fillRect (x, r.getY(), 1.0f, r.getHeight());
                g.setColour (p.textDim.withAlpha (0.8f));
                const auto label = f >= 1000.0f ? juce::String ((int) (f / 1000.0f)) + "k" : juce::String ((int) f);
                g.drawText (label, juce::Rectangle<float> (x + 3.0f, r.getBottom() - 14.0f, 30.0f, 12.0f), juce::Justification::centredLeft);
            }
            for (float db : { -12.0f, -6.0f, 0.0f, 6.0f, 12.0f })
            {
                const float y = yFor (db);
                g.setColour (db == 0.0f ? p.line : p.line.withAlpha (0.4f));
                g.fillRect (r.getX(), y, r.getWidth(), 1.0f);
                if (db != 0.0f)
                {
                    g.setColour (p.textDim.withAlpha (0.8f));
                    g.drawText ((db > 0 ? "+" : "") + juce::String ((int) db),
                                juce::Rectangle<float> (r.getX() + 4.0f, y - 12.0f, 30.0f, 11.0f), juce::Justification::centredLeft);
                }
            }

            fx::EqDesign design;
            design.design (sampleRate, eq.settings());

            // Curve of the selected (or hovered) band
            const int focus = dragBand >= 0 ? dragBand : (hoverBand >= 0 ? hoverBand : selected);
            if (focus >= 0 && eq.isActive (focus))
            {
                auto band = curvePath (design, focus);
                juce::Path area (band);
                area.lineTo (r.getRight(), yFor (0.0f));
                area.lineTo (r.getX(), yFor (0.0f));
                area.closeSubPath();
                g.setColour (accent.withAlpha (0.16f));
                g.fillPath (area);
            }

            // Total response
            const auto total = curvePath (design, -1);
            g.setColour (accent);
            g.strokePath (total, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            // Nodes
            for (int b = 0; b < fx::kNumEqBands; ++b)
            {
                const auto c = nodePosition (b);
                const bool active = eq.isActive (b), sel = b == selected, hot = b == hoverBand || b == dragBand;
                const float rad = hot ? 8.0f : 6.5f;
                if (sel || hot)
                {
                    g.setColour (accent.withAlpha (0.18f));
                    g.fillEllipse (juce::Rectangle<float> (rad * 3.2f, rad * 3.2f).withCentre (c));
                }
                g.setColour (p.panel);
                g.fillEllipse (juce::Rectangle<float> (rad * 2.0f, rad * 2.0f).withCentre (c));
                g.setColour (active ? accent : p.textDim);
                if (active) g.fillEllipse (juce::Rectangle<float> (rad * 2.0f, rad * 2.0f).withCentre (c).reduced (1.5f));
                else        g.drawEllipse (juce::Rectangle<float> (rad * 2.0f, rad * 2.0f).withCentre (c).reduced (1.0f), 1.4f);
                g.setColour (active ? p.panel : p.textDim);
                g.setFont (font (9.0f, Weight::SemiBold));
                g.drawText (juce::String (b + 1), juce::Rectangle<float> (rad * 2.0f, rad * 2.0f).withCentre (c),
                            juce::Justification::centred);
            }

            // Readout of the band under the mouse / being dragged
            const int info = dragBand >= 0 ? dragBand : hoverBand;
            if (info >= 0)
            {
                const auto text = describe (info);
                const auto f = font (11.5f, Weight::Medium);
                const float w = juce::GlyphArrangement::getStringWidth (f, text) + 20.0f;
                const auto c = nodePosition (info);
                auto box = juce::Rectangle<float> (w, 22.0f).withCentre ({ c.x, c.y < r.getCentreY() ? c.y + 26.0f : c.y - 26.0f });
                box = box.constrainedWithin (r.reduced (2.0f));
                g.setColour (p.panel.withAlpha (0.95f));
                g.fillRoundedRectangle (box, 11.0f);
                g.setColour (p.line);
                g.drawRoundedRectangle (box, 11.0f, 1.0f);
                g.setColour (p.text);
                g.setFont (f);
                g.drawText (text, box, juce::Justification::centred);
            }
            else if (! hint.isEmpty())
            {
                g.setColour (p.textDim);
                g.setFont (font (10.5f));
                g.drawText (hint, r.reduced (8.0f, 6.0f), juce::Justification::topRight);
            }
        }

        void mouseMove (const juce::MouseEvent& e) override
        {
            const int b = bandAt (e.position);
            if (b != hoverBand)
            {
                hoverBand = b;
                setMouseCursor (b >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::CrosshairCursor);
                repaint();
            }
        }

        void mouseExit (const juce::MouseEvent&) override { hoverBand = -1; repaint(); }

        void mouseDown (const juce::MouseEvent& e) override
        {
            const int b = bandAt (e.position);
            if (b < 0) return;
            select (b);

            if (e.mods.isPopupMenu())
                return showBandMenu (b);

            dragBand = b;
            dragStart = e.position;
            startFreq = eq.get (EqParams::bands[b].freqId);
            startGain = EqParams::bands[b].gainId != nullptr ? eq.get (EqParams::bands[b].gainId) : 0.0f;
            eq.begin (EqParams::bands[b].freqId);
            eq.begin (EqParams::bands[b].gainId);
            // A cut that is switched off starts moving from its end of the spectrum.
            if (b == 0 && ! eq.isActive (0)) startFreq = 20.6f;
            if (b == 5 && ! eq.isActive (5)) startFreq = 19800.0f;
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (dragBand < 0) return;
            const auto r = plotArea();
            const float fine = e.mods.isShiftDown() ? 0.2f : 1.0f;
            const auto& band = EqParams::bands[dragBand];

            // Frequency: horizontal movement on the log axis
            const float dx = (e.position.x - dragStart.x) * fine / r.getWidth();
            const float x = std::clamp (xNormFor (startFreq) + dx, 0.0f, 1.0f);
            eq.set (band.freqId, freqFor (x));

            if (band.gainId != nullptr)
            {
                const float dy = (e.position.y - dragStart.y) * fine;
                const float db = startGain - dy / (r.getHeight() * 0.5f) * displayRange;
                eq.set (band.gainId, std::clamp (std::abs (db) < 0.3f && fine == 1.0f ? 0.0f : db, -18.0f, 18.0f));
            }
            repaint();
        }

        void mouseUp (const juce::MouseEvent&) override
        {
            if (dragBand < 0) return;
            eq.end (EqParams::bands[dragBand].freqId);
            eq.end (EqParams::bands[dragBand].gainId);
            dragBand = -1;
            repaint();
        }

        void mouseDoubleClick (const juce::MouseEvent& e) override
        {
            const int b = bandAt (e.position);
            if (b >= 0)
            {
                eq.resetBand (b);
                return;
            }

            // Empty space: put a free peak band (or the nearer one) where the user clicked
            const float freq = freqFor ((e.position.x - plotArea().getX()) / plotArea().getWidth());
            const float db = std::clamp ((plotArea().getCentreY() - e.position.y) / (plotArea().getHeight() * 0.5f) * displayRange, -18.0f, 18.0f);
            int target = ! eq.isActive (2) ? 2 : (! eq.isActive (3) ? 3 : -1);
            if (target < 0)
                target = std::abs (std::log (freq / eq.get ("midfreq"))) < std::abs (std::log (freq / eq.get ("mid2freq"))) ? 2 : 3;
            eq.setAsGesture (EqParams::bands[target].freqId, freq);
            eq.setAsGesture (EqParams::bands[target].gainId, std::abs (db) < 1.0f ? 3.0f : db);
            if (! eq.isOn())
                eq.setAsGesture ("on", 1.0f);
            select (target);
        }

        void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
        {
            int b = bandAt (e.position);
            if (b < 0) b = selected;
            const auto& band = EqParams::bands[b];
            const float delta = (w.deltaY != 0.0f ? w.deltaY : w.deltaX) * (w.isReversed ? -1.0f : 1.0f);
            if (std::abs (delta) < 1.0e-4f) return;

            if (band.qId != nullptr)
                eq.setAsGesture (band.qId, std::clamp (eq.get (band.qId) * std::pow (2.0f, delta * 1.5f), 0.2f, 12.0f));
            else if (band.slopeId != nullptr)
                eq.setAsGesture (band.slopeId, delta > 0.0f ? 1.0f : 0.0f);
            else if (band.gainId != nullptr)
                eq.setAsGesture (band.gainId, std::clamp (eq.get (band.gainId) + delta * 6.0f, -18.0f, 18.0f));
            select (b);
            repaint();
        }

    private:
        static constexpr float displayRange = 18.0f;   // ±dB shown

        void select (int b)
        {
            selectBand (b);
            if (onBandSelected) onBandSelected (b);
        }

        juce::Rectangle<float> plotArea() const { return getLocalBounds().toFloat().reduced (10.0f, 12.0f); }

        static float freqFor (float x) { return 20.0f * std::pow (1000.0f, x); }
        static float xNormFor (float f) { return std::log (std::max (1.0f, f) / 20.0f) / std::log (1000.0f); }
        float xFor (float f) const { return plotArea().getX() + std::clamp (xNormFor (f), 0.0f, 1.0f) * plotArea().getWidth(); }
        float yFor (float db) const
        {
            const auto r = plotArea();
            return r.getCentreY() - std::clamp (db / displayRange, -1.0f, 1.0f) * r.getHeight() * 0.5f;
        }

        juce::Path curvePath (const fx::EqDesign& d, int band) const
        {
            const auto r = plotArea();
            juce::Path path;
            const int steps = (int) r.getWidth() / 2;
            for (int i = 0; i <= steps; ++i)
            {
                const float x = (float) i / (float) steps;
                const float y = yFor ((float) d.magnitudeDb (freqFor (x), sampleRate, band));
                if (i == 0) path.startNewSubPath (r.getX(), y); else path.lineTo (r.getX() + x * r.getWidth(), y);
            }
            return path;
        }

        juce::Point<float> nodePosition (int b) const
        {
            const auto& band = EqParams::bands[b];
            float freq = eq.get (band.freqId);
            if (b == 0 && ! eq.isActive (0)) freq = 24.0f;      // keep switched-off cuts reachable at the edges
            if (b == 5 && ! eq.isActive (5)) freq = 17000.0f;
            const float db = band.gainId != nullptr ? eq.get (band.gainId) : 0.0f;
            return { xFor (freq), yFor (db) };
        }

        int bandAt (juce::Point<float> pos) const
        {
            int best = -1;
            float bestDist = 14.0f;
            for (int b = 0; b < fx::kNumEqBands; ++b)
            {
                const float d = nodePosition (b).getDistanceFrom (pos);
                if (d < bestDist) { bestDist = d; best = b; }
            }
            return best;
        }

        static juce::String hz (float f)
        {
            return f < 1000.0f ? juce::String (juce::roundToInt (f)) + " Hz" : juce::String (f / 1000.0f, 2) + " kHz";
        }

        juce::String describe (int b) const
        {
            const auto& band = EqParams::bands[b];
            if (! eq.isActive (b) && band.gainId == nullptr)
                return juce::String (band.name) + utf8 ("  ·  off  ·  drag to use");
            auto s = juce::String (band.name) + utf8 ("  ·  ") + hz (eq.get (band.freqId));
            if (band.gainId != nullptr)
            {
                const float db = eq.get (band.gainId);
                s << utf8 ("  ·  ") << (db > 0.0f ? "+" : "") << juce::String (db, 1) << " dB";
            }
            if (band.qId != nullptr)
                s << utf8 ("  ·  Q ") << juce::String (eq.get (band.qId), 2);
            if (band.slopeId != nullptr)
                s << utf8 ("  ·  ") << (juce::roundToInt (eq.get (band.slopeId)) == 1 ? "24" : "12") << " dB/oct";
            return s;
        }

        void showBandMenu (int b)
        {
            const auto& band = EqParams::bands[b];
            juce::PopupMenu m;
            m.addSectionHeader (band.name);
            m.addItem (1, "Reset band");
            if (band.slopeId != nullptr)
            {
                const int slope = juce::roundToInt (eq.get (band.slopeId));
                m.addSeparator();
                m.addItem (10, "12 dB / octave", true, slope == 0);
                m.addItem (11, "24 dB / octave", true, slope == 1);
            }
            if (band.qId != nullptr)
            {
                m.addSeparator();
                m.addItem (20, "Wide (Q 0.5)");
                m.addItem (21, "Medium (Q 1.0)");
                m.addItem (22, "Narrow (Q 3.0)");
                m.addItem (23, "Surgical (Q 8.0)");
            }
            if (band.gainId != nullptr)
            {
                m.addSeparator();
                m.addItem (30, "Invert gain");
            }
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(), [this, b] (int r)
            {
                const auto& bd = EqParams::bands[b];
                if (r == 1) eq.resetBand (b);
                if (r == 10 || r == 11) eq.setAsGesture (bd.slopeId, (float) (r - 10));
                if (r >= 20 && r <= 23) eq.setAsGesture (bd.qId, std::array<float, 4> { 0.5f, 1.0f, 3.0f, 8.0f }[(size_t) (r - 20)]);
                if (r == 30) eq.setAsGesture (bd.gainId, -eq.get (bd.gainId));
                repaint();
            });
        }

        double scopeRate() const { return scope != nullptr ? std::max (8000.0, scope->sampleRate.load()) : 48000.0; }

        // --- Spectrum (Hann window, 2048-point FFT of the output, smoothed falloff)
        float spectrumAt (float freq) const
        {
            const float bin = freq / (float) scopeRate() * (float) fftSize;
            const int i = std::clamp ((int) bin, 1, fftSize / 2 - 2);
            const float t = std::clamp (bin - (float) i, 0.0f, 1.0f);
            return spectrum[(size_t) i] * (1.0f - t) + spectrum[(size_t) i + 1] * t;
        }

        void updateSpectrum()
        {
            const int w = scope->writePos.load (std::memory_order_acquire);
            if (w == lastWrite)
            {
                // No new audio: let the spectrum fall back
                bool any = false;
                for (auto& s : spectrum) { s = std::max (-120.0f, s - 2.0f); any |= s > -100.0f; }
                spectrumVisible = any;
                return;
            }
            lastWrite = w;
            std::fill (fftData.begin(), fftData.end(), 0.0f);
            for (int i = 0; i < fftSize; ++i)
                fftData[(size_t) i] = scope->data[(size_t) ((w - fftSize + i) & (ScopeBuffer::size - 1))];
            window.multiplyWithWindowingTable (fftData.data(), (size_t) fftSize);
            fft.performFrequencyOnlyForwardTransform (fftData.data());

            bool any = false;
            for (int i = 0; i < fftSize / 2; ++i)
            {
                const float mag = fftData[(size_t) i] / (float) fftSize * 4.0f;
                // +3 dB per octave tilt so the spectrum reads like what we hear
                const float tilt = 3.0f * std::log2 (std::max (1.0f, (float) i) * (float) scopeRate() / (float) fftSize / 1000.0f);
                const float db = juce::Decibels::gainToDecibels (mag, -120.0f) + tilt;
                auto& s = spectrum[(size_t) i];
                s = db > s ? s + (db - s) * 0.6f : std::max (db, s - 1.5f);
                any |= s > -100.0f;
            }
            spectrumVisible = any;
        }

        void timerCallback() override
        {
            if (! isShowing()) return;
            if (scope != nullptr) updateSpectrum();

            juce::String f;
            const auto e = eq.settings();
            for (float v : { e.lowCut, e.lowFreq, e.low, e.midFreq, e.mid, e.midQ, e.mid2Freq, e.mid2, e.mid2Q, e.highFreq, e.high, e.highCut })
                f << juce::String (v, 2) << ":";
            f << e.lowCutSlope << e.highCutSlope << (eq.isOn() ? "1" : "0") << (Theme::get().isDark() ? "d" : "l");
            if (f != fingerprint || spectrumVisible)
            {
                fingerprint = f;
                repaint();
            }
        }

        EqParams& eq;
        ScopeBuffer* scope;
        double sampleRate = 48000.0;
        int selected = 2, hoverBand = -1, dragBand = -1;
        juce::Point<float> dragStart;
        float startFreq = 1000.0f, startGain = 0.0f;
        juce::String fingerprint, hint { utf8 ("Drag the points  ·  wheel: Q  ·  double-click: add / reset  ·  right-click: menu") };

        static constexpr int fftOrder = 11, fftSize = 1 << fftOrder;
        juce::dsp::FFT fft { fftOrder };
        juce::dsp::WindowingFunction<float> window { (size_t) fftSize, juce::dsp::WindowingFunction<float>::hann };
        std::array<float, fftSize * 2> fftData {};
        std::array<float, fftSize / 2> spectrum {};
        int lastWrite = -1;
        bool spectrumVisible = false;
    };

    // ---------------------------------------------------------------------------------------------
    // Band tabs below the graph: one tab per band, a dot shows whether the band is active.
    class EqBandTabs : public juce::Component, private juce::Timer
    {
    public:
        explicit EqBandTabs (EqParams& p) : eq (p) { startTimerHz (10); setMouseCursor (juce::MouseCursor::PointingHandCursor); }

        std::function<void (int)> onSelect;
        void setSelected (int b) { if (b != selected) { selected = b; repaint(); } }

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            for (int b = 0; b < fx::kNumEqBands; ++b)
            {
                auto cell = cellBounds (b);
                const bool sel = b == selected, active = eq.isActive (b);
                if (sel)
                {
                    g.setColour (p.accentSoft);
                    g.fillRoundedRectangle (cell, 4.0f);
                }
                auto dot = cell.removeFromLeft (22.0f).withSizeKeepingCentre (7.0f, 7.0f);
                g.setColour (active ? p.accent : p.line);
                if (active) g.fillEllipse (dot); else g.drawEllipse (dot, 1.2f);
                g.setColour (sel ? p.accent : (b == hovered ? p.text : p.textDim));
                drawSpacedText (g, juce::String (EqParams::bands[b].name).toUpperCase(), cell, 10.0f,
                                juce::Justification::centredLeft, 0.1f, sel);
            }
        }

        void mouseMove (const juce::MouseEvent& e) override { const int h = cellAt (e.position); if (h != hovered) { hovered = h; repaint(); } }
        void mouseExit (const juce::MouseEvent&) override { hovered = -1; repaint(); }
        void mouseDown (const juce::MouseEvent& e) override
        {
            const int b = cellAt (e.position);
            if (b >= 0) { setSelected (b); if (onSelect) onSelect (b); }
        }

    private:
        juce::Rectangle<float> cellBounds (int b) const
        {
            const float w = (float) getWidth() / (float) fx::kNumEqBands;
            return { (float) b * w + 2.0f, 0.0f, w - 4.0f, (float) getHeight() };
        }

        int cellAt (juce::Point<float> pos) const
        {
            for (int b = 0; b < fx::kNumEqBands; ++b)
                if (cellBounds (b).contains (pos)) return b;
            return -1;
        }

        void timerCallback() override
        {
            int mask = 0;
            for (int b = 0; b < fx::kNumEqBands; ++b) mask |= eq.isActive (b) ? (1 << b) : 0;
            if (mask != shownMask) { shownMask = mask; repaint(); }
        }

        EqParams& eq;
        int selected = 2, hovered = -1, shownMask = -1;
    };

    // ---------------------------------------------------------------------------------------------
    // Complete EQ editor: graph, band tabs and the knobs of the selected band.
    class EqEditor : public juce::Component
    {
    public:
        EqEditor (APVTS& s, ScopeBuffer* scopeBuffer) : eq (s), graph (eq, scopeBuffer), tabs (eq)
        {
            addAndMakeVisible (graph);
            addAndMakeVisible (tabs);
            for (int b = 0; b < fx::kNumEqBands; ++b)
            {
                const auto& band = EqParams::bands[b];
                auto& c = controls[(size_t) b];
                c.freq = std::make_unique<Knob> (s, params::fx (2, band.freqId), b == 0 || b == 5 ? "Cutoff" : "Freq");
                if (band.gainId != nullptr) c.gain = std::make_unique<Knob> (s, params::fx (2, band.gainId), "Gain");
                if (band.qId != nullptr)    c.q = std::make_unique<Knob> (s, params::fx (2, band.qId), "Q");
                if (band.slopeId != nullptr)
                    c.slope = std::make_unique<Segmented> (s, params::fx (2, band.slopeId), nullptr,
                                                           juce::StringArray { "12 dB/oct", "24 dB/oct" });
                for (auto* comp : c.all())
                    addChildComponent (comp);
            }

            reset = std::make_unique<PillButton> ("Reset band", false);
            reset->onClick = [this] { eq.resetBand (graph.getSelectedBand()); };
            addAndMakeVisible (*reset);

            graph.onBandSelected = [this] (int b) { tabs.setSelected (b); showBand (b); };
            tabs.onSelect = [this] (int b) { graph.selectBand (b); showBand (b); };
            showBand (2);
        }

        void resized() override
        {
            auto r = getLocalBounds();
            auto bottom = r.removeFromBottom (128);
            graph.setBounds (r.withTrimmedBottom (8));
            tabs.setBounds (bottom.removeFromTop (28));
            bottom.removeFromTop (6);
            reset->setBounds (bottom.removeFromRight (130).withSizeKeepingCentre (120, 28));

            auto row = bottom.withTrimmedRight (10);
            for (auto& c : controls)
            {
                auto line = row;
                const int w = 110;
                if (c.slope != nullptr)
                    c.slope->setBounds (line.removeFromRight (std::min (260, line.getWidth() / 2)).withSizeKeepingCentre (240, 26));
                c.freq->setBounds (line.removeFromLeft (w).reduced (10, 0));
                if (c.gain != nullptr) c.gain->setBounds (line.removeFromLeft (w).reduced (10, 0));
                if (c.q != nullptr)    c.q->setBounds (line.removeFromLeft (w).reduced (10, 0));
            }
        }

    private:
        struct BandControls
        {
            std::unique_ptr<Knob> freq, gain, q;
            std::unique_ptr<Segmented> slope;

            std::vector<juce::Component*> all() const
            {
                std::vector<juce::Component*> v;
                for (juce::Component* c : { (juce::Component*) freq.get(), (juce::Component*) gain.get(),
                                            (juce::Component*) q.get(), (juce::Component*) slope.get() })
                    if (c != nullptr) v.push_back (c);
                return v;
            }
        };

        void showBand (int b)
        {
            for (int i = 0; i < fx::kNumEqBands; ++i)
                for (auto* c : controls[(size_t) i].all())
                    c->setVisible (i == b);
        }

        EqParams eq;
        EqGraph graph;
        EqBandTabs tabs;
        std::array<BandControls, fx::kNumEqBands> controls;
        std::unique_ptr<PillButton> reset;
    };
}
