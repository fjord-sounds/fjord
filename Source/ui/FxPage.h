#pragma once
#include "SynthPage.h"
#include "../fx/Effects.h"
#include "../presets/FxPresetManager.h"
#include "../presets/FxModulePresets.h"
#include "PresetBrowser.h"
#include "EqEditor.h"

namespace fjord::ui
{
    // Live graphic per effect (reacts to the knobs)
    class FxVisual : public PolledDisplay
    {
    public:
        FxVisual (APVTS& s, int fxType) : type ((fx::FxType) fxType)
        {
            for (auto* p : s.processor.getParameters())
                if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (p))
                    if (r->getParameterID().startsWith (params::fx (fxType, "")))
                        refs[r->getParameterID().fromLastOccurrenceOf ("_", false, false)] = s.getRawParameterValue (r->getParameterID());
        }

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            auto r = getLocalBounds().toFloat().reduced (4.0f, 8.0f);
            const auto c = v ("on") > 0.5f ? p.accent : p.textDim;
            g.setColour (p.line);
            g.fillRect (r.getX(), r.getCentreY(), r.getWidth(), 1.0f);
            using fx::FxType;

            switch (type)
            {
                case FxType::Drive:
                {
                    const int t = juce::roundToInt (v ("type"));
                    const float gain = std::pow (10.0f, v ("amount") * 36.0f / 20.0f), comp = 1.0f / (1.0f + v ("amount") * 2.5f);
                    auto sq = r.withSizeKeepingCentre (r.getHeight() * 1.6f, r.getHeight());
                    g.setColour (p.line);
                    g.fillRect (sq.getCentreX(), sq.getY(), 1.0f, sq.getHeight());
                    drawCurve (g, sq, [&] (float x) { return fx::Drive::shape (t, (x * 2.0f - 1.0f) * gain * 0.6f) * comp * 1.6f; }, c, false);
                    break;
                }
                case FxType::Crush:
                {
                    const float q = std::pow (2.0f, v ("bits") - 1.0f) / 4.0f, rate = v ("rate");
                    drawCurve (g, r, [&] (float x)
                    {
                        const float step = rate / 400.0f;
                        const float xs = step > 0.003f ? std::floor (x / step) * step : x;
                        return std::round (std::sin (xs * kTwoPiF * 2.0f) * q) / q * 0.85f;
                    }, c, true);
                    break;
                }
                case FxType::Comp:
                {
                    const float thr = v ("thresh"), ratio = v ("ratio"), makeup = v ("makeup");
                    auto sq = r.withSizeKeepingCentre (r.getHeight(), r.getHeight()).withX (r.getX());
                    g.setColour (p.line);
                    g.drawRect (sq, 1.0f);
                    juce::Path path;
                    for (int i = 0; i <= 60; ++i)
                    {
                        const float in = -60.0f + (float) i;
                        const float out = (in > thr ? thr + (in - thr) / ratio : in) + makeup;
                        const float x = sq.getX() + (float) i / 60.0f * sq.getWidth();
                        const float y = sq.getBottom() - std::clamp ((out + 60.0f) / 60.0f, 0.0f, 1.0f) * sq.getHeight();
                        if (i == 0) path.startNewSubPath (x, y); else path.lineTo (x, y);
                    }
                    g.setColour (c);
                    g.strokePath (path, juce::PathStrokeType (1.8f));
                    break;
                }
                case FxType::Chorus: case FxType::Phaser: case FxType::Flanger:
                {
                    const float rate = v ("rate"), depth = v ("depth");
                    const float cycles = std::clamp (rate * 2.0f, 0.5f, 6.0f);
                    const float offset = type == FxType::Chorus ? 0.5f : 0.25f;
                    drawCurve (g, r, [&] (float x) { return std::sin ((x * cycles) * kTwoPiF) * depth * 0.9f; }, c, false);
                    drawCurve (g, r, [&] (float x) { return std::sin ((x * cycles + offset) * kTwoPiF) * depth * 0.9f; },
                               c.withAlpha (0.45f), false);
                    break;
                }
                case FxType::Delay:
                {
                    const bool synced = v ("sync") > 0.5f, ping = v ("pingpong") > 0.5f;
                    const float seconds = synced
                        ? (float) dsp::divisions[(size_t) std::clamp (juce::roundToInt (v ("div")), 0, dsp::numDivisions - 1)].beats * 0.5f
                        : v ("time") * 0.001f;
                    const float fb = v ("feedback"), span = 2.0f;
                    float level = 1.0f;
                    for (int k = 0; k < 64; ++k)
                    {
                        const float t = seconds * (float) k;
                        if (t > span || level < 0.01f) break;
                        const float x = r.getX() + t / span * r.getWidth();
                        const float h = level * r.getHeight() * 0.48f;
                        const bool up = ! ping || k % 2 == 0;
                        g.setColour (k == 0 ? p.textDim : c.withAlpha (0.35f + 0.65f * level));
                        g.fillRoundedRectangle (x - 2.0f, up ? r.getCentreY() - h : r.getCentreY(), 4.0f, h, 2.0f);
                        if (k > 0) level *= fb;
                        if (k == 0) level = 0.9f;
                    }
                    break;
                }
                case FxType::Reverb:
                {
                    const float pre = v ("predelay"), decay = v ("decay"), size = v ("size"), span = 6.0f;
                    juce::Path env;
                    env.startNewSubPath (r.getX(), r.getBottom());
                    const float preX = r.getX() + pre / span * r.getWidth();
                    env.lineTo (preX, r.getBottom());
                    const float attack = 0.02f + 0.15f * size;
                    for (int i = 0; i <= 200; ++i)
                    {
                        const float t = (float) i / 200.0f * (span - pre);
                        const float a = std::min (1.0f, t / attack) * std::pow (10.0f, -3.0f * t / decay);
                        env.lineTo (preX + t / span * r.getWidth(), r.getBottom() - a * r.getHeight() * 0.95f);
                    }
                    env.lineTo (r.getRight(), r.getBottom());
                    g.setColour (c.withAlpha (0.14f));
                    g.fillPath (env);
                    g.setColour (c);
                    g.strokePath (env, juce::PathStrokeType (1.6f));
                    break;
                }
                case FxType::Width:
                {
                    const float w = v ("amount");
                    const auto centre = r.getCentre();
                    const float h = r.getHeight() * 0.9f;
                    g.setColour (c.withAlpha (0.14f));
                    g.fillEllipse (juce::Rectangle<float> (std::max (6.0f, h * 1.1f * w), h).withCentre (centre));
                    g.setColour (c);
                    g.drawEllipse (juce::Rectangle<float> (std::max (6.0f, h * 1.1f * w), h).withCentre (centre), 1.6f);
                    g.setColour (p.textDim);
                    drawSpacedText (g, "L", r.withWidth (20.0f), 11.0f, juce::Justification::centred);
                    drawSpacedText (g, "R", r.withLeft (r.getRight() - 20.0f), 11.0f, juce::Justification::centred);
                    break;
                }
                case FxType::Eq: case FxType::Count: break;
            }
        }

    private:
        static constexpr float kTwoPiF = juce::MathConstants<float>::twoPi;

        float v (const juce::String& key) const
        {
            const auto it = refs.find (key);
            return it != refs.end() ? it->second->load() : 0.0f;
        }

        juce::String fingerprint() const override
        {
            juce::String s;
            for (const auto& [k, r] : refs) s << juce::String (r->load(), 3) << ":";
            return s + (Theme::get().isDark() ? "d" : "l");
        }

        fx::FxType type;
        std::map<juce::String, std::atomic<float>*> refs;
    };

    // Compressor gain reduction meter
    class ReductionMeter : public juce::Component, private juce::Timer
    {
    public:
        explicit ReductionMeter (std::function<float()> source) : read (std::move (source)) { startTimerHz (30); }

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            auto r = getLocalBounds().toFloat();
            g.setColour (p.textDim);
            drawSpacedText (g, "GAIN REDUCTION", r.removeFromTop (18.0f), 9.5f, juce::Justification::centredLeft, 0.18f);
            auto bar = r.removeFromTop (8.0f);
            g.setColour (p.track);
            g.fillRoundedRectangle (bar, 3.0f);
            const float frac = std::clamp (shown / 24.0f, 0.0f, 1.0f);
            g.setColour (p.accent);
            g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * frac), 3.0f);
            g.setColour (p.textDim);
            g.setFont (font (11.0f));
            g.drawText (juce::String (shown, 1) + " dB", r.removeFromTop (20.0f), juce::Justification::centredLeft);
        }

    private:
        void timerCallback() override
        {
            const float now = read();
            shown = now > shown ? now : shown * 0.85f + now * 0.15f;
            repaint();
        }

        std::function<float()> read;
        float shown = 0.0f;
    };

    // Detail view of an effect: title with on/off, optional selector/toggles, one row of large knobs.
    class FxDetail : public Section, private juce::Timer
    {
    public:
        FxDetail (APVTS& s, int type, std::function<float()> compReduction, ScopeBuffer* scope)
            : Section (fx::fxNames()[type]), state (s), fxType (type), power (s, params::fx (type, "on")), lock (s, locks::effectGroup (type))
        {
            addAndMakeVisible (power);
            addAndMakeVisible (lock);
            addAndMakeVisible (modulePresets);
            modulePresets.setTooltip ("Presets for this effect only – the rest of the chain stays as it is");
            modulePresets.onChange = [this] { modulePresetChosen(); };
            refreshPresetMenu();
            using fx::FxType;
            auto knob = [&] (const char* id, const char* label)
            {
                knobs.push_back (std::make_unique<Knob> (s, params::fx (type, id), label));
                addAndMakeVisible (*knobs.back());
            };

            switch ((FxType) type)
            {
                case FxType::Drive:
                    selector = std::make_unique<Segmented> (s, params::fx (type, "type"));
                    knob ("amount", "Amount"); knob ("tone", "Tone"); knob ("mix", "Mix");
                    description = "Analog-style saturation with 2x oversampling. Tape is soft and warm, Tube adds even harmonics.";
                    break;
                case FxType::Crush:
                    knob ("bits", "Bits"); knob ("rate", "Downsample"); knob ("mix", "Mix");
                    description = "Lo-fi bit depth and sample rate reduction.";
                    break;
                case FxType::Eq:
                    display = std::make_unique<EqEditor> (s, scope);
                    break;
                case FxType::Comp:
                    knob ("thresh", "Threshold"); knob ("ratio", "Ratio"); knob ("attack", "Attack");
                    knob ("release", "Release"); knob ("makeup", "Makeup"); knob ("mix", "Mix");
                    description = "Smooth stereo-linked compressor. Lower the mix for parallel compression.";
                    meter = std::make_unique<ReductionMeter> (std::move (compReduction));
                    break;
                case FxType::Chorus:
                    knob ("rate", "Rate"); knob ("depth", "Depth"); knob ("mix", "Mix");
                    description = "Classic stereo ensemble in the style of 80s poly synths. Wide, soft and lush.";
                    break;
                case FxType::Phaser:
                    knob ("rate", "Rate"); knob ("depth", "Depth"); knob ("feedback", "Feedback"); knob ("mix", "Mix");
                    description = "Six-stage stereo phaser with feedback.";
                    break;
                case FxType::Flanger:
                    knob ("rate", "Rate"); knob ("depth", "Depth"); knob ("feedback", "Feedback"); knob ("mix", "Mix");
                    description = "Through-zero style jet sweeps.";
                    break;
                case FxType::Delay:
                    syncParam = s.getRawParameterValue (params::fx (type, "sync"));
                    toggles.push_back (std::make_unique<Toggle> (s, params::fx (type, "sync"), "Sync"));
                    toggles.push_back (std::make_unique<Toggle> (s, params::fx (type, "pingpong"), "Ping Pong"));
                    knob ("time", "Time"); knob ("feedback", "Feedback"); knob ("damp", "Damp");
                    knob ("duck", "Ducking"); knob ("mix", "Mix");
                    division = std::make_unique<Knob> (s, params::fx (type, "div"), "Time");
                    addAndMakeVisible (*division);
                    description = "Tempo-synced stereo / ping-pong delay with tape-style damping and ducking.";
                    break;
                case FxType::Reverb:
                    knob ("size", "Size"); knob ("decay", "Decay"); knob ("damp", "Damp");
                    knob ("predelay", "Pre-Delay"); knob ("width", "Width"); knob ("mix", "Mix");
                    description = "Lush algorithmic reverb: eight modulated delay lines, from small rooms to endless halls.";
                    break;
                case FxType::Width:
                    knob ("amount", "Width");
                    description = "Mid/side stereo width. 0 % is mono, 200 % is extra wide.";
                    break;
                case FxType::Count: break;
            }

            if (display == nullptr)
                display = std::make_unique<FxVisual> (s, type);
            if (meter != nullptr) addAndMakeVisible (*meter);
            if (selector != nullptr) addAndMakeVisible (*selector);
            for (auto& t : toggles) addAndMakeVisible (*t);
            if (display != nullptr) addAndMakeVisible (*display);
            updateSync();
            startTimerHz (10);
        }

        void paint (juce::Graphics& g) override
        {
            Section::paint (g);
            if (description.isNotEmpty())
            {
                g.setColour (Theme::get().palette().textDim);
                g.setFont (font (13.0f));
                g.drawFittedText (description, descriptionArea, juce::Justification::centredLeft, 2);
            }
        }

        void resized() override
        {
            auto title = titleArea();
            power.setBounds (title.removeFromRight (18).withSizeKeepingCentre (18, 18));
            title.removeFromRight (12);
            lock.setBounds (title.removeFromRight (78).withSizeKeepingCentre (78, 18));
            title.removeFromRight (12);
            modulePresets.setBounds (title.removeFromRight (200).withSizeKeepingCentre (200, 24));

            auto c = content().reduced (8, 0);
            if (knobs.empty() && description.isEmpty())   // EQ: the editor brings its own controls
            {
                if (display != nullptr) display->setBounds (c.withTrimmedTop (4));
                return;
            }
            auto top = c.removeFromTop (juce::jmax (80, c.getHeight() - 170));
            infoArea = top.withTrimmedTop (8);

            auto controls = top.removeFromTop (30);
            if (selector != nullptr) selector->setBounds (controls.removeFromLeft (320).reduced (0, 3));
            for (auto& t : toggles) t->setBounds (controls.removeFromLeft (120).reduced (0, 6));
            if (selector != nullptr || ! toggles.empty())
                infoArea = top.withTrimmedTop (8);
            descriptionArea = infoArea.removeFromBottom (description.isNotEmpty() ? 36 : 0);
            if (meter != nullptr) meter->setBounds (infoArea.removeFromRight (220).withSizeKeepingCentre (200, 50));
            if (display != nullptr) display->setBounds (infoArea.reduced (0, 4));

            auto row = c.reduced (0, 10);
            const int w = std::min (150, row.getWidth() / std::max (1, (int) knobs.size()));
            auto line = row.withSizeKeepingCentre (w * (int) knobs.size(), row.getHeight());
            for (auto& k : knobs)
                k->setBounds (line.removeFromLeft (w).reduced (10, 0));
            if (division != nullptr && ! knobs.empty()) division->setBounds (knobs.front()->getBounds());
        }

    private:
        static constexpr int userIdBase = 1000, saveId = 5000;

        void refreshPresetMenu()
        {
            modulePresets.clear (juce::dontSendNotification);
            const auto& list = FxModulePresets::get().getPresets (fxType);
            juce::String section;
            for (int i = 0; i < (int) list.size(); ++i)
            {
                if (list[(size_t) i].category != section)
                {
                    section = list[(size_t) i].category;
                    modulePresets.addSectionHeading (section);
                }
                modulePresets.addItem (list[(size_t) i].name, (list[(size_t) i].file == juce::File() ? 1 : userIdBase) + i);
            }
            modulePresets.addSeparator();
            modulePresets.addItem (utf8 ("Save current settings …"), saveId);
            updatePresetName (true);
        }

        // The menu shows the name of the preset the current settings match, otherwise "<Effect> presets".
        void updatePresetName (bool force = false, int preferred = -1)
        {
            const auto& list = FxModulePresets::get().getPresets (fxType);
            if (preferred < 0) preferred = shownPreset;
            int match = -1;
            if (juce::isPositiveAndBelow (preferred, (int) list.size())
                    && FxModulePresets::matches (state, fxType, list[(size_t) preferred]))
                match = preferred;
            else
                for (int i = 0; i < (int) list.size() && match < 0; ++i)
                    if (FxModulePresets::matches (state, fxType, list[(size_t) i]))
                        match = i;
            if (match == shownPreset && ! force) return;
            shownPreset = match;
            modulePresets.setTextWhenNothingSelected (match >= 0 ? list[(size_t) match].name
                                                                 : fx::fxNames()[fxType] + " presets");
        }

        void modulePresetChosen()
        {
            const int id = modulePresets.getSelectedId();
            modulePresets.setSelectedId (0, juce::dontSendNotification);
            if (id == saveId)
                return promptSave();
            const int index = id >= userIdBase ? id - userIdBase : id - 1;
            const auto& list = FxModulePresets::get().getPresets (fxType);
            if (juce::isPositiveAndBelow (index, (int) list.size()))
            {
                FxModulePresets::apply (state, fxType, list[(size_t) index]);
                updatePresetName (true, index);
            }
        }

        void promptSave()
        {
            auto* w = new juce::AlertWindow ("Save " + fx::fxNames()[fxType] + " preset",
                                             "Saves the current " + fx::fxNames()[fxType] + " settings.", juce::MessageBoxIconType::NoIcon);
            w->addTextEditor ("name", "My " + fx::fxNames()[fxType]);
            w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
            w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            juce::Component::SafePointer<FxDetail> self (this);
            w->enterModalState (true, juce::ModalCallbackFunction::create ([self, w] (int result)
            {
                if (result == 1 && self != nullptr)
                {
                    FxModulePresets::get().saveUserPreset (self->state, self->fxType, w->getTextEditorContents ("name"));
                    self->refreshPresetMenu();
                }
            }), true);
        }

        void timerCallback() override
        {
            updateSync();
            if (isShowing()) updatePresetName();
        }

        void updateSync()
        {
            if (syncParam == nullptr) return;
            const bool synced = syncParam->load() > 0.5f;
            knobs.front()->setVisible (! synced);
            division->setVisible (synced);
        }

        APVTS& state;
        int fxType;
        Toggle power;
        LockButton lock;
        juce::ComboBox modulePresets;
        int shownPreset = -1;   // index of the preset whose name the menu shows, -1 none
        std::unique_ptr<Segmented> selector;
        std::vector<std::unique_ptr<Toggle>> toggles;
        std::vector<std::unique_ptr<Knob>> knobs;
        std::unique_ptr<Knob> division;
        std::unique_ptr<juce::Component> display, meter;
        std::atomic<float>* syncParam = nullptr;
        juce::String description;
        juce::Rectangle<int> infoArea, descriptionArea;
    };

    // Effect chain: drag & drop to reorder, click to select, dot toggles on/off.
    class FxChainList : public Section, private juce::Timer
    {
    public:
        FxChainList (APVTS& s, std::function<fx::Order()> getOrder, std::function<void (const fx::Order&)> setOrder)
            : Section ("Chain"), state (s.state), readOrder (std::move (getOrder)), writeOrder (std::move (setOrder))
        {
            for (int t = 0; t < fx::kNumFx; ++t)
                onAttachments[(size_t) t] = std::make_unique<juce::ParameterAttachment> (
                    *s.getParameter (params::fx (t, "on")), [this, t] (float v) { on[(size_t) t] = v > 0.5f; repaint(); }, nullptr);
            for (auto& a : onAttachments) a->sendInitialUpdate();
            order = readOrder();
            startTimerHz (5);
        }

        std::function<void (int type)> onSelect;
        int getSelected() const { return selected; }

    private:
        juce::ValueTree state;
        int lockMask = 0;

    public:

        void paint (juce::Graphics& g) override
        {
            Section::paint (g);
            const auto& p = Theme::get().palette();

            for (int i = 0; i < fx::kNumFx; ++i)
            {
                const int type = order[(size_t) i];
                auto r = rowBounds (i).toFloat();
                if (dragging && i == dragFrom)
                    r = r.withY (r.getY() + (float) dragOffset);

                const bool sel = type == selected;
                if (sel || i == hovered)
                {
                    g.setColour (sel ? p.accentSoft : p.track.withAlpha (0.5f));
                    g.fillRoundedRectangle (r, 6.0f);
                }

                // Griff
                g.setColour (p.textDim.withAlpha (0.6f));
                for (int k = 0; k < 3; ++k)
                    for (int j = 0; j < 2; ++j)
                        g.fillEllipse (r.getX() + 10.0f + (float) j * 4.0f, r.getCentreY() - 5.0f + (float) k * 4.0f, 2.0f, 2.0f);

                g.setColour (on[(size_t) type] ? p.text : p.textDim);
                g.setFont (font (14.0f, sel));
                g.drawText (fx::fxNames()[type], r.withTrimmedLeft (28.0f).withTrimmedRight (40.0f), juce::Justification::centredLeft);

                if (isEffectLocked (type))
                    drawPadlock (g, powerBounds (r).translated (-24.0f, 0.0f).withSizeKeepingCentre (16.0f, 16.0f), true, p.textDim);

                const auto dot = powerBounds (r);
                g.setColour (on[(size_t) type] ? p.accent : p.line);
                if (on[(size_t) type]) g.fillEllipse (dot);
                else                   g.drawEllipse (dot.reduced (0.5f), 1.2f);
            }

            if (dragging)
            {
                g.setColour (p.accent);
                const auto target = rowBounds (dropIndex()).toFloat();
                g.fillRect (target.getX() + 8.0f, dropIndex() > dragFrom ? target.getBottom() : target.getY() - 1.0f,
                            target.getWidth() - 16.0f, 2.0f);
            }
        }

        void mouseMove (const juce::MouseEvent& e) override
        {
            const int h = rowAt (e.y);
            if (h != hovered) { hovered = h; repaint(); }
            setMouseCursor (h >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        }

        void mouseExit (const juce::MouseEvent&) override { hovered = -1; repaint(); }

        void mouseDown (const juce::MouseEvent& e) override
        {
            const int i = rowAt (e.y);
            if (i < 0) return;
            const int type = order[(size_t) i];

            if (powerBounds (rowBounds (i).toFloat()).expanded (6.0f).contains (e.position))
            {
                onAttachments[(size_t) type]->setValueAsCompleteGesture (on[(size_t) type] ? 0.0f : 1.0f);
                return;
            }

            select (type);
            dragFrom = i;
            dragStartY = e.y;
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (dragFrom < 0) return;
            dragOffset = e.y - dragStartY;
            if (std::abs (dragOffset) > 4) dragging = true;
            setMouseCursor (juce::MouseCursor::DraggingHandCursor);
            repaint();
        }

        void mouseUp (const juce::MouseEvent&) override
        {
            if (dragging)
            {
                const int to = dropIndex();
                if (to != dragFrom)
                {
                    const int moved = order[(size_t) dragFrom];
                    std::vector<int> v (order.begin(), order.end());
                    v.erase (v.begin() + dragFrom);
                    v.insert (v.begin() + to, moved);
                    std::copy (v.begin(), v.end(), order.begin());
                    writeOrder (order);
                }
            }
            dragging = false;
            dragFrom = -1;
            dragOffset = 0;
            repaint();
        }

        void select (int type)
        {
            selected = type;
            if (onSelect) onSelect (type);
            repaint();
        }

    private:
        bool isEffectLocked (int type) const
        {
            return locks::isLocked (state, locks::kAllEffects) || locks::isLocked (state, locks::effectGroup (type));
        }

        void timerCallback() override
        {
            if (dragging) return;
            int mask = 0;
            for (int t = 0; t < fx::kNumFx; ++t) mask |= isEffectLocked (t) ? (1 << t) : 0;
            const auto o = readOrder();
            if (o != order || mask != lockMask) { order = o; lockMask = mask; repaint(); }
        }

        juce::Rectangle<int> rowBounds (int i) const
        {
            auto c = content();
            const int h = std::min (40, c.getHeight() / fx::kNumFx);
            return c.withY (c.getY() + i * h).withHeight (h).reduced (0, 2);
        }

        static juce::Rectangle<float> powerBounds (juce::Rectangle<float> row)
        {
            return { row.getRight() - 26.0f, row.getCentreY() - 6.0f, 12.0f, 12.0f };
        }

        int rowAt (int y) const
        {
            for (int i = 0; i < fx::kNumFx; ++i)
                if (y >= rowBounds (i).getY() - 2 && y < rowBounds (i).getBottom() + 2) return i;
            return -1;
        }

        int dropIndex() const
        {
            const int h = rowBounds (1).getY() - rowBounds (0).getY();
            return std::clamp (dragFrom + (int) std::round ((float) dragOffset / (float) std::max (1, h)), 0, fx::kNumFx - 1);
        }

        std::function<fx::Order()> readOrder;
        std::function<void (const fx::Order&)> writeOrder;
        std::array<std::unique_ptr<juce::ParameterAttachment>, fx::kNumFx> onAttachments;
        std::array<bool, fx::kNumFx> on {};
        fx::Order order {};
        int selected = 0, hovered = -1, dragFrom = -1, dragStartY = 0, dragOffset = 0;
        bool dragging = false;
    };

    // ---------------------------------------------------------------------------------------------
    // Bar at the top of the FX page:  FX PRESET  ‹ Big Hall ›  ⤓              ● LOCK FX
    class FxPresetStrip : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
    {
    public:
        explicit FxPresetStrip (FxPresetManager& m) : fx (m) { startTimerHz (5); }

        std::function<void()> onOpenPicker, onSave;

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            g.setColour (p.textDim);
            drawSpacedText (g, "FX PRESET", labelArea().toFloat(), 10.5f, juce::Justification::centredLeft, 0.22f);

            auto bar = barArea().toFloat().reduced (0.5f);
            g.setColour (p.panel);
            g.fillRoundedRectangle (bar, bar.getHeight() * 0.5f);
            g.setColour (hover == 2 ? p.textDim : p.line);
            g.drawRoundedRectangle (bar, bar.getHeight() * 0.5f, 1.0f);

            auto chevron = [&] (juce::Rectangle<int> a, bool left, bool hot)
            {
                const auto c = a.toFloat().getCentre();
                const float d = left ? 1.0f : -1.0f;
                juce::Path path;
                path.startNewSubPath (c.x + 2.0f * d, c.y - 4.0f);
                path.lineTo (c.x - 2.0f * d, c.y);
                path.lineTo (c.x + 2.0f * d, c.y + 4.0f);
                g.setColour (hot ? p.accent : p.textDim);
                g.strokePath (path, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            };
            chevron (prevArea(), true, hover == 1);
            chevron (nextArea(), false, hover == 3);

            const auto name = fx.getCurrentName();
            g.setColour (name.isEmpty() ? p.textDim : p.text);
            g.setFont (font (13.5f));
            g.drawText (name.isEmpty() ? juce::String ("From sound preset") : name, nameArea(), juce::Justification::centred);

            // Save icon
            {
                const auto c = saveArea().toFloat().getCentre();
                juce::Path icon;
                icon.startNewSubPath (c.x, c.y - 5.0f); icon.lineTo (c.x, c.y + 2.0f);
                icon.startNewSubPath (c.x - 3.0f, c.y - 1.0f); icon.lineTo (c.x, c.y + 2.0f); icon.lineTo (c.x + 3.0f, c.y - 1.0f);
                icon.startNewSubPath (c.x - 5.0f, c.y + 2.0f); icon.lineTo (c.x - 5.0f, c.y + 5.0f);
                icon.lineTo (c.x + 5.0f, c.y + 5.0f); icon.lineTo (c.x + 5.0f, c.y + 2.0f);
                g.setColour (hover == 4 ? p.accent : p.textDim);
                g.strokePath (icon, juce::PathStrokeType (1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }

            // Lock switch
            const bool locked = fx.isLocked();
            auto lock = lockArea().toFloat();
            const auto dot = lock.removeFromLeft (16.0f).withSizeKeepingCentre (12.0f, 12.0f);
            g.setColour (locked ? p.accent : p.line);
            if (locked) g.fillEllipse (dot); else g.drawEllipse (dot.reduced (0.5f), 1.2f);
            g.setColour (locked ? p.text : p.textDim);
            drawSpacedText (g, "LOCK FX", lock.withTrimmedLeft (6.0f), 10.5f, juce::Justification::centredLeft, 0.18f, locked);
        }

        void mouseMove (const juce::MouseEvent& e) override
        {
            const int z = zoneAt (e.getPosition());
            if (z != hover)
            {
                hover = z;
                setTooltip (z == 5 ? "Keep these effects when switching sound presets"
                          : z == 4 ? "Save FX preset" : z == 2 ? "Browse FX presets" : "");
                setMouseCursor (z > 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
                repaint();
            }
        }

        void mouseExit (const juce::MouseEvent&) override { hover = 0; repaint(); }

        void mouseDown (const juce::MouseEvent& e) override
        {
            switch (zoneAt (e.getPosition()))
            {
                case 1: fx.previous(); break;
                case 2: if (onOpenPicker) onOpenPicker(); break;
                case 3: fx.next(); break;
                case 4: if (onSave) onSave(); break;
                case 5: fx.setLocked (! fx.isLocked()); break;
                default: break;
            }
            repaint();
        }

    private:
        juce::Rectangle<int> labelArea() const { return getLocalBounds().withWidth (90); }
        juce::Rectangle<int> barArea() const { return getLocalBounds().withTrimmedLeft (90).withWidth (330).reduced (0, 4); }
        juce::Rectangle<int> prevArea() const { return barArea().withWidth (30); }
        juce::Rectangle<int> nextArea() const { return barArea().withTrimmedRight (34).removeFromRight (30); }
        juce::Rectangle<int> saveArea() const { return barArea().removeFromRight (34); }
        juce::Rectangle<int> nameArea() const { return barArea().withTrimmedLeft (30).withTrimmedRight (64); }
        juce::Rectangle<int> lockArea() const { return getLocalBounds().removeFromRight (110); }

        int zoneAt (juce::Point<int> pos) const
        {
            if (prevArea().contains (pos)) return 1;
            if (nextArea().contains (pos)) return 3;
            if (saveArea().contains (pos)) return 4;
            if (barArea().contains (pos)) return 2;
            if (lockArea().contains (pos)) return 5;
            return 0;
        }

        void timerCallback() override
        {
            const auto key = fx.getCurrentName() + (fx.isLocked() ? "1" : "0");
            if (key != shown) { shown = key; repaint(); }
        }

        FxPresetManager& fx;
        int hover = 0;
        juce::String shown;
    };

    // FX preset picker: one column per group.
    class FxPresetPicker : public juce::Component
    {
    public:
        explicit FxPresetPicker (FxPresetManager& m) : fx (m), done ("Done", false)
        {
            addAndMakeVisible (done);
            done.onClick = [this] { setVisible (false); };
        }

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            auto r = getLocalBounds().toFloat();
            g.setColour (p.panel);
            g.fillRoundedRectangle (r, 8.0f);
            g.setColour (p.line);
            g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);

            const auto cats = categories();
            for (int c = 0; c < cats.size(); ++c)
            {
                g.setColour (p.text);
                drawSpacedText (g, cats[c].toUpperCase(), columnArea (c, cats.size()).removeFromTop (30).toFloat(),
                                11.0f, juce::Justification::centredLeft, 0.25f, true);
            }

            for (const auto& item : items())
            {
                const bool cur = item.index == fx.getCurrentIndex(), hov = item.index == hovered;
                auto ir = item.bounds.toFloat();
                if (cur || hov)
                {
                    g.setColour (cur ? p.accentSoft : p.track.withAlpha (0.6f));
                    g.fillRoundedRectangle (ir, 5.0f);
                }
                g.setColour (cur ? p.accent : p.text);
                g.setFont (font (13.0f, cur));
                g.drawText (fx.getPresets()[(size_t) item.index].name, ir.reduced (8.0f, 0.0f), juce::Justification::centredLeft, true);
            }
        }

        void resized() override { done.setBounds (getLocalBounds().removeFromBottom (48).removeFromRight (120).reduced (12, 8)); }

        void mouseMove (const juce::MouseEvent& e) override
        {
            int h = -1;
            for (const auto& item : items()) if (item.bounds.contains (e.getPosition())) h = item.index;
            if (h != hovered) { hovered = h; repaint(); }
            setMouseCursor (h >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            for (const auto& item : items())
                if (item.bounds.contains (e.getPosition()))
                {
                    fx.load (item.index);
                    repaint();
                }
        }

        void mouseDoubleClick (const juce::MouseEvent&) override { setVisible (false); }

    private:
        struct Item { int index; juce::Rectangle<int> bounds; };

        juce::StringArray categories() const
        {
            juce::StringArray c;
            for (const auto& p : fx.getPresets()) c.addIfNotAlreadyThere (p.category);
            return c;
        }

        juce::Rectangle<int> columnArea (int c, int count) const
        {
            auto r = getLocalBounds().reduced (20, 14).withTrimmedBottom (40);
            const int w = r.getWidth() / std::max (1, count);
            return r.withX (r.getX() + c * w).withWidth (w - 10);
        }

        std::vector<Item> items() const
        {
            std::vector<Item> out;
            const auto cats = categories();
            std::vector<int> row ((size_t) cats.size(), 0);
            const auto& list = fx.getPresets();
            for (int i = 0; i < (int) list.size(); ++i)
            {
                const int c = cats.indexOf (list[(size_t) i].category);
                auto col = columnArea (c, cats.size()).withTrimmedTop (32);
                out.push_back ({ i, col.withY (col.getY() + row[(size_t) c]++ * 30).withHeight (28) });
            }
            return out;
        }

        FxPresetManager& fx;
        PillButton done;
        int hovered = -1;
    };

    // Small dialog for naming an FX preset.
    class FxSavePrompt : public juce::Component
    {
    public:
        explicit FxSavePrompt (FxPresetManager& m) : fx (m), save ("Save", true), cancel ("Cancel", false)
        {
            name.setTextToShowWhenEmpty ("FX preset name", juce::Colours::grey);
            name.onReturnKey = [this] { commit(); };
            name.onEscapeKey = [this] { setVisible (false); };
            for (auto* c : std::initializer_list<juce::Component*> { &name, &save, &cancel }) addAndMakeVisible (c);
            save.onClick = [this] { commit(); };
            cancel.onClick = [this] { setVisible (false); };
        }

        void open()
        {
            styleTextEditor (name);
            name.setText (fx.getCurrentName(), false);
            setVisible (true);
            toFront (true);
            name.grabKeyboardFocus();
            name.selectAll();
        }

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            g.fillAll (p.background.withAlpha (0.8f));
            const auto c = card().toFloat();
            g.setColour (p.panel);
            g.fillRoundedRectangle (c, 12.0f);
            g.setColour (p.line);
            g.drawRoundedRectangle (c, 12.0f, 1.0f);
            g.setColour (p.text);
            drawSpacedText (g, "SAVE FX PRESET", card().reduced (24, 20).removeFromTop (20).toFloat(), 12.0f,
                            juce::Justification::centredLeft, 0.25f, true);
        }

        void resized() override
        {
            auto r = card().reduced (24, 20);
            r.removeFromTop (34);
            name.setBounds (r.removeFromTop (38));
            auto buttons = r.removeFromBottom (32);
            save.setBounds (buttons.removeFromRight (110));
            buttons.removeFromRight (10);
            cancel.setBounds (buttons.removeFromRight (110));
        }

        void mouseDown (const juce::MouseEvent& e) override { if (! card().contains (e.getPosition())) setVisible (false); }

    private:
        juce::Rectangle<int> card() const { return getLocalBounds().withSizeKeepingCentre (440, 180); }

        void commit()
        {
            if (fx.saveUserPreset (name.getText())) setVisible (false);
        }

        FxPresetManager& fx;
        juce::TextEditor name;
        PillButton save, cancel;
    };

    class FxPage : public juce::Component
    {
    public:
        FxPage (APVTS& s, FxPresetManager& fxPresets, std::function<fx::Order()> getOrder,
                std::function<void (const fx::Order&)> setOrder, std::function<float()> compReduction, ScopeBuffer& scope)
            : strip (fxPresets), picker (fxPresets), prompt (fxPresets), chain (s, std::move (getOrder), std::move (setOrder))
        {
            addAndMakeVisible (strip);
            addAndMakeVisible (chain);
            addChildComponent (picker);
            addChildComponent (prompt);
            strip.onOpenPicker = [this] { picker.setVisible (! picker.isVisible()); picker.toFront (false); };
            strip.onSave = [this] { picker.setVisible (false); prompt.open(); };
            for (int t = 0; t < fx::kNumFx; ++t)
            {
                details[(size_t) t] = std::make_unique<FxDetail> (s, t, compReduction, &scope);
                addChildComponent (*details[(size_t) t]);
            }
            chain.onSelect = [this] (int type)
            {
                for (int t = 0; t < fx::kNumFx; ++t)
                    details[(size_t) t]->setVisible (t == type);
            };
            chain.select ((int) fx::FxType::Reverb);

            // Picker and dialog sit above all effect panels
            picker.toFront (false);
            prompt.toFront (false);
        }

        void selectEffect (int type) { chain.select (type); }

        void resized() override
        {
            auto r = getLocalBounds();
            strip.setBounds (r.removeFromTop (36));
            r.removeFromTop (8);
            picker.setBounds (r);
            prompt.setBounds (getLocalBounds());
            chain.setBounds (r.removeFromLeft (250));
            r.removeFromLeft (10);
            for (auto& d : details) d->setBounds (r);
        }

    private:
        FxPresetStrip strip;
        FxPresetPicker picker;
        FxSavePrompt prompt;
        FxChainList chain;
        std::array<std::unique_ptr<FxDetail>, fx::kNumFx> details;
    };
}
