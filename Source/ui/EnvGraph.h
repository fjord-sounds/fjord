#pragma once
#include "Controls.h"

namespace fjord::ui
{
    // Interactive ADSR graph: drag the points directly.
    //   point 1 (peak)   → attack (horizontal)
    //   point 2 (knee)   → decay (horizontal) and sustain (vertical)
    //   point 3 (end)    → release (horizontal)
    class EnvGraph : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        EnvGraph (APVTS& s, const char* a, const char* d, const char* su, const char* r)
        {
            const char* ids[] = { a, d, su, r };
            for (size_t i = 0; i < 4; ++i)
            {
                params[i] = s.getParameter (ids[i]);
                attachments[i] = std::make_unique<juce::ParameterAttachment> (
                    *params[i], [this, i] (float) { values[i] = params[i]->getValue(); repaint(); }, nullptr);
                attachments[i]->sendInitialUpdate();
            }
            setTooltip ("Drag the points to shape the envelope. Double-click resets.");
        }

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            const auto r = plotArea();
            const auto pts = points();

            g.setColour (p.line);
            g.fillRect (r.getX(), r.getBottom(), r.getWidth(), 1.0f);

            // Curve: attack slightly convex, decay/release exponential (like the engine)
            juce::Path path;
            path.startNewSubPath (pts[0]);
            path.quadraticTo ({ pts[0].x + (pts[1].x - pts[0].x) * 0.45f, pts[1].y + (pts[0].y - pts[1].y) * 0.15f }, pts[1]);
            path.quadraticTo ({ pts[1].x + (pts[2].x - pts[1].x) * 0.15f, pts[2].y }, pts[2]);
            path.lineTo (pts[3]);
            path.quadraticTo ({ pts[3].x + (pts[4].x - pts[3].x) * 0.15f, pts[4].y }, pts[4]);

            juce::Path fill (path);
            fill.lineTo (pts[0]);
            fill.closeSubPath();
            g.setColour (p.accent.withAlpha (0.12f));
            g.fillPath (fill);
            g.setColour (p.accent);
            g.strokePath (path, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            // Dashed sustain line
            g.setColour (p.textDim.withAlpha (0.5f));
            const float dashes[] = { 3.0f, 3.0f };
            g.drawDashedLine ({ pts[2], pts[3] }, dashes, 2, 1.0f);

            for (int i = 1; i <= 3; ++i)
            {
                const auto pt = i == 3 ? pts[4] : pts[(size_t) i];
                const bool active = i == hover || i == dragging;
                const float rad = active ? 5.5f : 4.0f;
                g.setColour (p.panel);
                g.fillEllipse (pt.x - rad, pt.y - rad, rad * 2.0f, rad * 2.0f);
                g.setColour (active ? p.accent : p.text);
                g.drawEllipse (pt.x - rad, pt.y - rad, rad * 2.0f, rad * 2.0f, 1.6f);
            }

            if (dragging > 0)
            {
                juce::String text;
                if (dragging == 1) text = "A " + params[0]->getCurrentValueAsText();
                if (dragging == 2) text = "D " + params[1]->getCurrentValueAsText() + "   S " + params[2]->getCurrentValueAsText();
                if (dragging == 3) text = "R " + params[3]->getCurrentValueAsText();
                g.setColour (p.text);
                g.setFont (font (11.0f, Weight::Medium));
                g.drawText (text, getLocalBounds().removeFromTop (14), juce::Justification::centredRight);
            }
        }

        void mouseMove (const juce::MouseEvent& e) override
        {
            const int h = handleAt (e.position);
            if (h != hover) { hover = h; repaint(); }
            setMouseCursor (h > 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        }

        void mouseExit (const juce::MouseEvent&) override { hover = 0; repaint(); }

        void mouseDown (const juce::MouseEvent& e) override
        {
            dragging = handleAt (e.position);
            if (dragging == 1) attachments[0]->beginGesture();
            if (dragging == 2) { attachments[1]->beginGesture(); attachments[2]->beginGesture(); }
            if (dragging == 3) attachments[3]->beginGesture();
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (dragging == 0) return;
            const auto r = plotArea();
            const auto pts = points();
            const float seg = segmentWidth();
            auto setNorm = [this] (size_t i, float norm)
            {
                norm = std::clamp (norm, 0.0f, 1.0f);
                attachments[i]->setValueAsPartOfGesture (params[i]->convertFrom0to1 (norm));
            };

            if (dragging == 1) setNorm (0, (e.position.x - pts[0].x) / seg);
            if (dragging == 2)
            {
                setNorm (1, (e.position.x - pts[1].x) / seg);
                setNorm (2, (r.getBottom() - e.position.y) / r.getHeight());
            }
            if (dragging == 3) setNorm (3, (e.position.x - pts[3].x) / seg);
        }

        void mouseUp (const juce::MouseEvent&) override
        {
            if (dragging == 1) attachments[0]->endGesture();
            if (dragging == 2) { attachments[1]->endGesture(); attachments[2]->endGesture(); }
            if (dragging == 3) attachments[3]->endGesture();
            dragging = 0;
            repaint();
        }

        void mouseDoubleClick (const juce::MouseEvent& e) override
        {
            const int h = handleAt (e.position);
            auto reset = [this] (size_t i) { attachments[i]->setValueAsCompleteGesture (params[i]->convertFrom0to1 (params[i]->getDefaultValue())); };
            if (h == 1) reset (0);
            if (h == 2) { reset (1); reset (2); }
            if (h == 3) reset (3);
        }

    private:
        juce::Rectangle<float> plotArea() const { return getLocalBounds().toFloat().reduced (7.0f, 8.0f).withTrimmedTop (8.0f); }
        float segmentWidth() const { return plotArea().getWidth() / 3.4f; }

        // Points: start, peak, decay end, sustain end, release end
        std::array<juce::Point<float>, 5> points() const
        {
            const auto r = plotArea();
            const float seg = segmentWidth();
            const float x1 = r.getX() + values[0] * seg;
            const float x2 = x1 + values[1] * seg;
            const float x3 = x2 + seg * 0.4f;
            const float x4 = x3 + values[3] * seg;
            const float sy = r.getBottom() - values[2] * r.getHeight();
            return { juce::Point<float> { r.getX(), r.getBottom() }, { x1, r.getY() }, { x2, sy }, { x3, sy }, { x4, r.getBottom() } };
        }

        int handleAt (juce::Point<float> pos) const
        {
            const auto pts = points();
            const juce::Point<float> handles[] = { pts[1], pts[2], pts[4] };
            int best = 0;
            float bestDist = 12.0f;
            for (int i = 0; i < 3; ++i)
            {
                const float d = handles[i].getDistanceFrom (pos);
                if (d < bestDist) { bestDist = d; best = i + 1; }
            }
            return best;
        }

        std::array<juce::RangedAudioParameter*, 4> params {};
        std::array<std::unique_ptr<juce::ParameterAttachment>, 4> attachments;
        std::array<float, 4> values {};
        int hover = 0, dragging = 0;
    };
}
