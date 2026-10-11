#pragma once
#include "Controls.h"
#include "../Parameters.h"
#include "../fx/Effects.h"

namespace fjord::ui
{
    // Compressor page: a transfer curve you can grab, a scrolling picture of what the compressor does,
    // meters and one-click characters. All of it drives the compressor of the effect chain.
    namespace comp
    {
        constexpr int kType = (int) fx::FxType::Comp;
        inline juce::String id (const char* name) { return params::fx (kType, name); }

        // Sets a parameter (plain value) as one complete host gesture.
        inline void setPlain (APVTS& s, const juce::String& paramId, float value)
        {
            if (auto* p = s.getParameter (paramId))
            {
                p->beginChangeGesture();
                p->setValueNotifyingHost (p->convertTo0to1 (value));
                p->endChangeGesture();
            }
        }

        inline float toDb (float linear) { return juce::Decibels::gainToDecibels (linear, -96.0f); }

        // Live values of the compressor parameters
        struct Values
        {
            explicit Values (APVTS& s)
                : on (s.getRawParameterValue (id ("on"))), thresh (s.getRawParameterValue (id ("thresh"))),
                  ratio (s.getRawParameterValue (id ("ratio"))), knee (s.getRawParameterValue (id ("knee"))),
                  makeup (s.getRawParameterValue (id ("makeup"))), autoGain (s.getRawParameterValue (id ("autogain"))),
                  mix (s.getRawParameterValue (id ("mix"))) {}

            bool isOn() const        { return on->load() > 0.5f; }
            float threshold() const  { return thresh->load(); }
            float ratioValue() const { return ratio->load(); }
            float kneeDb() const     { return knee->load(); }
            float makeupDb() const
            {
                return makeup->load() + (autoGain->load() > 0.5f ? fx::Comp::autoMakeup (threshold(), ratioValue()) : 0.0f);
            }
            // static output level for an input level (dB), including makeup and the parallel mix
            float output (float inDb) const
            {
                const float wet = inDb - fx::Comp::reduction (inDb, threshold(), ratioValue(), kneeDb()) + makeupDb();
                const float m = mix->load();
                return toDb ((1.0f - m) * juce::Decibels::decibelsToGain (inDb) + m * juce::Decibels::decibelsToGain (wet));
            }

            std::atomic<float> *on, *thresh, *ratio, *knee, *makeup, *autoGain, *mix;
        };

        // Meter sources from the processor (linear peaks and reduction in dB)
        struct Meters
        {
            std::function<float()> input, reduction, output;
        };
    }

    // --------------------------------------------------------------------------------------------
    // Transfer curve: input level → output level. Drag the dot to move the threshold,
    // drag the end of the curve (or anywhere above the threshold) to set the ratio.
    class TransferCurve : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
    {
    public:
        TransferCurve (APVTS& s, comp::Meters m) : state (s), values (s), meters (std::move (m))
        {
            setTooltip ("Drag the dot: threshold.  Drag the curve above it: ratio.  Wheel: ratio.  Double-click: reset");
            startTimerHz (30);
        }

        void tick() { timerCallback(); }   // offscreen screenshots (no timer runs there)

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            const auto r = plotArea();
            const bool on = values.isOn();

            // grid every 12 dB
            g.setFont (font (9.5f));
            for (int db = -48; db <= 0; db += 12)
            {
                g.setColour (p.line.withAlpha (db == 0 ? 1.0f : 0.55f));
                g.fillRect (dbToX (db), r.getY(), 1.0f, r.getHeight());
                g.fillRect (r.getX(), dbToY (db), r.getWidth(), 1.0f);
                g.setColour (p.textDim.withAlpha (0.8f));
                g.drawText (juce::String (db), juce::Rectangle<float> (dbToX (db) - 16.0f, r.getBottom() + 3.0f, 32.0f, 12.0f),
                            juce::Justification::centred);
                if (db > lo)
                    g.drawText (juce::String (db), juce::Rectangle<float> (r.getX() - 30.0f, dbToY (db) - 6.0f, 25.0f, 12.0f),
                                juce::Justification::centredRight);
            }

            // unity line
            juce::Path unity;
            unity.startNewSubPath (dbToX (lo), dbToY (lo));
            unity.lineTo (dbToX (hi), dbToY (hi));
            const float dashes[] = { 3.0f, 4.0f };
            juce::Path dashed;
            juce::PathStrokeType (1.0f).createDashedStroke (dashed, unity, dashes, 2);
            g.setColour (p.textDim.withAlpha (0.5f));
            g.fillPath (dashed);

            // the curve and the area it takes away (reduction)
            const auto colour = on ? p.accent : p.textDim;
            juce::Path curve, cut;
            const int steps = 160;
            for (int i = 0; i <= steps; ++i)
            {
                const float in = lo + (hi - lo) * (float) i / (float) steps;
                const float x = dbToX (in), y = dbToY (std::clamp (values.output (in), lo - 6.0f, hi + 6.0f));
                if (i == 0) curve.startNewSubPath (x, y); else curve.lineTo (x, y);
            }
            cut = curve;
            cut.lineTo (dbToX (hi), dbToY (hi));
            cut.lineTo (dbToX (lo), dbToY (lo));
            cut.closeSubPath();
            {
                juce::Graphics::ScopedSaveState clip (g);
                g.reduceClipRegion (r.toNearestInt());
                g.setGradientFill (juce::ColourGradient (colour.withAlpha (0.20f), r.getRight(), r.getY(),
                                                         colour.withAlpha (0.02f), dbToX (values.threshold()), r.getBottom(), false));
                g.fillPath (cut);
                g.setColour (colour);
                g.strokePath (curve, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }

            // threshold hairline + handle
            const float thr = values.threshold();
            const float tx = dbToX (thr);
            g.setColour (colour.withAlpha (0.35f));
            g.fillRect (tx, r.getY(), 1.0f, r.getHeight());
            drawHandle (g, thresholdHandle(), drag == Drag::Threshold || hover == Drag::Threshold, colour);
            drawHandle (g, ratioHandle(), drag == Drag::Ratio || hover == Drag::Ratio, colour, 4.5f);

            // live input level riding on the curve
            if (on && level > lo + 1.0f)
            {
                const juce::Point<float> dot { dbToX (level), dbToY (std::clamp (values.output (level), lo, hi + 6.0f)) };
                g.setColour (colour.withAlpha (0.15f));
                g.fillEllipse (juce::Rectangle<float> (26.0f, 26.0f).withCentre (dot));
                g.setColour (colour.withAlpha (0.35f));
                g.fillEllipse (juce::Rectangle<float> (14.0f, 14.0f).withCentre (dot));
                g.setColour (colour);
                g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre (dot));
            }

            // readouts
            auto info = r.reduced (10.0f, 8.0f);
            g.setColour (p.text);
            drawSpacedText (g, juce::String (thr, 1) + " dB", info.removeFromTop (16.0f), 12.5f, juce::Justification::topLeft, 0.02f, true);
            g.setColour (p.textDim);
            drawSpacedText (g, "THRESHOLD", info.removeFromTop (13.0f), 9.0f, juce::Justification::topLeft, 0.2f);
            info.removeFromTop (6.0f);
            g.setColour (p.text);
            const float ratio = values.ratioValue();
            drawSpacedText (g, (ratio >= 19.95f ? juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x9e")) : juce::String (ratio, 1)) + " : 1",
                            info.removeFromTop (16.0f), 12.5f, juce::Justification::topLeft, 0.02f, true);
            g.setColour (p.textDim);
            drawSpacedText (g, "RATIO", info.removeFromTop (13.0f), 9.0f, juce::Justification::topLeft, 0.2f);

            g.setColour (p.textDim);
            drawSpacedText (g, "INPUT", juce::Rectangle<float> (r.getX(), r.getBottom() + 14.0f, r.getWidth(), 12.0f), 9.0f,
                            juce::Justification::centred, 0.2f);
        }

        void mouseMove (const juce::MouseEvent& e) override
        {
            const auto h = hitTestDrag (e.position);
            if (h != hover) { hover = h; repaint(); }
            setMouseCursor (h == Drag::Threshold ? juce::MouseCursor::LeftRightResizeCursor
                          : h == Drag::Ratio ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor);
        }

        void mouseExit (const juce::MouseEvent&) override { hover = Drag::None; repaint(); }

        void mouseDown (const juce::MouseEvent& e) override
        {
            drag = hitTestDrag (e.position);
            if (drag == Drag::None)
                drag = e.position.x < dbToX (values.threshold()) ? Drag::Threshold : Drag::Ratio;
            auto* param = state.getParameter (comp::id (drag == Drag::Threshold ? "thresh" : "ratio"));
            param->beginChangeGesture();
            mouseDrag (e);
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (drag == Drag::Threshold)
            {
                set ("thresh", std::clamp (xToDb (e.position.x), -48.0f, 0.0f));
            }
            else if (drag == Drag::Ratio)
            {
                // the curve's end at 0 dB input follows the mouse height
                const float thr = values.threshold();
                const float out = yToDb (e.position.y) - values.makeupDb();
                const float ratio = out <= thr + 0.05f ? 20.0f : -thr / std::max (0.05f, out - thr);
                set ("ratio", std::clamp (ratio, 1.0f, 20.0f));
            }
        }

        void mouseUp (const juce::MouseEvent&) override
        {
            if (drag != Drag::None)
                state.getParameter (comp::id (drag == Drag::Threshold ? "thresh" : "ratio"))->endChangeGesture();
            drag = Drag::None;
            repaint();
        }

        void mouseDoubleClick (const juce::MouseEvent&) override
        {
            for (auto* name : { "thresh", "ratio" })
            {
                auto* p = state.getParameter (comp::id (name));
                p->beginChangeGesture();
                p->setValueNotifyingHost (p->getDefaultValue());
                p->endChangeGesture();
            }
        }

        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
        {
            const float ratio = std::clamp (values.ratioValue() * std::exp2 (w.deltaY * (w.isReversed ? -1.5f : 1.5f)), 1.0f, 20.0f);
            comp::setPlain (state, comp::id ("ratio"), ratio);
        }

    private:
        enum class Drag { None, Threshold, Ratio };
        static constexpr float lo = -60.0f, hi = 0.0f;

        juce::Rectangle<float> plotArea() const
        {
            auto r = getLocalBounds().toFloat().withTrimmedLeft (32.0f).withTrimmedBottom (30.0f).withTrimmedTop (6.0f).withTrimmedRight (8.0f);
            const float side = std::min (r.getWidth(), r.getHeight());
            return r.withSizeKeepingCentre (side, side).withY (r.getY());
        }

        float dbToX (float db) const { const auto r = plotArea(); return r.getX() + (db - lo) / (hi - lo) * r.getWidth(); }
        float dbToY (float db) const { const auto r = plotArea(); return r.getBottom() - (db - lo) / (hi - lo) * r.getHeight(); }
        float xToDb (float x) const  { const auto r = plotArea(); return lo + (x - r.getX()) / r.getWidth() * (hi - lo); }
        float yToDb (float y) const  { const auto r = plotArea(); return lo + (r.getBottom() - y) / r.getHeight() * (hi - lo); }

        juce::Point<float> thresholdHandle() const
        {
            const float thr = values.threshold();
            return { dbToX (thr), dbToY (values.output (thr)) };
        }

        juce::Point<float> ratioHandle() const { return { dbToX (hi), dbToY (std::min (values.output (hi), hi + 6.0f)) }; }

        Drag hitTestDrag (juce::Point<float> pos) const
        {
            if (pos.getDistanceFrom (thresholdHandle()) < 12.0f) return Drag::Threshold;
            if (pos.getDistanceFrom (ratioHandle()) < 12.0f) return Drag::Ratio;
            return Drag::None;
        }

        static void drawHandle (juce::Graphics& g, juce::Point<float> c, bool active, juce::Colour colour, float radius = 6.0f)
        {
            const auto& p = Theme::get().palette();
            const float rad = active ? radius + 2.0f : radius;
            if (active)
            {
                g.setColour (colour.withAlpha (0.18f));
                g.fillEllipse (juce::Rectangle<float> (rad * 4.0f, rad * 4.0f).withCentre (c));
            }
            g.setColour (p.panel);
            g.fillEllipse (juce::Rectangle<float> (rad * 2.0f, rad * 2.0f).withCentre (c));
            g.setColour (colour);
            g.drawEllipse (juce::Rectangle<float> (rad * 2.0f, rad * 2.0f).withCentre (c), 2.0f);
        }

        void set (const char* name, float plain)
        {
            auto* p = state.getParameter (comp::id (name));
            p->setValueNotifyingHost (p->convertTo0to1 (plain));
        }

        void timerCallback() override
        {
            const float now = meters.input ? comp::toDb (meters.input()) : -96.0f;
            level = now > level ? now : level - 1.2f;   // quick rise, gentle fall
            repaint();
        }

        APVTS& state;
        comp::Values values;
        comp::Meters meters;
        float level = -96.0f;
        Drag drag = Drag::None, hover = Drag::None;
    };

    // --------------------------------------------------------------------------------------------
    // Scrolling picture of the last seconds: input level, output level and the reduction hanging from the top.
    class DynamicsHistory : public juce::Component, private juce::Timer
    {
    public:
        DynamicsHistory (APVTS& s, comp::Meters m) : values (s), meters (std::move (m))
        {
            inputs.fill (-96.0f);
            outputs.fill (-96.0f);
            startTimerHz (30);
        }

        void tick() { timerCallback(); }

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            auto r = getLocalBounds().toFloat();
            auto legend = r.removeFromTop (20.0f);
            r.removeFromTop (4.0f);
            const bool on = values.isOn();

            g.setColour (p.line.withAlpha (0.5f));
            for (int db = -12; db >= -48; db -= 12)
                g.fillRect (r.getX(), levelY (r, (float) db), r.getWidth(), 1.0f);
            g.setColour (p.line);
            g.drawRoundedRectangle (r, 4.0f, 1.0f);

            auto x = [&] (int i) { return r.getX() + r.getWidth() * (float) i / (float) (size - 1); };
            auto at = [&] (const std::array<float, size>& a, int i) { return a[(size_t) ((write + i) % size)]; };

            // input: soft filled area
            juce::Path in, out, gr;
            in.startNewSubPath (r.getX(), r.getBottom());
            for (int i = 0; i < size; ++i) in.lineTo (x (i), levelY (r, at (inputs, i)));
            in.lineTo (r.getRight(), r.getBottom());
            in.closeSubPath();
            g.setColour (p.textDim.withAlpha (0.16f));
            g.fillPath (in);

            // output: line
            for (int i = 0; i < size; ++i)
            {
                const float y = levelY (r, at (outputs, i));
                if (i == 0) out.startNewSubPath (x (i), y); else out.lineTo (x (i), y);
            }
            const auto colour = on ? p.accent : p.textDim;
            g.setColour (colour.withAlpha (0.9f));
            g.strokePath (out, juce::PathStrokeType (1.5f));

            // reduction: from the top down (24 dB = 45 % of the height)
            gr.startNewSubPath (r.getX(), r.getY());
            for (int i = 0; i < size; ++i) gr.lineTo (x (i), r.getY() + std::clamp (at (reductions, i) / 24.0f, 0.0f, 1.0f) * r.getHeight() * 0.45f);
            gr.lineTo (r.getRight(), r.getY());
            gr.closeSubPath();
            g.setGradientFill (juce::ColourGradient (colour.withAlpha (0.45f), 0.0f, r.getY(), colour.withAlpha (0.10f), 0.0f,
                                                     r.getY() + r.getHeight() * 0.45f, false));
            g.fillPath (gr);

            // threshold
            const float ty = levelY (r, values.threshold());
            juce::Path line;
            line.startNewSubPath (r.getX(), ty);
            line.lineTo (r.getRight(), ty);
            juce::Path dashed;
            const float dashes[] = { 4.0f, 4.0f };
            juce::PathStrokeType (1.0f).createDashedStroke (dashed, line, dashes, 2);
            g.setColour (colour.withAlpha (0.6f));
            g.fillPath (dashed);
            g.setColour (colour);
            drawSpacedText (g, "THRESHOLD", juce::Rectangle<float> (r.getRight() - 90.0f, ty - 15.0f, 84.0f, 13.0f), 8.5f,
                            juce::Justification::centredRight, 0.2f);

            // legend
            auto item = [&] (const juce::String& text, juce::Colour c, bool filled)
            {
                auto box = legend.removeFromLeft (12.0f).withSizeKeepingCentre (10.0f, filled ? 8.0f : 2.0f);
                g.setColour (c);
                g.fillRoundedRectangle (box, 1.0f);
                legend.removeFromLeft (6.0f);
                g.setColour (p.textDim);
                const float w = (float) text.length() * 7.5f + 10.0f;
                drawSpacedText (g, text, legend.removeFromLeft (w), 9.0f, juce::Justification::centredLeft, 0.2f);
                legend.removeFromLeft (14.0f);
            };
            item ("INPUT", p.textDim.withAlpha (0.4f), true);
            item ("OUTPUT", colour, false);
            item ("REDUCTION", colour.withAlpha (0.45f), true);

            if (! on)
            {
                g.setColour (p.textDim);
                drawSpacedText (g, "COMPRESSOR OFF  -  PICK A CHARACTER OR SWITCH IT ON", r, 10.0f, juce::Justification::centred, 0.18f);
            }
        }

    private:
        static constexpr int size = 180;   // 6 seconds at 30 Hz

        static float levelY (juce::Rectangle<float> r, float db)
        {
            return r.getBottom() - std::clamp ((db + 54.0f) / 54.0f, 0.0f, 1.0f) * r.getHeight();
        }

        void timerCallback() override
        {
            inputs[(size_t) write] = meters.input ? comp::toDb (meters.input()) : -96.0f;
            outputs[(size_t) write] = meters.output ? comp::toDb (meters.output()) : -96.0f;
            reductions[(size_t) write] = meters.reduction ? meters.reduction() : 0.0f;
            write = (write + 1) % size;
            repaint();
        }

        comp::Values values;
        comp::Meters meters;
        std::array<float, size> inputs {}, outputs {}, reductions {};
        int write = 0;
    };

    // --------------------------------------------------------------------------------------------
    // IN / GR / OUT bars with peak hold
    class CompMeters : public juce::Component, private juce::Timer
    {
    public:
        explicit CompMeters (comp::Meters m) : meters (std::move (m)) { startTimerHz (30); }

        void tick() { timerCallback(); }

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            auto r = getLocalBounds().toFloat();
            const float w = r.getWidth() / 3.0f;
            drawBar (g, r.removeFromLeft (w), "IN", in, inHold, false, p.textDim);
            drawBar (g, r.removeFromLeft (w), "GR", gr, grHold, true, p.accent);
            drawBar (g, r, "OUT", out, outHold, false, p.accent);
        }

    private:
        static void drawBar (juce::Graphics& g, juce::Rectangle<float> area, const char* label, float value, float hold,
                             bool reduction, juce::Colour colour)
        {
            const auto& p = Theme::get().palette();
            auto valueArea = area.removeFromTop (18.0f);
            auto labelArea = area.removeFromBottom (18.0f);
            auto bar = area.withSizeKeepingCentre (10.0f, area.getHeight() - 8.0f);
            g.setColour (p.track);
            g.fillRoundedRectangle (bar, 5.0f);

            const float frac = reduction ? std::clamp (value / 24.0f, 0.0f, 1.0f) : std::clamp ((value + 54.0f) / 54.0f, 0.0f, 1.0f);
            const float holdFrac = reduction ? std::clamp (hold / 24.0f, 0.0f, 1.0f) : std::clamp ((hold + 54.0f) / 54.0f, 0.0f, 1.0f);
            g.setColour (colour);
            if (reduction)
            {
                g.fillRoundedRectangle (bar.withHeight (bar.getHeight() * frac), 5.0f);
                if (holdFrac > 0.005f)
                    g.fillRect (bar.getX(), bar.getY() + bar.getHeight() * holdFrac - 1.0f, bar.getWidth(), 2.0f);
            }
            else
            {
                g.fillRoundedRectangle (bar.withTop (bar.getBottom() - bar.getHeight() * frac), 5.0f);
                if (holdFrac > 0.0f)
                    g.fillRect (bar.getX(), bar.getBottom() - bar.getHeight() * holdFrac - 1.0f, bar.getWidth(), 2.0f);
            }

            g.setColour (p.text);
            const juce::String text = reduction ? (value > 0.05f ? "-" + juce::String (value, 1) : juce::String ("0.0"))
                                                : (value <= -60.0f ? juce::String (juce::CharPointer_UTF8 ("-\xe2\x88\x9e")) : juce::String (value, 1));
            g.setFont (font (11.0f));
            g.drawText (text, valueArea, juce::Justification::centred);
            g.setColour (p.textDim);
            drawSpacedText (g, label, labelArea, 9.5f, juce::Justification::centred, 0.2f);
        }

        void timerCallback() override
        {
            auto ballistic = [] (float& shown, float now, bool up) { shown = up ? now : shown + (now - shown) * 0.2f; };
            const float i = meters.input ? comp::toDb (meters.input()) : -96.0f;
            const float o = meters.output ? comp::toDb (meters.output()) : -96.0f;
            const float r = meters.reduction ? meters.reduction() : 0.0f;
            ballistic (in, i, i > in);
            ballistic (out, o, o > out);
            ballistic (gr, r, r > gr);
            auto holdIt = [] (float& h, int& age, float v, bool higher) { if (higher || ++age > 45) { h = v; age = 0; } };
            holdIt (inHold, inAge, in, in >= inHold);
            holdIt (outHold, outAge, out, out >= outHold);
            holdIt (grHold, grAge, gr, gr >= grHold);
            repaint();
        }

        comp::Meters meters;
        float in = -96.0f, out = -96.0f, gr = 0.0f, inHold = -96.0f, outHold = -96.0f, grHold = 0.0f;
        int inAge = 0, outAge = 0, grAge = 0;
    };

    // --------------------------------------------------------------------------------------------
    // One-click characters: each sets the whole compressor (and switches it on).
    class CompCharacters : public juce::Component, private juce::Timer
    {
    public:
        struct Style
        {
            const char* name;
            const char* blurb;
            float thresh, ratio, knee, attack, release, makeup, mix, schpf;
            bool autoGain;
        };

        static inline const Style styles[] = {
            { "Gentle",   "Evens out the level, barely there",     -16.0f,  1.8f, 18.0f, 0.020f, 0.30f, 0.0f, 1.00f,  20.0f, true },
            { "Glue",     "Bus style, binds layers together",      -20.0f,  2.5f, 10.0f, 0.012f, 0.18f, 0.0f, 1.00f,  60.0f, true },
            { "Punch",    "Slow attack lets transients through",   -22.0f,  4.0f,  4.0f, 0.035f, 0.12f, 0.0f, 1.00f,  20.0f, true },
            { "Squash",   "Flat and dense like an old radio",      -34.0f, 12.0f,  2.0f, 0.001f, 0.07f, 0.0f, 1.00f,  20.0f, true },
            { "Parallel", "Dense copy under the dry sound"     , -38.0f, 10.0f,  4.0f, 0.002f, 0.10f, 9.0f, 0.45f,  20.0f, false },
            { "Breathe",  "Long release, the sound swells back",   -30.0f,  6.0f,  6.0f, 0.004f, 0.45f, 0.0f, 1.00f, 120.0f, true },
        };
        static constexpr int numStyles = (int) std::size (styles);

        explicit CompCharacters (APVTS& s) : state (s)
        {
            for (auto* name : { "thresh", "ratio", "knee", "attack", "release", "makeup", "mix", "schpf", "autogain", "on" })
                refs.push_back (s.getRawParameterValue (comp::id (name)));
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
            startTimerHz (8);
        }

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            for (int i = 0; i < numStyles; ++i)
            {
                const auto& st = styles[i];
                const auto r = tile (i).toFloat();
                const bool on = i == active, over = i == hovered;
                g.setColour (on ? p.accentSoft : (over ? p.track.withAlpha (0.5f) : p.panel));
                g.fillRoundedRectangle (r, 6.0f);
                g.setColour (on ? p.accent : p.line);
                g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);

                auto content = r.reduced (12.0f, 8.0f);
                auto glyph = content.removeFromRight (content.getHeight()).reduced (2.0f);
                drawMiniCurve (g, glyph, st, on ? p.accent : p.textDim);
                content.removeFromRight (6.0f);
                g.setColour (on ? p.accent : p.text);
                drawSpacedText (g, juce::String (st.name).toUpperCase(), content.removeFromTop (content.getHeight() * 0.5f),
                                11.0f, juce::Justification::bottomLeft, 0.2f, true);
                g.setColour (p.textDim);
                g.setFont (font (11.0f));
                g.drawFittedText (st.blurb, content.toNearestInt().withTrimmedTop (3), juce::Justification::topLeft, 2, 0.9f);
            }
        }

        void mouseMove (const juce::MouseEvent& e) override
        {
            const int h = tileAt (e.getPosition());
            if (h != hovered) { hovered = h; repaint(); }
        }

        void mouseExit (const juce::MouseEvent&) override { hovered = -1; repaint(); }

        void mouseDown (const juce::MouseEvent& e) override
        {
            const int i = tileAt (e.getPosition());
            if (i < 0) return;
            const auto& st = styles[i];
            comp::setPlain (state, comp::id ("thresh"), st.thresh);
            comp::setPlain (state, comp::id ("ratio"), st.ratio);
            comp::setPlain (state, comp::id ("knee"), st.knee);
            comp::setPlain (state, comp::id ("attack"), st.attack);
            comp::setPlain (state, comp::id ("release"), st.release);
            comp::setPlain (state, comp::id ("makeup"), st.makeup);
            comp::setPlain (state, comp::id ("mix"), st.mix);
            comp::setPlain (state, comp::id ("schpf"), st.schpf);
            comp::setPlain (state, comp::id ("autogain"), st.autoGain ? 1.0f : 0.0f);
            comp::setPlain (state, comp::id ("on"), 1.0f);
            timerCallback();
        }

        void resized() override {}

    private:
        juce::Rectangle<int> tile (int i) const
        {
            constexpr int columns = 2, gap = 8;
            const int rows = (numStyles + columns - 1) / columns;
            const int w = (getWidth() - gap * (columns - 1)) / columns, h = (getHeight() - gap * (rows - 1)) / rows;
            return { (i % columns) * (w + gap), (i / columns) * (h + gap), w, h };
        }

        int tileAt (juce::Point<int> pos) const
        {
            for (int i = 0; i < numStyles; ++i)
                if (tile (i).contains (pos)) return i;
            return -1;
        }

        static void drawMiniCurve (juce::Graphics& g, juce::Rectangle<float> r, const Style& st, juce::Colour c)
        {
            juce::Path path;
            for (int i = 0; i <= 30; ++i)
            {
                const float in = -48.0f + 48.0f * (float) i / 30.0f;
                const float out = in - fx::Comp::reduction (in, st.thresh, st.ratio, st.knee) * st.mix;
                const auto pt = juce::Point<float> (r.getX() + r.getWidth() * (float) i / 30.0f,
                                                    r.getBottom() - std::clamp ((out + 48.0f) / 48.0f, 0.0f, 1.0f) * r.getHeight());
                if (i == 0) path.startNewSubPath (pt); else path.lineTo (pt);
            }
            g.setColour (Theme::get().palette().line);
            g.drawLine (r.getX(), r.getBottom(), r.getRight(), r.getY(), 1.0f);
            g.setColour (c);
            g.strokePath (path, juce::PathStrokeType (1.6f));
        }

        // The tile whose settings match the current compressor
        void timerCallback() override
        {
            int found = -1;
            if (refs[9]->load() > 0.5f)
                for (int i = 0; i < numStyles && found < 0; ++i)
                {
                    const auto& st = styles[i];
                    const float want[] = { st.thresh, st.ratio, st.knee, st.attack, st.release, st.makeup, st.mix, st.schpf, st.autoGain ? 1.0f : 0.0f };
                    bool same = true;
                    for (size_t k = 0; k < std::size (want); ++k)
                        same = same && std::abs (refs[k]->load() - want[k]) <= 0.01f * std::max (1.0f, std::abs (want[k]));
                    if (same) found = i;
                }
            if (found != active) { active = found; repaint(); }
        }

        APVTS& state;
        std::vector<std::atomic<float>*> refs;
        int active = -1, hovered = -1;
    };

    // --------------------------------------------------------------------------------------------
    class CompPage : public juce::Component
    {
    public:
        CompPage (APVTS& s, comp::Meters meters)
            : curveSection ("Compressor"), dynamicsSection ("Dynamics"), characterSection ("Character"), controlSection ("Controls"),
              curve (s, meters), history (s, meters), meterBars (meters), characters (s),
              power (s, comp::id ("on")), lock (s, locks::effectGroup (comp::kType), false),
              autoGain (s, comp::id ("autogain"), "Auto Gain"),
              thresh (s, comp::id ("thresh"), "Threshold"), ratio (s, comp::id ("ratio"), "Ratio"), knee (s, comp::id ("knee"), "Knee"),
              attack (s, comp::id ("attack"), "Attack"), release (s, comp::id ("release"), "Release"),
              makeup (s, comp::id ("makeup"), "Makeup"), mix (s, comp::id ("mix"), "Mix"), schpf (s, comp::id ("schpf"), "SC Filter")
        {
            for (auto* c : std::initializer_list<juce::Component*> { &curveSection, &dynamicsSection, &characterSection, &controlSection })
                addAndMakeVisible (c);
            curveSection.addAndMakeVisible (curve);
            curveSection.addAndMakeVisible (power);
            curveSection.addAndMakeVisible (lock);
            dynamicsSection.addAndMakeVisible (history);
            dynamicsSection.addAndMakeVisible (meterBars);
            characterSection.addAndMakeVisible (characters);
            for (auto* c : std::initializer_list<juce::Component*> { &autoGain, &thresh, &ratio, &knee, &attack, &release, &makeup, &mix, &schpf })
                controlSection.addAndMakeVisible (c);
            autoGain.setTooltip ("Auto Gain: makes up for the level the compressor takes away");
            schpf.setTooltipText ("Sidechain filter: the compressor ignores the lows below this frequency (no bass pumping)");
        }

        void resized() override
        {
            constexpr int gap = 10;
            auto r = getLocalBounds();
            auto top = r.removeFromTop ((r.getHeight() - gap) * 58 / 100);
            r.removeFromTop (gap);

            curveSection.setBounds (top.removeFromLeft (top.getHeight() - 10));
            top.removeFromLeft (gap);
            dynamicsSection.setBounds (top);
            characterSection.setBounds (r.removeFromLeft (curveSection.getWidth() + 120));
            r.removeFromLeft (gap);
            controlSection.setBounds (r);

            {
                auto title = curveSection.titleArea();
                power.setBounds (title.removeFromRight (18).withSizeKeepingCentre (18, 18));
                title.removeFromRight (6);
                lock.setBounds (title.removeFromRight (18).withSizeKeepingCentre (18, 18));
                curve.setBounds (curveSection.content());
            }
            {
                auto c = dynamicsSection.content().withTrimmedTop (-6);
                meterBars.setBounds (c.removeFromRight (120).withTrimmedLeft (12));
                history.setBounds (c);
            }
            characters.setBounds (characterSection.content().withTrimmedTop (2));
            {
                auto title = controlSection.titleArea();
                autoGain.setBounds (title.removeFromRight (100).withSizeKeepingCentre (100, 18));
                auto c = controlSection.content();
                const int rowH = c.getHeight() / 2;
                layoutRow (c.removeFromTop (rowH).reduced (0, 4), { &thresh, &ratio, &knee, &attack });
                layoutRow (c.reduced (0, 4), { &release, &makeup, &mix, &schpf });
            }
        }

        // For offscreen screenshots: let the live displays take one frame of the current audio
        void prepareSnapshotFrame() { curve.tick(); history.tick(); meterBars.tick(); }

    private:
        // Knob with a custom tooltip text (the slider inside keeps the parameter name otherwise)
        struct TipKnob : Knob
        {
            using Knob::Knob;
            void setTooltipText (const juce::String& t) { for (auto* c : getChildren()) if (auto* s = dynamic_cast<juce::Slider*> (c)) s->setTooltip (t); }
        };

        Section curveSection, dynamicsSection, characterSection, controlSection;
        TransferCurve curve;
        DynamicsHistory history;
        CompMeters meterBars;
        CompCharacters characters;
        Toggle power;
        LockButton lock;
        Toggle autoGain;
        Knob thresh, ratio, knee, attack, release, makeup, mix;
        TipKnob schpf;
    };
}
