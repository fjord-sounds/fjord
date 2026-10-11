#pragma once
#include "Controls.h"
#include "Displays.h"
#include "EnvGraph.h"
#include "../Parameters.h"

namespace fjord::ui
{
    class OscPanel : public Section, private juce::Timer
    {
    public:
        OscPanel (APVTS& s, int i)
            : Section ("Osc " + juce::String (i + 1)),
              waveParam (s.getRawParameterValue (params::osc (i, "wave"))),
              power  (s, params::osc (i, "on")),
              lock   (s, locks::oscGroup (i), false),
              wave   (s, params::osc (i, "wave"), waveGlyph),
              table  (s, params::osc (i, "table")),
              preview (s, i),
              retrig (s, params::osc (i, "retrig"), "Retrig"),
              octave (s, params::osc (i, "octave"), "Oct"),
              semi   (s, params::osc (i, "semi"), "Semi"),
              fine   (s, params::osc (i, "fine"), "Fine"),
              level  (s, params::osc (i, "level"), "Level"),
              pan    (s, params::osc (i, "pan"), "Pan"),
              unison (s, params::osc (i, "unison"), "Voices"),
              detune (s, params::osc (i, "detune"), "Detune"),
              spread (s, params::osc (i, "spread"), "Spread"),
              blend  (s, params::osc (i, "blend"), "Blend"),
              pw     (s, params::osc (i, "pw"), "Width"),
              wtpos  (s, params::osc (i, "wtpos"), "Position")
        {
            for (auto* c : std::initializer_list<juce::Component*> { &power, &lock, &wave, &table, &preview, &retrig, &octave, &semi, &fine,
                                                                      &level, &pan, &unison, &detune, &spread, &blend, &pw, &wtpos })
                addAndMakeVisible (c);

            // Disabled oscillators are visually dimmed.
            power.onChange = [this] (bool on)
            {
                for (auto* c : std::initializer_list<juce::Component*> { &octave, &semi, &fine, &level, &pan, &unison, &detune,
                                                                          &spread, &blend, &pw, &wtpos, &wave, &table })
                    c->setAlpha (on ? 1.0f : 0.45f);
            };
            power.onChange (power.isOn());
            updateMode (true);
            startTimerHz (10);
        }

        void resized() override
        {
            auto title = titleArea();
            power.setBounds (title.removeFromRight (18).withSizeKeepingCentre (18, 18));
            title.removeFromRight (6);
            lock.setBounds (title.removeFromRight (18).withSizeKeepingCentre (18, 18));
            title.removeFromRight (4);
            retrig.setBounds (title.removeFromRight (66).withSizeKeepingCentre (66, 18));
            title.removeFromLeft (54);
            preview.setBounds (title.reduced (4, 7));

            auto c = content();
            auto source = c.removeFromTop (28);
            wave.setBounds (source.removeFromLeft (std::min (140, source.getWidth() * 55 / 100)).reduced (0, 2));
            source.removeFromLeft (6);
            table.setBounds (source.reduced (0, 2));

            const int rowH = c.getHeight() / 2;
            layoutRow (c.removeFromTop (rowH).reduced (0, 4), { &octave, &semi, &fine, &level, &pan });
            layoutRow (c.reduced (0, 4), { &unison, &detune, &spread, &blend, &pw });
            wtpos.setBounds (pw.getBounds());
        }

    private:
        void timerCallback() override { updateMode (false); }

        // Wavetable mode: table selection + position instead of pulse width
        void updateMode (bool force)
        {
            const bool isTable = juce::roundToInt (waveParam->load()) == 4;
            if (isTable == tableMode && ! force)
                return;
            tableMode = isTable;
            table.setVisible (isTable);
            wtpos.setVisible (isTable);
            pw.setVisible (! isTable);
        }

        std::atomic<float>* waveParam;
        bool tableMode = false;
        Toggle power;
        LockButton lock;
        Segmented wave;
        Choice table;
        OscPreview preview;
        Toggle retrig;
        Knob octave, semi, fine, level, pan, unison, detune, spread, blend, pw, wtpos;
    };

    class SourcePanel : public Section
    {
    public:
        explicit SourcePanel (APVTS& s)
            : Section ("Sub / Noise"), lock (s, locks::kSubNoise, false),
              subWave (s, params::subWave), subOct (s, params::subOctave), noiseColor (s, params::noiseColor),
              sub (s, params::subLevel, "Sub"), noise (s, params::noiseLevel, "Noise")
        {
            for (auto* c : std::initializer_list<juce::Component*> { &lock, &subWave, &subOct, &noiseColor, &sub, &noise })
                addAndMakeVisible (c);
        }

        void resized() override
        {
            placeTitleLock (lock);
            auto c = content();
            auto knobs = c.removeFromTop (c.getHeight() - 84);
            layoutRow (knobs.reduced (0, 6), { &sub, &noise });
            subWave.setBounds (c.removeFromTop (26).reduced (2, 0));
            subOct.setBounds (c.removeFromTop (26).reduced (2, 0));
            noiseColor.setBounds (c.removeFromTop (26).reduced (2, 0));
        }

    private:
        LockButton lock;
        Segmented subWave, subOct, noiseColor;
        Knob sub, noise;
    };

    // Controls of one filter (filter 1 also has velocity, filter 2 the routing).
    class FilterControls : public juce::Component
    {
    public:
        FilterControls (APVTS& s, bool second)
            : type (s, second ? params::filter2Type : params::filterType),
              cutoff (s, second ? params::cutoff2 : params::cutoff, "Cutoff"),
              reso (s, second ? params::reso2 : params::reso, "Reso"),
              drive (s, second ? params::drive2 : params::drive, "Drive"),
              env (s, second ? params::filter2EnvAmt : params::filterEnvAmt, "Env"),
              key (s, second ? params::keyTrack2 : params::keyTrack, "Key")
        {
            for (auto* c : std::initializer_list<juce::Component*> { &cutoff, &reso, &drive, &env, &key })
                addAndMakeVisible (c);

            if (second)
            {
                routing = std::make_unique<Segmented> (s, params::filterRouting);
                addAndMakeVisible (*routing);
            }
            else
            {
                velocity = std::make_unique<Knob> (s, params::filterVel, "Vel");
                addAndMakeVisible (*velocity);
            }
        }

        Choice type;   // sits in the panel title row

        void resized() override
        {
            auto c = getLocalBounds();
            auto big = c.removeFromLeft (c.getWidth() / 2);
            cutoff.setBounds (big.withSizeKeepingCentre (std::min (big.getWidth(), 118), std::min (big.getHeight(), 136)));

            const int rowH = c.getHeight() / 2;
            layoutRow (c.removeFromTop (rowH).reduced (0, 4), { &reso, &drive });
            auto bottom = c.reduced (0, 4);
            if (velocity != nullptr)
            {
                layoutRow (bottom, { &env, &key, velocity.get() });
            }
            else
            {
                routing->setBounds (bottom.removeFromBottom (24));
                layoutRow (bottom, { &env, &key });
            }
        }

    private:
        Knob cutoff, reso, drive, env, key;
        std::unique_ptr<Knob> velocity;
        std::unique_ptr<Segmented> routing;
    };

    class FilterPanel : public Section
    {
    public:
        explicit FilterPanel (APVTS& s)
            : Section ("Filter"), one (s, false), two (s, true), power2 (s, params::filter2On), lock (s, locks::kFilters, false)
        {
            addAndMakeVisible (lock);
            addAndMakeVisible (one);
            addChildComponent (two);
            addAndMakeVisible (one.type);
            addChildComponent (two.type);
            addAndMakeVisible (power2);
            setMouseCursor (juce::MouseCursor::NormalCursor);
        }

        void paint (juce::Graphics& g) override
        {
            Section::paint (g);
            const auto& p = Theme::get().palette();
            for (int i = 0; i < 2; ++i)
            {
                const auto r = tabBounds (i).toFloat();
                const bool on = i == current;
                if (on)
                {
                    g.setColour (p.accentSoft);
                    g.fillRoundedRectangle (r, 4.0f);
                }
                g.setColour (on ? p.accent : p.textDim);
                drawSpacedText (g, juce::String (i + 1), r, 11.0f, juce::Justification::centred, 0.0f, true);
            }
        }

        void resized() override
        {
            const auto title = titleArea();
            const auto typeArea = title.withLeft (title.getRight() - 96).withSizeKeepingCentre (96, 24);
            one.type.setBounds (typeArea);
            two.type.setBounds (typeArea);
            power2.setBounds (tabBounds (1).translated (30, 0).withSizeKeepingCentre (18, 18));
            lock.setBounds (power2.getRight() + 6, typeArea.getCentreY() - 9, 18, 18);
            one.setBounds (content());
            two.setBounds (content());
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            for (int i = 0; i < 2; ++i)
                if (tabBounds (i).contains (e.getPosition()))
                    show (i);
        }

        void show (int i)
        {
            current = i;
            one.setVisible (i == 0);
            one.type.setVisible (i == 0);
            two.setVisible (i == 1);
            two.type.setVisible (i == 1);
            repaint();
        }

    private:
        juce::Rectangle<int> tabBounds (int i) const
        {
            const auto t = titleArea();
            return { t.getX() + 72 + i * 28, t.getCentreY() - 11, 24, 22 };
        }

        FilterControls one, two;
        Toggle power2;
        LockButton lock;
        int current = 0;
    };

    class EnvPanel : public Section
    {
    public:
        EnvPanel (APVTS& s, const juce::String& title, int lockGroup, const char* a, const char* d, const char* su, const char* r,
                  const char* extra = nullptr, const juce::String& extraLabel = {})
            : Section (title), lock (s, lockGroup, false), graph (s, a, d, su, r),
              attack (s, a, "A"), decay (s, d, "D"), sustain (s, su, "S"), release (s, r, "R")
        {
            for (auto* c : std::initializer_list<juce::Component*> { &lock, &graph, &attack, &decay, &sustain, &release })
                addAndMakeVisible (c);
            if (extra != nullptr)
            {
                side = std::make_unique<Knob> (s, extra, extraLabel);
                addAndMakeVisible (*side);
            }
        }

        void resized() override
        {
            placeTitleLock (lock);
            auto c = content();
            auto top = c.removeFromTop (c.getHeight() * 52 / 100);
            if (side != nullptr)
                side->setBounds (top.removeFromRight (std::min (84, top.getWidth() / 3)).reduced (2, 4));
            graph.setBounds (top);
            layoutRow (c.reduced (0, 2), { &attack, &decay, &sustain, &release });
        }

    private:
        LockButton lock;
        EnvGraph graph;
        Knob attack, decay, sustain, release;
        std::unique_ptr<Knob> side;
    };

    class VoicePanel : public Section
    {
    public:
        explicit VoicePanel (APVTS& s)
            : Section ("Voice"), lock (s, locks::kVoice, false),
              mode (s, params::voiceMode),
              glide (s, params::glide, "Glide"), poly (s, params::polyphony, "Poly"),
              bend (s, params::bendRange, "Bend"), analog (s, params::analog, "Analog")
        {
            for (auto* c : std::initializer_list<juce::Component*> { &lock, &mode, &glide, &poly, &bend, &analog })
                addAndMakeVisible (c);
        }

        void resized() override
        {
            placeTitleLock (lock);
            auto c = content();
            mode.setBounds (c.removeFromTop (26).reduced (2, 0));
            c.removeFromTop (6);
            const int rowH = c.getHeight() / 2;
            layoutRow (c.removeFromTop (rowH).reduced (0, 4), { &glide, &poly });
            layoutRow (c.reduced (0, 4), { &bend, &analog });
        }

    private:
        LockButton lock;
        Segmented mode;
        Knob glide, poly, bend, analog;
    };

    // All oscillators in a row, four visible at a time; the strip scrolls horizontally to oscillators 5-8.
    class OscStrip : public juce::Component
    {
    public:
        explicit OscStrip (APVTS& s)
        {
            for (int i = 0; i < dsp::kNumOsc; ++i)
                content.addAndMakeVisible (panels.add (new OscPanel (s, i)));
            viewport.setViewedComponent (&content, false);
            viewport.setScrollBarsShown (false, true);
            viewport.setScrollBarThickness (6);
            viewport.setScrollOnDragMode (juce::Viewport::ScrollOnDragMode::never);
            addAndMakeVisible (viewport);
        }

        void resized() override
        {
            viewport.setBounds (getLocalBounds());
            const int panelH = getHeight() - viewport.getScrollBarThickness() - 4;
            const int panelW = (getWidth() - (visible - 1) * gap) / visible;
            content.setSize (dsp::kNumOsc * panelW + (dsp::kNumOsc - 1) * gap, panelH);
            for (int i = 0; i < panels.size(); ++i)
                panels[i]->setBounds (i * (panelW + gap), 0, panelW, panelH);
        }

    private:
        static constexpr int visible = 4, gap = 10;
        juce::Viewport viewport;
        juce::Component content;
        juce::OwnedArray<OscPanel> panels;
    };

    // Oscillator page: all eight oscillators at once, four across and two rows.
    class OscPage : public juce::Component
    {
    public:
        explicit OscPage (APVTS& s)
        {
            for (int i = 0; i < dsp::kNumOsc; ++i)
                addAndMakeVisible (panels.add (new OscPanel (s, i)));
        }

        void resized() override
        {
            constexpr int gap = 10, columns = 4;
            const int w = (getWidth() - (columns - 1) * gap) / columns;
            const int h = (getHeight() - gap) / 2;
            for (int i = 0; i < panels.size(); ++i)
                panels[i]->setBounds ((i % columns) * (w + gap), (i / columns) * (h + gap), w, h);
        }

    private:
        juce::OwnedArray<OscPanel> panels;
    };

    class SynthPage : public juce::Component
    {
    public:
        explicit SynthPage (APVTS& s)
            : oscillators (s), source (s), filter (s),
              filterEnv (s, "Filter Env", locks::kFilterEnv, params::fltA, params::fltD, params::fltS, params::fltR, params::filterEnvAmt, "Amount"),
              ampEnv (s, "Amp Env", locks::kAmpEnv, params::ampA, params::ampD, params::ampS, params::ampR, params::ampVel, "Velocity"),
              voice (s)
        {
            for (auto* c : std::initializer_list<juce::Component*> { &oscillators, &source, &filter, &filterEnv, &ampEnv, &voice })
                addAndMakeVisible (c);
        }

        void resized() override
        {
            constexpr int gap = 10;
            auto r = getLocalBounds();
            // the strip's scrollbar sits below the panels, inside the gap
            oscillators.setBounds (r.removeFromTop ((r.getHeight() - gap) / 2 + 14 + 6));
            r.removeFromTop (gap - 6);

            const int unit = (r.getWidth() - 4 * gap) / 20;
            source.setBounds (r.removeFromLeft (unit * 3));    r.removeFromLeft (gap);
            filter.setBounds (r.removeFromLeft (unit * 6));    r.removeFromLeft (gap);
            filterEnv.setBounds (r.removeFromLeft (unit * 4)); r.removeFromLeft (gap);
            ampEnv.setBounds (r.removeFromLeft (unit * 4));    r.removeFromLeft (gap);
            voice.setBounds (r);
        }

    private:
        OscStrip oscillators;
        SourcePanel source;
        FilterPanel filter;
        EnvPanel filterEnv, ampEnv;
        VoicePanel voice;
    };
}
