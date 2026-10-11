#pragma once
#include "Theme.h"
#include "../presets/PresetManager.h"
#include "Controls.h"

namespace fjord::ui
{
    // Horizontal preset bar:  ‹   PAD · Nordlys   ›  |  🔒  ⤓
    // Clicking the centre opens the preset browser, the padlock opens the lock menu
    // (keep effects / arp / gate / stutter / pump when switching presets), the icon on the right saves.
    class PresetBar : public juce::Component,
                      public juce::SettableTooltipClient,
                      private juce::ChangeListener,
                      private juce::Value::Listener
    {
    public:
        explicit PresetBar (PresetManager& pm) : presets (pm)
        {
            presets.addChangeListener (this);
            for (int g = 0; g < locks::kNumGroups; ++g)
            {
                lockValues[(size_t) g].referTo (presets.getState().getPropertyAsValue (locks::groups[g].property, nullptr));
                lockValues[(size_t) g].addListener (this);
            }
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
        }

        ~PresetBar() override
        {
            presets.removeChangeListener (this);
            for (auto& v : lockValues) v.removeListener (this);
        }

        std::function<void()> onOpenBrowser, onSave;

        void setBrowserOpen (bool open) { browserOpen = open; repaint(); }

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            auto r = getLocalBounds().toFloat().reduced (0.5f);
            const float rad = r.getHeight() * 0.5f;

            g.setColour (p.panel);
            g.fillRoundedRectangle (r, rad);
            g.setColour (browserOpen ? p.accent : (hover != Zone::None ? p.textDim : p.line));
            g.drawRoundedRectangle (r, rad, 1.0f);

            drawChevron (g, zoneBounds (Zone::Prev), true,  hover == Zone::Prev ? p.accent : p.textDim);
            drawChevron (g, zoneBounds (Zone::Next), false, hover == Zone::Next ? p.accent : p.textDim);

            // Divider, lock + save icons
            const auto lock = zoneBounds (Zone::Lock).toFloat();
            const auto save = zoneBounds (Zone::Save).toFloat();
            g.setColour (p.line);
            g.fillRect (lock.getX(), lock.getY() + 9.0f, 1.0f, lock.getHeight() - 18.0f);
            const bool anyLocked = ! locks::lockedLabels (presets.getState()).isEmpty();
            drawPadlock (g, lock.withTrimmedLeft (4.0f), anyLocked,
                         anyLocked ? p.accent : (hover == Zone::Lock ? p.text : p.textDim));
            drawSaveIcon (g, save, hover == Zone::Save ? p.accent : p.textDim);

            // Category + name, centred together
            const auto category = presets.getCurrentCategory().toUpperCase();
            const auto name = presets.getCurrentName();
            const auto catFont = font (10.5f, true).withExtraKerningFactor (0.22f);
            const auto nameFont = font (15.0f);
            const float catW = category.isEmpty() ? 0.0f : juce::GlyphArrangement::getStringWidth (catFont, category) + 22.0f;
            const float nameW = juce::GlyphArrangement::getStringWidth (nameFont, name);
            auto centre = zoneBounds (Zone::Centre).toFloat();
            float x = std::max (centre.getX(), centre.getCentreX() - (catW + nameW) * 0.5f);

            if (category.isNotEmpty())
            {
                g.setColour (p.accent);
                g.setFont (catFont);
                g.drawText (category, juce::Rectangle<float> (x, centre.getY(), catW, centre.getHeight()),
                            juce::Justification::centredLeft, false);
                g.setColour (p.line);
                g.fillEllipse (x + catW - 13.0f, centre.getCentreY() - 1.5f, 3.0f, 3.0f);
                x += catW;
            }

            g.setColour (p.text);
            g.setFont (nameFont);
            g.drawText (name, juce::Rectangle<float> (x, centre.getY(), centre.getRight() - x, centre.getHeight()),
                        juce::Justification::centredLeft, true);
        }

        void mouseMove (const juce::MouseEvent& e) override
        {
            const auto z = zoneAt (e.getPosition());
            if (z != hover)
            {
                hover = z;
                setTooltip (z == Zone::Save ? "Save preset" : z == Zone::Centre ? "Browse presets" : z == Zone::Lock ? lockTooltip() : "");
                repaint();
            }
        }

        void mouseExit (const juce::MouseEvent&) override { hover = Zone::None; repaint(); }

        void mouseDown (const juce::MouseEvent& e) override
        {
            switch (zoneAt (e.getPosition()))
            {
                case Zone::Prev:   presets.previous(); break;
                case Zone::Next:   presets.next();     break;
                case Zone::Centre: if (onOpenBrowser) onOpenBrowser(); break;
                case Zone::Save:   if (onSave) onSave(); break;
                case Zone::Lock:   showLockMenu(); break;
                case Zone::None:   break;
            }
        }

    private:
        enum class Zone { None, Prev, Centre, Next, Lock, Save };

        juce::String lockTooltip() const
        {
            const auto locked = locks::lockedLabels (presets.getState());
            return locked.isEmpty() ? juce::String ("Lock parts of the sound (oscillators, effects, arp ...) while browsing presets")
                                    : "Kept when switching presets: " + locked.joinIntoString (", ");
        }

        void showLockMenu()
        {
            auto& state = presets.getState();
            auto item = [&state] (juce::PopupMenu& m, int g, const juce::String& label = {})
            {
                m.addItem (g + 1, label.isNotEmpty() ? label : juce::String (locks::groups[g].label), true, locks::isLocked (state, g));
            };

            juce::PopupMenu m, effects;
            m.addSectionHeader ("Keep when switching presets");
            juce::PopupMenu oscillators;
            bool anyOsc = false;
            for (int i = 0; i < dsp::kNumOsc; ++i)
            {
                item (oscillators, locks::oscGroup (i));
                anyOsc = anyOsc || locks::isLocked (state, locks::oscGroup (i));
            }
            oscillators.addSeparator();
            oscillators.addItem (allOscId, "All oscillators");
            m.addSubMenu ("Oscillators", oscillators, true, nullptr, anyOsc);
            item (m, locks::kSubNoise);
            item (m, locks::kFilters);

            auto subMenu = [&] (const juce::String& name, std::initializer_list<int> groups, int allGroup = -1, const juce::String& allLabel = {})
            {
                juce::PopupMenu sub;
                bool any = false;
                for (int g : groups) { item (sub, g); any = any || locks::isLocked (state, g); }
                if (allGroup >= 0)
                {
                    sub.addSeparator();
                    item (sub, allGroup, allLabel);
                    any = any || locks::isLocked (state, allGroup);
                }
                m.addSubMenu (name, sub, true, nullptr, any);
            };
            subMenu ("Envelopes", { locks::kAmpEnv, locks::kFilterEnv, locks::kModEnv }, locks::kEnvelopes, "All envelopes");
            subMenu ("Modulation", { locks::lfoGroup (0), locks::lfoGroup (1), locks::lfoGroup (2), locks::kMatrix });
            item (m, locks::kVoice);
            item (m, locks::kMacros);
            m.addSeparator();

            item (effects, locks::kAllEffects, "Whole effect chain (incl. order)");
            effects.addSeparator();
            for (int t = 0; t < 10; ++t) item (effects, locks::effectGroup (t));
            const bool anyFx = locks::isLocked (state, locks::kAllEffects) || [&state]
            {
                for (int t = 0; t < 10; ++t) if (locks::isLocked (state, locks::effectGroup (t))) return true;
                return false;
            }();
            m.addSubMenu ("Effects", effects, true, nullptr, anyFx);
            m.addSeparator();

            for (int g : { locks::kArp, locks::kGate, locks::kStutter, locks::kPump }) item (m, g);
            m.addItem (allRhythmId, "All rhythm (arp, gate, stutter, pump)");
            m.addSeparator();
            m.addItem (unlockId, "Unlock all");

            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this] (int r)
            {
                auto& st = presets.getState();
                if (r >= 1 && r <= locks::kNumGroups) locks::setLocked (st, r - 1, ! locks::isLocked (st, r - 1));
                if (r == allOscId)    for (int i = 0; i < dsp::kNumOsc; ++i) locks::setLocked (st, locks::oscGroup (i), true);
                if (r == allRhythmId) for (int g : { locks::kArp, locks::kGate, locks::kStutter, locks::kPump }) locks::setLocked (st, g, true);
                if (r == unlockId)    locks::unlockAll (st);
                repaint();
            });
        }

        static constexpr int allOscId = 100, allRhythmId = 101, unlockId = 102;

        void valueChanged (juce::Value&) override { repaint(); }

        juce::Rectangle<int> zoneBounds (Zone z) const
        {
            auto r = getLocalBounds();
            const int h = getHeight();
            auto save = r.removeFromRight (h);
            auto lock = r.removeFromRight (h - 2);
            auto prev = r.removeFromLeft (h + 4);
            auto next = r.removeFromRight (h);
            switch (z)
            {
                case Zone::Lock: return lock;
                case Zone::Prev: return prev;
                case Zone::Next: return next;
                case Zone::Save: return save;
                case Zone::Centre: return r;
                case Zone::None: break;
            }
            return {};
        }

        Zone zoneAt (juce::Point<int> pos) const
        {
            for (auto z : { Zone::Prev, Zone::Next, Zone::Lock, Zone::Save, Zone::Centre })
                if (zoneBounds (z).contains (pos)) return z;
            return Zone::None;
        }

        static void drawChevron (juce::Graphics& g, juce::Rectangle<int> area, bool left, juce::Colour c)
        {
            const auto ctr = area.toFloat().getCentre();
            const float s = 4.0f, dir = left ? 1.0f : -1.0f;
            juce::Path path;
            path.startNewSubPath (ctr.x + s * 0.5f * dir, ctr.y - s);
            path.lineTo (ctr.x - s * 0.5f * dir, ctr.y);
            path.lineTo (ctr.x + s * 0.5f * dir, ctr.y + s);
            g.setColour (c);
            g.strokePath (path, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // Arrow down into a tray – "save"
        static void drawSaveIcon (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour c)
        {
            const auto ctr = area.getCentre();
            juce::Path p;
            p.startNewSubPath (ctr.x, ctr.y - 6.0f);
            p.lineTo (ctr.x, ctr.y + 2.0f);
            p.startNewSubPath (ctr.x - 3.5f, ctr.y - 1.5f);
            p.lineTo (ctr.x, ctr.y + 2.0f);
            p.lineTo (ctr.x + 3.5f, ctr.y - 1.5f);
            p.startNewSubPath (ctr.x - 6.0f, ctr.y + 2.0f);
            p.lineTo (ctr.x - 6.0f, ctr.y + 6.0f);
            p.lineTo (ctr.x + 6.0f, ctr.y + 6.0f);
            p.lineTo (ctr.x + 6.0f, ctr.y + 2.0f);
            g.setColour (c);
            g.strokePath (p, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }

        PresetManager& presets;
        std::array<juce::Value, locks::kNumGroups> lockValues;
        Zone hover = Zone::None;
        bool browserOpen = false;
    };
}
