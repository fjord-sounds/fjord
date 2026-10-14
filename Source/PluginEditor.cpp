#include "PluginEditor.h"

using namespace fjord;

ui::ViewSetup FjordView::makeSetup (FjordProcessor& p)
{
    ui::ViewSetup s { p.apvts, p.presetManager };
    s.undo = &p.undoManager;
    s.masterParamId = params::masterGain;
    s.tabs = { "Play", "Synth", "Osc", "Mod", "FX", "Comp", "Arp", "Rhythm" };
    s.peaks = [&p] { return p.takeOutputPeaks(); };
    s.noteCounter = [&p] { return p.getNoteCount(); };
    s.lastVelocity = [&p] { return p.getLastVelocity(); };
    s.debugReport = [&p] { return p.getDebugReport(); };
    s.panic = [&p] { p.requestPanic(); };
    return s;
}

FjordView::FjordView (FjordProcessor& p)
    : PluginView (makeSetup (p), width, height),
      fjordProcessor (p),
      playPage (p.apvts, p.scope),
      synthPage (p.apvts),
      oscPage (p.apvts),
      modPage (p.apvts),
      fxPage ({ p.apvts, &p.undoManager, p.fxMeters, [&p] { return p.getFxOrder(); },
                [&p] (const fjord::base::fx::Order& o) { p.setFxOrder (o); }, &p.chainPresets }),
      compPage (p.apvts, { [&p] { return p.getCompressorInput(); }, [&p] { return p.getCompressorReduction(); },
                           [&p] { return p.getCompressorOutput(); } }),
      arpPage (p.apvts, [&p] { return p.getArpStep(); }),
      rhythmPage (p.apvts, [&p] { return p.getGateStep(); }),
      keyboard (p.keyboardState)
{
    // The padlock in the preset bar: Fjord's lock menu (parts of the sound that stay when switching presets)
    fjord::base::ui::LockMenu::attach (getPresetBar(), p.apvts, locks::set(), ui::lockMenuLayout());

    keyboard.setAvailableRange (24, 108);
    keyboard.setLowestVisibleKey (36);
    keyboard.setScrollButtonsVisible (false);
    addAndMakeVisible (keyboard);
    setPages ({ &playPage, &synthPage, &oscPage, &modPage, &fxPage, &compPage, &arpPage, &rhythmPage });
}

juce::Rectangle<int> FjordView::layoutContent (juce::Rectangle<int> r)
{
    keyboard.setBounds (r.removeFromBottom (keyboardHeight + margin).withTrimmedBottom (margin));
    keyboard.setKeyWidth ((float) keyboard.getWidth() / 50.0f);   // 50 white keys from C0 to C7
    r.removeFromBottom (12);
    r.removeFromTop (4);
    for (auto* c : std::initializer_list<juce::Component*> { &playPage, &synthPage, &oscPage, &modPage, &fxPage, &compPage, &arpPage, &rhythmPage })
        c->setBounds (r);
    return r.withBottom (keyboard.getY() - 12);
}
