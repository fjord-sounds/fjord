#pragma once
#include "SynthPage.h"

namespace fjord::ui
{
    // Bipolar amount bar (−100 … +100 %), fills from the centre.
    class AmountBar : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        AmountBar (APVTS& s, const juce::String& paramId)
        {
            param = s.getParameter (paramId);
            attachment = std::make_unique<juce::ParameterAttachment> (*param, [this] (float v) { value = v; repaint(); }, nullptr);
            attachment->sendInitialUpdate();
            setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
        }

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            auto r = getLocalBounds().toFloat().reduced (1.0f, 4.0f);
            g.setColour (p.track);
            g.fillRoundedRectangle (r, 3.0f);

            const float centre = r.getCentreX();
            const float x = centre + value * r.getWidth() * 0.5f;
            if (std::abs (value) > 0.001f)
            {
                g.setColour (p.accent.withAlpha (0.85f));
                g.fillRoundedRectangle (juce::Rectangle<float> (std::min (centre, x), r.getY(), std::abs (x - centre), r.getHeight()), 2.0f);
            }
            g.setColour (p.textDim.withAlpha (0.5f));
            g.fillRect (centre - 0.5f, r.getY() - 2.0f, 1.0f, r.getHeight() + 4.0f);

            if (hover || dragging)
            {
                g.setColour (p.text);
                g.setFont (font (11.0f));
                g.drawText (param->getCurrentValueAsText(), r, juce::Justification::centred);
            }
        }

        void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
        void mouseExit (const juce::MouseEvent&) override  { hover = false; repaint(); }

        void mouseDown (const juce::MouseEvent& e) override
        {
            dragging = true;
            startValue = value;
            startX = e.x;
            attachment->beginGesture();
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            const float sensitivity = e.mods.isShiftDown() ? 0.2f : 1.0f;
            const float v = std::clamp (startValue + (float) (e.x - startX) / ((float) getWidth() * 0.5f) * sensitivity, -1.0f, 1.0f);
            attachment->setValueAsPartOfGesture (v);
        }

        void mouseUp (const juce::MouseEvent&) override
        {
            dragging = false;
            attachment->endGesture();
            repaint();
        }

        void mouseDoubleClick (const juce::MouseEvent&) override { attachment->setValueAsCompleteGesture (0.0f); }

    private:
        juce::RangedAudioParameter* param = nullptr;
        std::unique_ptr<juce::ParameterAttachment> attachment;
        float value = 0.0f, startValue = 0.0f;
        int startX = 0;
        bool hover = false, dragging = false;
    };

    class LfoPanel : public Section, private juce::Timer
    {
    public:
        LfoPanel (APVTS& s, int i)
            : Section ("LFO " + juce::String (i + 1)),
              syncParam (s.getRawParameterValue (params::lfo (i, "sync"))), lock (s, locks::lfoGroup (i), false),
              shape (s, params::lfo (i, "shape"), lfoGlyph),
              preview (s, i),
              sync (s, params::lfo (i, "sync"), "Sync"),
              retrig (s, params::lfo (i, "retrig"), "Retrig"),
              rate (s, params::lfo (i, "rate"), "Rate"),
              division (s, params::lfo (i, "div"), "Rate"),
              fade (s, params::lfo (i, "fade"), "Fade")
        {
            for (auto* c : std::initializer_list<juce::Component*> { &lock, &shape, &preview, &sync, &retrig, &rate, &division, &fade })
                addAndMakeVisible (c);
            retrig.setTooltip ("On: restarts with every note (per voice). Off: free-running and shared by all voices.");
            updateSync();
            startTimerHz (10);
        }

        void resized() override
        {
            auto title = titleArea();
            lock.setBounds (title.removeFromRight (18).withSizeKeepingCentre (18, 18));
            title.removeFromRight (8);
            retrig.setBounds (title.removeFromRight (70).withSizeKeepingCentre (70, 18));
            sync.setBounds (title.removeFromRight (62).withSizeKeepingCentre (62, 18));

            auto c = content();
            shape.setBounds (c.removeFromTop (26));
            preview.setBounds (c.removeFromTop (c.getHeight() / 3).reduced (4, 2));
            layoutRow (c.reduced (0, 2), { &rate, &fade });
            division.setBounds (rate.getBounds());
        }

    private:
        void timerCallback() override { updateSync(); }

        void updateSync()
        {
            const bool synced = syncParam->load() > 0.5f;
            rate.setVisible (! synced);
            division.setVisible (synced);
        }

        std::atomic<float>* syncParam;
        LockButton lock;
        Segmented shape;
        LfoPreview preview;
        Toggle sync, retrig;
        Knob rate, division, fade;
    };

    class MatrixPanel : public Section
    {
    public:
        explicit MatrixPanel (APVTS& s) : Section ("Modulation Matrix"), lock (s, locks::kMatrix, false)
        {
            addAndMakeVisible (lock);
            for (int i = 0; i < dsp::kNumModSlots; ++i)
            {
                auto& row = rows[(size_t) i];
                row.source = std::make_unique<Choice> (s, params::mod (i, "src"));
                row.via = std::make_unique<Choice> (s, params::mod (i, "via"));
                row.dest = std::make_unique<Choice> (s, params::mod (i, "dst"));
                row.amount = std::make_unique<AmountBar> (s, params::mod (i, "amt"));
                row.amount->setTooltip ("Drag to set the amount, double-click resets, Shift for fine control");
                for (auto* c : std::initializer_list<juce::Component*> { row.source.get(), row.via.get(), row.dest.get(), row.amount.get() })
                    addAndMakeVisible (c);
            }
        }

        void paint (juce::Graphics& g) override
        {
            Section::paint (g);
            const auto& p = Theme::get().palette();

            // Column headers and slot numbers
            for (int col = 0; col < 2; ++col)
            {
                const auto h = columnArea (col).removeFromTop (headerH).toFloat();
                g.setColour (p.textDim);
                auto x = h;
                x.removeFromLeft ((float) numberW);
                drawSpacedText (g, "SOURCE", x.removeFromLeft (sourceW (col)), 9.5f, juce::Justification::centredLeft, 0.18f);
                drawSpacedText (g, "VIA", x.removeFromLeft (viaW (col)), 9.5f, juce::Justification::centredLeft, 0.18f);
                x.removeFromLeft ((float) arrowW);
                drawSpacedText (g, "DESTINATION", x.removeFromLeft (destW (col)), 9.5f, juce::Justification::centredLeft, 0.18f);
                drawSpacedText (g, "AMOUNT", x, 9.5f, juce::Justification::centredLeft, 0.18f);
            }

            for (int i = 0; i < dsp::kNumModSlots; ++i)
            {
                const auto r = rowArea (i).toFloat();
                g.setColour (p.textDim);
                g.setFont (font (10.5f));
                g.drawText (juce::String (i + 1), r.withWidth ((float) numberW - 4.0f), juce::Justification::centredLeft);

                // Arrow between source and destination
                const float ax = r.getX() + (float) numberW + sourceW (i / 8) + viaW (i / 8) + (float) arrowW * 0.5f;
                juce::Path arrow;
                arrow.startNewSubPath (ax - 5.0f, r.getCentreY());
                arrow.lineTo (ax + 4.0f, r.getCentreY());
                arrow.startNewSubPath (ax + 1.0f, r.getCentreY() - 3.0f);
                arrow.lineTo (ax + 4.0f, r.getCentreY());
                arrow.lineTo (ax + 1.0f, r.getCentreY() + 3.0f);
                g.setColour (p.line);
                g.strokePath (arrow, juce::PathStrokeType (1.2f));
            }
        }

        void resized() override
        {
            placeTitleLock (lock);
            for (int i = 0; i < dsp::kNumModSlots; ++i)
            {
                auto r = rowArea (i);
                const int col = i / 8;
                auto& row = rows[(size_t) i];
                r.removeFromLeft (numberW);
                row.source->setBounds (r.removeFromLeft ((int) sourceW (col)).reduced (2, 3));
                row.via->setBounds (r.removeFromLeft ((int) viaW (col)).reduced (2, 3));
                r.removeFromLeft (arrowW);
                row.dest->setBounds (r.removeFromLeft ((int) destW (col)).reduced (2, 3));
                row.amount->setBounds (r.reduced (4, 2));
            }
        }

    private:
        static constexpr int headerH = 18, numberW = 22, arrowW = 18;

        juce::Rectangle<int> columnArea (int col) const
        {
            auto c = content();
            const int w = (c.getWidth() - 24) / 2;
            return col == 0 ? c.removeFromLeft (w) : c.removeFromRight (w);
        }

        float sourceW (int col) const { return (float) columnArea (col).getWidth() * 0.24f; }
        float viaW (int col) const    { return (float) columnArea (col).getWidth() * 0.19f; }
        float destW (int col) const   { return (float) columnArea (col).getWidth() * 0.27f; }

        juce::Rectangle<int> rowArea (int i) const
        {
            auto c = columnArea (i / 8).withTrimmedTop (headerH);
            const int h = c.getHeight() / 8;
            return c.withY (c.getY() + (i % 8) * h).withHeight (h);
        }

        struct Row
        {
            std::unique_ptr<Choice> source, via, dest;
            std::unique_ptr<AmountBar> amount;
        };
        std::array<Row, dsp::kNumModSlots> rows;
        LockButton lock;
    };

    class ModPage : public juce::Component
    {
    public:
        explicit ModPage (APVTS& s)
            : lfo1 (s, 0), lfo2 (s, 1), lfo3 (s, 2),
              modEnv (s, "Mod Env", locks::kModEnv, params::modA, params::modD, params::modS, params::modR),
              matrix (s)
        {
            for (auto* c : std::initializer_list<juce::Component*> { &lfo1, &lfo2, &lfo3, &modEnv, &matrix })
                addAndMakeVisible (c);
        }

        void resized() override
        {
            constexpr int gap = 10;
            auto r = getLocalBounds();
            auto top = r.removeFromTop ((int) ((float) r.getHeight() * 0.44f));
            r.removeFromTop (gap);

            const int w = (top.getWidth() - 3 * gap) / 4;
            lfo1.setBounds (top.removeFromLeft (w)); top.removeFromLeft (gap);
            lfo2.setBounds (top.removeFromLeft (w)); top.removeFromLeft (gap);
            lfo3.setBounds (top.removeFromLeft (w)); top.removeFromLeft (gap);
            modEnv.setBounds (top);
            matrix.setBounds (r);
        }

    private:
        LfoPanel lfo1, lfo2, lfo3;
        EnvPanel modEnv;
        MatrixPanel matrix;
    };
}
