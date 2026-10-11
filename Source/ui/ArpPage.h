#pragma once
#include "SynthPage.h"
#include "../fx/Rhythm.h"

namespace fjord::ui
{
    // Step lanes of the arp pattern:
    //   Velocity  draw the bars (drag to the bottom = rest)
    //   Pitch     draw the transposition in semitones (double-click = 0)
    //   Tie       click to tie a step into the next one (legato / slides in mono mode)
    // Clicking a step number sets the pattern length.
    class ArpLanes : public juce::Component, private juce::Timer
    {
    public:
        ArpLanes (APVTS& s, std::function<int()> currentStep) : playhead (std::move (currentStep))
        {
            using rhythm::kArpSteps;
            for (int i = 0; i < kArpSteps; ++i)
            {
                auto attach = [this, &s] (const juce::String& id, float& target)
                {
                    auto a = std::make_unique<juce::ParameterAttachment> (
                        *s.getParameter (id), [this, &target] (float v) { target = v; repaint(); }, nullptr);
                    a->sendInitialUpdate();
                    return a;
                };
                vel[(size_t) i] = attach (params::arpStep (i, "vel"), velValues[(size_t) i]);
                pitch[(size_t) i] = attach (params::arpStep (i, "pitch"), pitchValues[(size_t) i]);
                tie[(size_t) i] = attach (params::arpStep (i, "tie"), tieValues[(size_t) i]);
            }
            stepsAttachment = std::make_unique<juce::ParameterAttachment> (
                *s.getParameter ("arp_steps"), [this] (float v) { length = juce::roundToInt (v); repaint(); }, nullptr);
            stepsAttachment->sendInitialUpdate();
            onParam = s.getRawParameterValue ("arp_on");
            startTimerHz (30);
        }

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            const bool on = onParam->load() > 0.5f;
            const auto accent = on ? p.accent : p.textDim;

            g.setColour (p.textDim);
            for (const auto& [label, area] : { std::pair<const char*, juce::Rectangle<int>> { "VELOCITY", velArea() },
                                               { "PITCH", pitchArea() }, { "TIE", tieArea() }, { "STEP", numberArea() } })
                drawSpacedText (g, label, area.withX (0).withWidth (labelWidth - 8).toFloat(), 9.5f,
                                juce::Justification::centredLeft, 0.18f);

            for (int i = 0; i < rhythm::kArpSteps; ++i)
            {
                const bool inside = i < length;
                const float alpha = inside ? 1.0f : 0.28f;
                const bool current = i == shownStep && on;

                // Velocity
                auto cell = cellFor (velArea(), i);
                g.setColour (p.track.withAlpha ((i / 4 % 2 == 0 ? 1.0f : 0.6f) * alpha));
                g.fillRoundedRectangle (cell, 3.0f);
                const float v = velValues[(size_t) i];
                if (v > 0.001f)
                {
                    g.setColour (accent.withAlpha ((0.45f + 0.55f * v) * alpha));
                    g.fillRoundedRectangle (cell.withTop (cell.getBottom() - cell.getHeight() * v), 3.0f);
                }
                else if (inside)
                {
                    g.setColour (p.textDim.withAlpha (0.6f));
                    g.fillRect (cell.getCentreX() - 4.0f, cell.getBottom() - 6.0f, 8.0f, 1.5f);   // rest
                }

                // Pitch (bipolar)
                auto pc = cellFor (pitchArea(), i);
                g.setColour (p.track.withAlpha ((i / 4 % 2 == 0 ? 1.0f : 0.6f) * alpha));
                g.fillRoundedRectangle (pc, 3.0f);
                const int st = juce::roundToInt (pitchValues[(size_t) i]);
                if (st != 0)
                {
                    const float h = pc.getHeight() * 0.5f * (float) std::abs (st) / 12.0f;
                    const auto bar = st > 0 ? pc.withTop (pc.getCentreY() - h).withBottom (pc.getCentreY())
                                            : pc.withTop (pc.getCentreY()).withHeight (h);
                    g.setColour (accent.withAlpha (0.75f * alpha));
                    g.fillRoundedRectangle (bar, 2.0f);
                    g.setColour (p.text.withAlpha (alpha));
                    g.setFont (font (9.5f, Weight::Medium));
                    g.drawText ((st > 0 ? "+" : "") + juce::String (st), pc.withHeight (14.0f).withY (st > 0 ? pc.getBottom() - 15.0f : pc.getY() + 1.0f),
                                juce::Justification::centred);
                }
                g.setColour (p.line.withAlpha (alpha));
                g.fillRect (pc.getX() + 2.0f, pc.getCentreY(), pc.getWidth() - 4.0f, 1.0f);

                // Tie
                auto tc = cellFor (tieArea(), i);
                const bool tied = tieValues[(size_t) i] > 0.5f;
                g.setColour ((tied ? accent : p.line).withAlpha (alpha));
                if (tied)
                    g.fillRoundedRectangle (tc.withSizeKeepingCentre (tc.getWidth() + (i < rhythm::kArpSteps - 1 ? gap() : 0.0f), 6.0f)
                                              .withX (tc.getCentreX()), 3.0f);
                const auto dot = tc.withSizeKeepingCentre (10.0f, 10.0f);
                g.setColour ((tied ? accent : p.line).withAlpha (alpha));
                if (tied) g.fillEllipse (dot); else g.drawEllipse (dot.reduced (0.5f), 1.2f);

                // Step numbers (click sets the length)
                auto nc = cellFor (numberArea(), i);
                g.setColour (current ? p.accent : (inside ? p.text : p.textDim).withAlpha (inside ? 0.8f : 0.5f));
                g.setFont (font (10.0f, current ? Weight::SemiBold : Weight::Medium));
                g.drawText (juce::String (i + 1), nc, juce::Justification::centred);

                if (current)
                {
                    g.setColour (p.text);
                    g.drawRoundedRectangle (cell.expanded (1.5f), 4.0f, 1.5f);
                }
            }

            // Length marker
            const float lx = cellFor (velArea(), length - 1).getRight() + gap() * 0.5f;
            g.setColour (accent);
            g.fillRect (lx - 0.75f, (float) velArea().getY(), 1.5f, (float) (numberArea().getBottom() - velArea().getY()));
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            const int i = stepAt (e.position.x);
            if (i < 0) return;
            lane = laneAt (e.position.y);
            if (lane == Lane::Number) { stepsAttachment->setValueAsCompleteGesture ((float) (i + 1)); return; }
            if (lane == Lane::Tie)
            {
                paintTie = tieValues[(size_t) i] < 0.5f;
                tie[(size_t) i]->setValueAsCompleteGesture (paintTie ? 1.0f : 0.0f);
                return;
            }
            drawAt (e.position);
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (lane == Lane::Tie)
            {
                const int i = stepAt (e.position.x);
                if (i >= 0 && (tieValues[(size_t) i] > 0.5f) != paintTie)
                    tie[(size_t) i]->setValueAsCompleteGesture (paintTie ? 1.0f : 0.0f);
            }
            else if (lane == Lane::Velocity || lane == Lane::Pitch)
            {
                drawAt (e.position);
            }
        }

        void mouseUp (const juce::MouseEvent&) override { lane = Lane::None; }

        void mouseDoubleClick (const juce::MouseEvent& e) override
        {
            const int i = stepAt (e.position.x);
            if (i < 0) return;
            const auto l = laneAt (e.position.y);
            if (l == Lane::Pitch)    pitch[(size_t) i]->setValueAsCompleteGesture (0.0f);
            if (l == Lane::Velocity) vel[(size_t) i]->setValueAsCompleteGesture (1.0f);
        }

        void mouseMove (const juce::MouseEvent& e) override
        {
            setMouseCursor (stepAt (e.position.x) >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        }

        void applyPattern (const rhythm::ArpPattern& pat)
        {
            const int len = (int) std::strlen (pat.velocity);
            for (int i = 0; i < rhythm::kArpSteps; ++i)
            {
                vel[(size_t) i]->setValueAsCompleteGesture (rhythm::arpVelocityValue (pat.velocity[i % len]));
                pitch[(size_t) i]->setValueAsCompleteGesture ((float) pat.pitch[(size_t) (i % std::max (1, pat.steps))]);
                tie[(size_t) i]->setValueAsCompleteGesture (pat.ties[i % len] == 't' ? 1.0f : 0.0f);
            }
            stepsAttachment->setValueAsCompleteGesture ((float) pat.steps);
        }

        // Musical random pattern: mostly root notes, some octaves and fifths, a few rests and ties.
        void randomize()
        {
            juce::Random r;
            const int choices[] = { 0, 0, 0, 0, 12, 12, 7, -12, 5, 3 };
            for (int i = 0; i < rhythm::kArpSteps; ++i)
            {
                const float v = i % 4 == 0 ? 1.0f : (r.nextFloat() < 0.18f ? 0.0f : 0.4f + 0.6f * r.nextFloat());
                vel[(size_t) i]->setValueAsCompleteGesture (std::round (v * 10.0f) / 10.0f);
                pitch[(size_t) i]->setValueAsCompleteGesture ((float) choices[r.nextInt ((int) std::size (choices))]);
                tie[(size_t) i]->setValueAsCompleteGesture (r.nextFloat() < 0.1f ? 1.0f : 0.0f);
            }
        }

    private:
        enum class Lane { None, Velocity, Pitch, Tie, Number };
        static constexpr int labelWidth = 74;

        juce::Rectangle<int> lanesArea() const { return getLocalBounds().withTrimmedLeft (labelWidth); }
        juce::Rectangle<int> numberArea() const { return lanesArea().removeFromBottom (16); }
        juce::Rectangle<int> tieArea() const { return lanesArea().withTrimmedBottom (22).removeFromBottom (18); }
        juce::Rectangle<int> velArea() const
        {
            auto r = lanesArea().withTrimmedBottom (48);
            return r.removeFromTop (r.getHeight() * 56 / 100);
        }
        juce::Rectangle<int> pitchArea() const
        {
            auto r = lanesArea().withTrimmedBottom (48);
            r.removeFromTop (r.getHeight() * 56 / 100 + 10);
            return r;
        }

        float gap() const { return 5.0f; }

        juce::Rectangle<float> cellFor (juce::Rectangle<int> area, int i) const
        {
            const float w = ((float) area.getWidth() - gap() * (float) (rhythm::kArpSteps - 1)) / (float) rhythm::kArpSteps;
            return { (float) area.getX() + (float) i * (w + gap()), (float) area.getY(), w, (float) area.getHeight() };
        }

        int stepAt (float x) const
        {
            for (int i = 0; i < rhythm::kArpSteps; ++i)
            {
                const auto c = cellFor (velArea(), i);
                if (x >= c.getX() - gap() * 0.5f && x <= c.getRight() + gap() * 0.5f) return i;
            }
            return -1;
        }

        Lane laneAt (float y) const
        {
            if (y < (float) velArea().getBottom() + 5.0f) return Lane::Velocity;
            if (y < (float) pitchArea().getBottom() + 2.0f) return Lane::Pitch;
            if (y < (float) tieArea().getBottom() + 2.0f) return Lane::Tie;
            return Lane::Number;
        }

        void drawAt (juce::Point<float> pos)
        {
            const int i = stepAt (pos.x);
            if (i < 0) return;
            if (lane == Lane::Velocity)
            {
                const auto c = cellFor (velArea(), i);
                float v = std::clamp ((c.getBottom() - pos.y) / c.getHeight(), 0.0f, 1.0f);
                if (v < 0.08f) v = 0.0f;   // bottom = rest
                vel[(size_t) i]->setValueAsCompleteGesture (std::round (v * 20.0f) / 20.0f);
            }
            else if (lane == Lane::Pitch)
            {
                const auto c = cellFor (pitchArea(), i);
                const float rel = (c.getCentreY() - pos.y) / (c.getHeight() * 0.5f);
                pitch[(size_t) i]->setValueAsCompleteGesture ((float) juce::roundToInt (std::clamp (rel, -1.0f, 1.0f) * 12.0f));
            }
        }

        void timerCallback() override
        {
            const int s = playhead();
            if (s != shownStep) { shownStep = s; repaint(); }
        }

        std::function<int()> playhead;
        std::array<std::unique_ptr<juce::ParameterAttachment>, rhythm::kArpSteps> vel, pitch, tie;
        std::array<float, rhythm::kArpSteps> velValues {}, pitchValues {}, tieValues {};
        std::unique_ptr<juce::ParameterAttachment> stepsAttachment;
        std::atomic<float>* onParam = nullptr;
        int length = rhythm::kArpSteps, shownStep = -1;
        Lane lane = Lane::None;
        bool paintTie = true;
    };

    // ---------------------------------------------------------------------------------------------
    class ArpPage : public juce::Component
    {
    public:
        ArpPage (APVTS& s, std::function<int()> currentStep)
            : main ("Arpeggiator"), pattern ("Pattern"),
              power (s, "arp_on"), latch (s, "arp_latch", "Latch"), retrig (s, "arp_retrig", "Retrigger"),
              lock (s, 1), mode (s, "arp_mode"),
              rate (s, "arp_rate", "Rate"), octaves (s, "arp_oct", "Octaves"), gate (s, "arp_gate", "Gate"),
              swing (s, "arp_swing", "Swing"), repeats (s, "arp_repeat", "Repeats"), chance (s, "arp_chance", "Chance"),
              steps (s, "arp_steps", "Steps"), lanes (s, std::move (currentStep))
        {
            addAndMakeVisible (main);
            addAndMakeVisible (pattern);
            for (auto* c : std::initializer_list<juce::Component*> { &power, &latch, &retrig, &lock, &mode, &rate, &octaves,
                                                                      &gate, &swing, &repeats, &chance, &steps })
                main.addAndMakeVisible (c);
            pattern.addAndMakeVisible (lanes);
            pattern.addAndMakeVisible (patterns);

            latch.setTooltip ("Latch: notes keep playing after you release the keys");
            retrig.setTooltip ("Retrigger: every new key starts the pattern from step 1");

            int id = 1;
            for (const auto& pat : rhythm::arpPatterns)
                patterns.addItem (pat.name, id++);
            patterns.addSeparator();
            patterns.addItem ("Randomize", randomId);
            patterns.setTextWhenNothingSelected ("Patterns");
            patterns.onChange = [this]
            {
                const int sel = patterns.getSelectedId();
                if (sel == randomId) lanes.randomize();
                else if (sel > 0)    lanes.applyPattern (rhythm::arpPatterns[sel - 1]);
                patterns.setSelectedId (0, juce::dontSendNotification);
            };
        }

        void resized() override
        {
            constexpr int gapY = 10;
            auto r = getLocalBounds();
            main.setBounds (r.removeFromTop (r.getHeight() * 40 / 100));
            r.removeFromTop (gapY);
            pattern.setBounds (r);

            // Main section
            auto t = main.titleArea();
            power.setBounds (t.removeFromRight (18).withSizeKeepingCentre (18, 18));
            t.removeFromRight (14);
            lock.setBounds (t.removeFromRight (78).withSizeKeepingCentre (78, 18));
            t.removeFromRight (10);
            retrig.setBounds (t.removeFromRight (100).withSizeKeepingCentre (100, 18));
            t.removeFromRight (6);
            latch.setBounds (t.removeFromRight (70).withSizeKeepingCentre (70, 18));

            auto c = main.content();
            mode.setBounds (c.removeFromTop (28).reduced (0, 2));
            layoutRow (c.reduced (90, 22), { &rate, &octaves, &gate, &swing, &repeats, &chance, &steps });

            // Pattern section
            auto pt = pattern.titleArea();
            patterns.setBounds (pt.removeFromRight (170).withSizeKeepingCentre (170, 24));
            lanes.setBounds (pattern.content().reduced (4, 6));
        }

    private:
        static constexpr int randomId = 1000;

        Section main, pattern;
        Toggle power, latch, retrig;
        LockButton lock;
        Segmented mode;
        Knob rate, octaves, gate, swing, repeats, chance, steps;
        ArpLanes lanes;
        juce::ComboBox patterns;
    };
}
