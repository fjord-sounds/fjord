#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Theme.h"
#include "../Parameters.h"
#include "../dsp/Wavetable.h"
#include "../dsp/Modulation.h"

namespace fjord::ui
{
    using APVTS = juce::AudioProcessorValueTreeState;

    // A ring buffer written by the audio thread and read by the UI (lock-free).
    struct ScopeBuffer
    {
        static constexpr int size = 4096;
        std::array<float, size> data {};
        std::atomic<int> writePos { 0 };
        std::atomic<double> sampleRate { 48000.0 };   // for the spectrum display

        void push (const float* l, const float* r, int n)
        {
            int w = writePos.load (std::memory_order_relaxed);
            for (int i = 0; i < n; ++i)
            {
                data[(size_t) w] = 0.5f * (l[i] + r[i]);
                w = (w + 1) & (size - 1);
            }
            writePos.store (w, std::memory_order_release);
        }
    };

    // Base class: polls parameters and repaints only when something changed.
    class PolledDisplay : public juce::Component, private juce::Timer
    {
    public:
        PolledDisplay() { startTimerHz (24); }

    protected:
        virtual juce::String fingerprint() const = 0;

    private:
        void timerCallback() override
        {
            const auto f = fingerprint();
            if (f != last) { last = f; repaint(); }
        }
        juce::String last;
    };

    inline void drawCurve (juce::Graphics& g, juce::Rectangle<float> r, const std::function<float (float)>& fn,
                           juce::Colour colour, bool fill)
    {
        juce::Path p;
        const int steps = std::max (32, (int) r.getWidth());
        for (int i = 0; i <= steps; ++i)
        {
            const float x = (float) i / (float) steps;
            const float y = r.getCentreY() - std::clamp (fn (x), -1.0f, 1.0f) * r.getHeight() * 0.5f;
            if (i == 0) p.startNewSubPath (r.getX(), y);
            else        p.lineTo (r.getX() + x * r.getWidth(), y);
        }

        if (fill)
        {
            juce::Path area (p);
            area.lineTo (r.getRight(), r.getCentreY());
            area.lineTo (r.getX(), r.getCentreY());
            area.closeSubPath();
            g.setColour (colour.withAlpha (0.12f));
            g.fillPath (area);
        }
        g.setColour (colour);
        g.strokePath (p, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Preview of the current oscillator waveform (including wavetable position).
    class OscPreview : public PolledDisplay
    {
    public:
        OscPreview (APVTS& s, int index)
            : wave (s.getRawParameterValue (params::osc (index, "wave"))),
              table (s.getRawParameterValue (params::osc (index, "table"))),
              pos (s.getRawParameterValue (params::osc (index, "wtpos"))),
              pw (s.getRawParameterValue (params::osc (index, "pw"))),
              on (s.getRawParameterValue (params::osc (index, "on"))) {}

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            auto r = getLocalBounds().toFloat().reduced (2.0f, 4.0f);
            g.setColour (p.line);
            g.fillRect (r.getX(), r.getCentreY(), r.getWidth(), 1.0f);

            const int w = juce::roundToInt (wave->load());
            const int t = juce::roundToInt (table->load());
            const float position = pos->load(), width = pw->load();
            const auto& bank = dsp::WavetableBank::get();

            auto fn = [&] (float xx) -> float
            {
                const float x = xx * 2.0f - std::floor (xx * 2.0f);   // two cycles
                switch (w)
                {
                    case 0:  return std::sin (x * juce::MathConstants<float>::twoPi);
                    case 1:  return x < 0.5f ? 4.0f * x - 1.0f : 3.0f - 4.0f * x;
                    case 2:  return 2.0f * x - 1.0f;
                    case 3:  return x < width ? 1.0f : -1.0f;
                    default: return bank.sampleForDisplay (t, position, x);
                }
            };
            drawCurve (g, r, fn, on->load() > 0.5f ? p.accent : p.textDim, true);
        }

    private:
        juce::String fingerprint() const override
        {
            return juce::String (wave->load()) + ":" + juce::String (table->load()) + ":" + juce::String (pos->load(), 3)
                 + ":" + juce::String (pw->load(), 3) + ":" + juce::String (on->load()) + (Theme::get().isDark() ? "d" : "l");
        }

        std::atomic<float>* wave, *table, *pos, *pw, *on;
    };

    // Preview of the LFO shape (two cycles).
    class LfoPreview : public PolledDisplay
    {
    public:
        LfoPreview (APVTS& s, int index) : shape (s.getRawParameterValue (params::lfo (index, "shape"))), seed ((uint32_t) (1000 + index)) {}

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            auto r = getLocalBounds().toFloat().reduced (2.0f, 6.0f);
            g.setColour (p.line);
            g.fillRect (r.getX(), r.getCentreY(), r.getWidth(), 1.0f);
            const auto s = (dsp::LfoShape) juce::roundToInt (shape->load());
            drawCurve (g, r, [s, this] (float x) { return dsp::lfoValue (s, x * 2.0, seed); }, p.accent, true);
        }

    private:
        juce::String fingerprint() const override { return juce::String (shape->load()) + (Theme::get().isDark() ? "d" : "l"); }
        std::atomic<float>* shape;
        uint32_t seed;
    };

    // Oscilloscope of the output signal, triggered on rising zero crossings.
    class Scope : public juce::Component, private juce::Timer
    {
    public:
        explicit Scope (ScopeBuffer& b) : buffer (b) { startTimerHz (30); }

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            auto r = getLocalBounds().toFloat();
            g.setColour (p.line);
            g.fillRect (r.getX(), r.getCentreY(), r.getWidth(), 1.0f);

            constexpr int window = 1024;
            const int end = buffer.writePos.load (std::memory_order_acquire);
            auto at = [&] (int i) { return buffer.data[(size_t) ((i + ScopeBuffer::size * 2) & (ScopeBuffer::size - 1))]; };

            // Trigger: last rising zero crossing before the visible window
            int start = end - window - 1;
            for (int i = end - window - 1; i > end - ScopeBuffer::size + window; --i)
                if (at (i - 1) <= 0.0f && at (i) > 0.0f) { start = i; break; }

            float peak = 0.05f;
            for (int i = 0; i < window; ++i) peak = std::max (peak, std::abs (at (start + i)));
            smoothedPeak = std::max (peak, smoothedPeak * 0.9f);

            juce::Path path;
            for (int i = 0; i < window; ++i)
            {
                const float x = r.getX() + r.getWidth() * (float) i / (float) (window - 1);
                const float y = r.getCentreY() - at (start + i) / smoothedPeak * r.getHeight() * 0.42f;
                if (i == 0) path.startNewSubPath (x, y); else path.lineTo (x, y);
            }
            g.setColour (p.accent);
            g.strokePath (path, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

    private:
        void timerCallback() override { if (isShowing()) repaint(); }
        ScopeBuffer& buffer;
        float smoothedPeak = 0.1f;
    };
}
