#pragma once
#include "SynthPage.h"
#include "../fx/Rhythm.h"

namespace fjord::ui
{
    // Step gate grid: click toggles a step, dragging vertically sets its strength,
    // dragging horizontally across steps "paints" the value.
    class StepGrid : public juce::Component, private juce::Timer
    {
    public:
        StepGrid (APVTS& s, std::function<int()> currentStep) : playhead (std::move (currentStep))
        {
            for (int i = 0; i < rhythm::kMaxSteps; ++i)
            {
                auto* param = s.getParameter (params::gateStep (i));
                attachments[(size_t) i] = std::make_unique<juce::ParameterAttachment> (
                    *param, [this, i] (float v) { values[(size_t) i] = v; repaint(); }, nullptr);
                attachments[(size_t) i]->sendInitialUpdate();
            }
            stepsParam = s.getRawParameterValue ("gate_steps");
            onParam = s.getRawParameterValue ("gate_on");
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
            startTimerHz (30);
        }

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            const int n = numSteps();
            const bool on = onParam->load() > 0.5f;

            for (int i = 0; i < n; ++i)
            {
                const auto cell = cellBounds (i);
                const float v = values[(size_t) i];
                g.setColour (p.track.withAlpha (i / 4 % 2 == 0 ? 1.0f : 0.6f));
                g.fillRoundedRectangle (cell, 3.0f);

                if (v > 0.001f)
                {
                    const auto bar = cell.withTop (cell.getBottom() - cell.getHeight() * v);
                    g.setColour ((on ? p.accent : p.textDim).withAlpha (0.45f + 0.55f * v));
                    g.fillRoundedRectangle (bar, 3.0f);
                }

                if (i == shownStep && on)
                {
                    g.setColour (p.text);
                    g.drawRoundedRectangle (cell.expanded (1.5f), 4.0f, 1.5f);
                }
            }
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            const int i = cellAt (e.position.x);
            if (i < 0) return;
            dragStartY = e.position.y;
            dragStep = i;
            // Click: toggle on/off; this value is painted on when dragging horizontally
            paintValue = values[(size_t) i] > 0.001f ? 0.0f : 1.0f;
            write (i, paintValue);
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            const int i = cellAt (e.position.x);
            if (i < 0) return;
            if (i == dragStep && std::abs (e.position.y - dragStartY) > 3.0f)
            {
                // Vertical: set the strength
                const auto cell = cellBounds (i);
                write (i, std::clamp ((cell.getBottom() - e.position.y) / cell.getHeight(), 0.0f, 1.0f));
            }
            else if (i != dragStep)
            {
                write (i, paintValue);
            }
        }

        void setPattern (const char* pattern)
        {
            for (int i = 0; i < rhythm::kMaxSteps; ++i)
                write (i, rhythm::patternValue (pattern, i));
        }

    private:
        void timerCallback() override
        {
            const int s = playhead();
            if (s != shownStep) { shownStep = s; repaint(); }
        }

        int numSteps() const { return 8 << std::clamp (juce::roundToInt (stepsParam->load()), 0, 2); }

        juce::Rectangle<float> cellBounds (int i) const
        {
            const int n = numSteps();
            const float gap = n > 16 ? 3.0f : 5.0f;
            const float w = ((float) getWidth() - gap * (float) (n - 1)) / (float) n;
            return { (float) i * (w + gap), 0.0f, w, (float) getHeight() };
        }

        int cellAt (float x) const
        {
            const int n = numSteps();
            for (int i = 0; i < n; ++i)
                if (x >= cellBounds (i).getX() - 2.0f && x <= cellBounds (i).getRight() + 2.0f) return i;
            return -1;
        }

        void write (int i, float v) { attachments[(size_t) i]->setValueAsCompleteGesture (v); }

        std::function<int()> playhead;
        std::array<std::unique_ptr<juce::ParameterAttachment>, rhythm::kMaxSteps> attachments;
        std::array<float, rhythm::kMaxSteps> values {};
        std::atomic<float>* stepsParam = nullptr, *onParam = nullptr;
        int shownStep = -1, dragStep = -1;
        float dragStartY = 0.0f, paintValue = 1.0f;
    };

    // Curve of the sidechain pump (four beats)
    class PumpCurve : public PolledDisplay
    {
    public:
        explicit PumpCurve (APVTS& s)
            : depth (s.getRawParameterValue ("pump_depth")), release (s.getRawParameterValue ("pump_release")),
              curve (s.getRawParameterValue ("pump_curve")), on (s.getRawParameterValue ("pump_on")) {}

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            rhythm::PumpParams pp;
            pp.depth = depth->load(); pp.release = release->load(); pp.curve = curve->load();
            auto r = getLocalBounds().toFloat().reduced (2.0f, 6.0f);
            drawCurve (g, r, [&] (float x)
            {
                const double ph = x * 4.0 - std::floor (x * 4.0);
                return rhythm::pumpGain (ph, pp) * 2.0f - 1.0f;
            }, on->load() > 0.5f ? p.accent : p.textDim, true);
        }

    private:
        juce::String fingerprint() const override
        {
            return juce::String (depth->load()) + juce::String (release->load()) + juce::String (curve->load())
                 + juce::String (on->load()) + (Theme::get().isDark() ? "d" : "l");
        }
        std::atomic<float>* depth, *release, *curve, *on;
    };

    // Stutter timeline: one grid cell ("Every") divided into slices. Normal signal light,
    // the recording outlined, the repeats filled (height = decay, width = gate, line = pitch).
    class StutterView : public PolledDisplay
    {
    public:
        explicit StutterView (APVTS& s)
        {
            for (auto* id : { "stutter_on", "stutter_trigger", "stutter_interval", "stutter_slice", "stutter_start",
                              "stutter_duration", "stutter_gate", "stutter_pitch", "stutter_decay", "stutter_reverse", "stutter_chance" })
                refs.push_back (s.getRawParameterValue (id));
        }

        void paint (juce::Graphics& g) override
        {
            const auto& pal = Theme::get().palette();
            auto r = getLocalBounds().toFloat().reduced (2.0f, 4.0f);
            auto labels = r.removeFromBottom (14.0f);
            const bool on = v (0) > 0.5f, wheel = juce::roundToInt (v (1)) == 1;
            const double every = rhythm::stutterEveryBeats[std::clamp (juce::roundToInt (v (2)), 0, 3)];
            const double slice = rhythm::stutterSliceBeats[std::clamp (juce::roundToInt (v (3)), 0, 4)];
            const double start = wheel ? 0.0 : rhythm::stutterStartFraction[std::clamp (juce::roundToInt (v (4)), 0, 3)];
            const double length = wheel ? 1.0 : v (5);
            const float gate = v (6), pitch = v (7), decay = v (8);
            const auto accent = on ? pal.accent : pal.textDim;

            // Base bar with grid
            g.setColour (pal.track);
            g.fillRoundedRectangle (r, 4.0f);
            const int slices = (int) std::round (every / slice);
            const float sw = r.getWidth() / (float) slices;
            g.setColour (pal.line);
            for (int k = 1; k < slices && sw > 3.0f; ++k)
                g.fillRect (r.getX() + sw * (float) k, r.getY() + (k % std::max (1, (int) (1.0 / slice)) == 0 ? 0.0f : r.getHeight() * 0.7f),
                            1.0f, r.getHeight() * (k % std::max (1, (int) (1.0 / slice)) == 0 ? 1.0f : 0.3f));

            // Stutter window
            const float x0 = r.getX() + r.getWidth() * (float) start;
            const float x1 = x0 + r.getWidth() * (float) ((1.0 - start) * length);
            const int first = (int) std::round (start * slices);
            const int count = std::max (1, (int) std::round ((x1 - x0) / sw));
            float level = 1.0f;
            juce::Path pitchLine;
            for (int k = 0; k < count && first + k < slices; ++k)
            {
                const float x = r.getX() + sw * (float) (first + k);
                if (k == 0)
                {
                    g.setColour (accent);
                    g.drawRoundedRectangle (x + 1.0f, r.getY() + 1.0f, sw - 2.0f, r.getHeight() - 2.0f, 3.0f, 1.4f);
                    continue;
                }
                const float h = r.getHeight() * level;
                const float w = std::max (2.0f, (sw - 2.0f) * gate);
                g.setColour (accent.withAlpha (0.35f + 0.55f * level));
                g.fillRoundedRectangle (x + 1.0f, r.getBottom() - h, w, h, 2.5f);
                const float py = r.getBottom() - h + 4.0f - (float) (pitch / 24.0) * r.getHeight() * 0.4f * (float) k / (float) count;
                if (k == 1) pitchLine.startNewSubPath (x + 1.0f, py); else pitchLine.lineTo (x + 1.0f + w * 0.5f, py);
                level *= 1.0f - decay;
            }
            if (std::abs (pitch) > 0.1f && ! pitchLine.isEmpty())
            {
                g.setColour (pal.text.withAlpha (0.6f));
                g.strokePath (pitchLine, juce::PathStrokeType (1.2f));
            }

            g.setColour (pal.textDim);
            g.setFont (font (10.5f, Weight::Medium));
            const auto info = wheel ? juce::String ("Hold the mod wheel up to stutter")
                                    : juce::String (juce::roundToInt (v (10) * 100.0f)) + "% chance every "
                                      + rhythm::stutterEveryNames[std::clamp (juce::roundToInt (v (2)), 0, 3)].toLowerCase();
            g.drawText (info, labels, juce::Justification::centredLeft);
            g.drawText (v (9) > 0.5f ? "reverse" : "", labels, juce::Justification::centredRight);
        }

    private:
        float v (size_t i) const { return refs[i]->load(); }
        juce::String fingerprint() const override
        {
            juce::String f;
            for (auto* r : refs) f << juce::String (r->load(), 3) << ":";
            return f + (Theme::get().isDark() ? "d" : "l");
        }
        std::vector<std::atomic<float>*> refs;
    };

    class StutterPanel : public Section, private juce::Timer
    {
    public:
        explicit StutterPanel (APVTS& s)
            : Section ("Stutter"), state (s), power (s, "stutter_on"), lock (s, 3, false), reverse (s, "stutter_reverse", "Reverse"),
              trigger (s, "stutter_trigger"), every (s, "stutter_interval"), slice (s, "stutter_slice"), start (s, "stutter_start"),
              view (s),
              chance (s, "stutter_chance", "Chance"), length (s, "stutter_duration", "Length"), gate (s, "stutter_gate", "Gate"),
              pitch (s, "stutter_pitch", "Pitch"), decay (s, "stutter_decay", "Decay"), mix (s, "stutter_mix", "Mix"),
              triggerParam (s.getRawParameterValue ("stutter_trigger"))
        {
            for (auto* c : std::initializer_list<juce::Component*> { &power, &lock, &reverse, &trigger, &every, &slice, &start, &view,
                                                                      &chance, &length, &gate, &pitch, &decay, &mix, &presets })
                addAndMakeVisible (c);

            // Presets: set all stutter values at once (and switch the stutter on)
            int id = 1;
            for (const auto& sp : rhythm::stutterPresets)
                presets.addItem (sp.name, id++);
            presets.setTextWhenNothingSelected ("Presets");
            presets.onChange = [this, &s]
            {
                const int i = presets.getSelectedItemIndex();
                if (i >= 0) apply (s, rhythm::stutterPresets[i]);
                presets.setSelectedId (0, juce::dontSendNotification);
                updatePresetName();
            };
            trigger.setTooltip ("Auto: stutters by chance on the grid. Mod Wheel: stutters while you hold the wheel up.");
            every.setTooltip ("How often a stutter can happen");
            slice.setTooltip ("Length of the repeated piece");
            start.setTooltip ("Where in the grid the stutter begins (3/4 and 7/8 make fills at the end of the bar)");
            updateMode();
            startTimerHz (8);
        }

        void paint (juce::Graphics& g) override
        {
            Section::paint (g);
            g.setColour (Theme::get().palette().textDim);
            for (const auto& [label, comp] : { std::pair<const char*, juce::Component*> { "EVERY", &every }, { "SLICE", &slice },
                                               { "START", &start } })
                drawSpacedText (g, label, comp->getBounds().toFloat().withX ((float) content().getX()).withWidth (52.0f),
                                9.5f, juce::Justification::centredLeft, 0.18f);
        }

        void resized() override
        {
            auto t = titleArea();
            power.setBounds (t.removeFromRight (18).withSizeKeepingCentre (18, 18));
            t.removeFromRight (8);
            lock.setBounds (t.removeFromRight (18).withSizeKeepingCentre (18, 18));
            t.removeFromRight (10);
            reverse.setBounds (t.removeFromRight (80).withSizeKeepingCentre (80, 18));
            t.removeFromRight (10);
            trigger.setBounds (t.removeFromRight (170).withSizeKeepingCentre (170, 22));
            t.removeFromLeft (80);
            presets.setBounds (t.withSizeKeepingCentre (std::min (t.getWidth(), 150), 24));

            auto c = content();
            auto row = [&c] (int h) { auto r = c.removeFromTop (h); return r.withTrimmedLeft (54); };
            every.setBounds (row (24).reduced (0, 1));
            slice.setBounds (row (24).reduced (0, 1));
            start.setBounds (row (24).reduced (0, 1));
            view.setBounds (c.removeFromTop (58).reduced (0, 4));
            layoutRow (c.reduced (0, 2), { &chance, &length, &gate, &pitch, &decay, &mix });
        }

    private:
        // Calls f (parameter ID, plain value) for every value of a stutter preset.
        template <typename F>
        static void forEachValue (const rhythm::StutterPreset& sp, F&& f)
        {
            f ("stutter_trigger", (float) sp.trigger);
            f ("stutter_interval", (float) sp.every);
            f ("stutter_slice", (float) sp.slice);
            f ("stutter_start", (float) sp.start);
            f ("stutter_chance", sp.chance);
            f ("stutter_duration", sp.length);
            f ("stutter_gate", sp.gate);
            f ("stutter_pitch", sp.pitch);
            f ("stutter_decay", sp.decay);
            f ("stutter_reverse", sp.reverse ? 1.0f : 0.0f);
            f ("stutter_mix", sp.mix);
        }

        static void apply (APVTS& s, const rhythm::StutterPreset& sp)
        {
            auto setValue = [&s] (const char* id, float v)
            {
                auto* p = s.getParameter (id);
                p->beginChangeGesture();
                p->setValueNotifyingHost (p->convertTo0to1 (v));
                p->endChangeGesture();
            };
            setValue ("stutter_on", 1.0f);
            forEachValue (sp, setValue);
        }

        static bool matches (APVTS& s, const rhythm::StutterPreset& sp)
        {
            bool same = true;
            forEachValue (sp, [&] (const char* id, float v)
            {
                auto* p = s.getParameter (id);
                same = same && std::abs (p->getValue() - p->convertTo0to1 (v)) < 1.0e-3f;
            });
            return same;
        }

        // The menu shows the name of the preset the current settings match, otherwise "Presets".
        void updatePresetName()
        {
            int match = -1;
            for (int i = 0; i < (int) std::size (rhythm::stutterPresets) && match < 0; ++i)
                if (matches (state, rhythm::stutterPresets[i]))
                    match = i;
            if (match == shownPreset) return;
            shownPreset = match;
            presets.setTextWhenNothingSelected (match >= 0 ? rhythm::stutterPresets[match].name : "Presets");
        }

        void timerCallback() override
        {
            updateMode();
            if (isShowing()) updatePresetName();
        }

        // In mod wheel mode the grid settings have no effect and are dimmed.
        void updateMode()
        {
            const bool wheel = juce::roundToInt (triggerParam->load()) == 1;
            for (auto* c : std::initializer_list<juce::Component*> { &every, &start, &chance, &length })
                c->setAlpha (wheel ? 0.35f : 1.0f);
        }

        APVTS& state;
        Toggle power;
        LockButton lock;
        Toggle reverse;
        Segmented trigger, every, slice, start;
        StutterView view;
        Knob chance, length, gate, pitch, decay, mix;
        juce::ComboBox presets;
        int shownPreset = -1;
        std::atomic<float>* triggerParam;
    };

    class PumpPanel : public Section
    {
    public:
        explicit PumpPanel (APVTS& s)
            : Section ("Pump"), power (s, "pump_on"), lock (s, 4, false), curveView (s),
              rate (s, "pump_rate", "Rate"), depth (s, "pump_depth", "Depth"), release (s, "pump_release", "Release"), curve (s, "pump_curve", "Curve")
        {
            for (auto* c : std::initializer_list<juce::Component*> { &power, &lock, &curveView, &rate, &depth, &release, &curve })
                addAndMakeVisible (c);
        }

        void resized() override
        {
            auto t = titleArea();
            power.setBounds (t.removeFromRight (18).withSizeKeepingCentre (18, 18));
            t.removeFromRight (8);
            lock.setBounds (t.removeFromRight (18).withSizeKeepingCentre (18, 18));
            auto c = content();
            curveView.setBounds (c.removeFromTop (c.getHeight() * 2 / 5).reduced (2, 0));
            layoutRow (c.reduced (0, 4), { &rate, &depth, &release, &curve });
        }

    private:
        Toggle power;
        LockButton lock;
        PumpCurve curveView;
        Knob rate, depth, release, curve;
    };

    class GatePanel : public Section
    {
    public:
        GatePanel (APVTS& s, std::function<int()> currentStep)
            : Section ("Step Gate"), power (s, "gate_on"), lock (s, 2, false), steps (s, "gate_steps"), grid (s, std::move (currentStep)),
              rate (s, "gate_rate", "Rate"), length (s, "gate_length", "Length"), smooth (s, "gate_smooth", "Smooth"),
              depth (s, "gate_depth", "Depth"), swing (s, "gate_swing", "Swing")
        {
            for (auto* c : std::initializer_list<juce::Component*> { &power, &lock, &steps, &patterns, &grid, &rate, &length, &smooth, &depth, &swing })
                addAndMakeVisible (c);

            int id = 1;
            for (const auto& pat : rhythm::gatePatterns)
                patterns.addItem (pat.name, id++);
            patterns.setTextWhenNothingSelected ("Patterns");
            patterns.onChange = [this]
            {
                const int i = patterns.getSelectedItemIndex();
                if (i >= 0) grid.setPattern (rhythm::gatePatterns[i].steps);
                patterns.setSelectedId (0, juce::dontSendNotification);
            };
        }

        void resized() override
        {
            auto t = titleArea();
            power.setBounds (t.removeFromRight (18).withSizeKeepingCentre (18, 18));
            t.removeFromRight (8);
            lock.setBounds (t.removeFromRight (18).withSizeKeepingCentre (18, 18));
            t.removeFromRight (14);
            patterns.setBounds (t.removeFromRight (150).withSizeKeepingCentre (150, 24));
            t.removeFromRight (12);
            steps.setBounds (t.removeFromRight (130).withSizeKeepingCentre (130, 24));

            auto c = content();
            auto knobs = c.removeFromRight (std::min (460, c.getWidth() / 2));
            grid.setBounds (c.reduced (4, 14).withTrimmedRight (16));
            layoutRow (knobs.reduced (0, 10), { &rate, &length, &smooth, &depth, &swing });
        }

    private:
        Toggle power;
        LockButton lock;
        Segmented steps;
        juce::ComboBox patterns;
        StepGrid grid;
        Knob rate, length, smooth, depth, swing;
    };

    class RhythmPage : public juce::Component
    {
    public:
        RhythmPage (APVTS& s, std::function<int()> currentGateStep)
            : stutter (s), pump (s), gate (s, std::move (currentGateStep))
        {
            for (auto* c : std::initializer_list<juce::Component*> { &stutter, &pump, &gate })
                addAndMakeVisible (c);
        }

        void resized() override
        {
            constexpr int gap = 10;
            auto r = getLocalBounds();
            auto top = r.removeFromTop ((r.getHeight() - gap) * 57 / 100);
            r.removeFromTop (gap);
            const int w = top.getWidth() - gap;
            stutter.setBounds (top.removeFromLeft (w * 62 / 100)); top.removeFromLeft (gap);
            pump.setBounds (top);
            gate.setBounds (r);
        }

    private:
        StutterPanel stutter;
        PumpPanel pump;
        GatePanel gate;
    };
}
