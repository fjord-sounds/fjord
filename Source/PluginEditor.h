#pragma once
#include "PluginProcessor.h"
#include "ui/SynthPage.h"
#include "ui/ModPage.h"
#include "ui/PlayPage.h"
#include "ui/CompPage.h"
#include "ui/RhythmPage.h"
#include "ui/ArpPage.h"
#include "ui/LockMenu.h"
#include <fjord_base/fx/FxPage.h>

// The actual UI at a fixed size (1100 x 740) inside the shared frame of fjord-base (header, presets, About).
// The editor scales it as a whole.
class FjordView : public fjord::ui::PluginView
{
public:
    static constexpr int width = 1100, height = 740;

    explicit FjordView (FjordProcessor&);

    // For the screenshot tool (0 Play, 1 Synth, 2 Osc, 3 Mod, 4 FX, 5 Comp, 6 Arp, 7 Rhythm)
    void selectPage (int index) { showPage (index); }
    fjord::base::fx::FxPage& getFxPage() { return fxPage; }
    fjord::ui::PlayPage& getPlayPage() { return playPage; }
    fjord::ui::CompPage& getCompPage() { return compPage; }

protected:
    juce::Rectangle<int> layoutContent (juce::Rectangle<int>) override;

private:
    static fjord::ui::ViewSetup makeSetup (FjordProcessor&);

    FjordProcessor& fjordProcessor;
    fjord::ui::PlayPage playPage;
    fjord::ui::SynthPage synthPage;
    fjord::ui::OscPage oscPage;
    fjord::ui::ModPage modPage;
    fjord::base::fx::FxPage fxPage;
    fjord::ui::CompPage compPage;
    fjord::ui::ArpPage arpPage;
    fjord::ui::RhythmPage rhythmPage;
    fjord::ui::Keyboard keyboard;

    static constexpr int keyboardHeight = 72;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FjordView)
};

// Plugin window: freely scalable (75 - 150 %) with a fixed aspect ratio, the size is remembered.
class FjordEditor : public fjord::ui::ScaledEditor<FjordView, FjordProcessor>
{
public:
    using ScaledEditor::ScaledEditor;
};
