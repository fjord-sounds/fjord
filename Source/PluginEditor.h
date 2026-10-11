#pragma once
#include "PluginProcessor.h"
#include "ui/LookAndFeelFjord.h"
#include "ui/SynthPage.h"
#include "ui/ModPage.h"
#include "ui/PlayPage.h"
#include "ui/FxPage.h"
#include "ui/CompPage.h"
#include "ui/RhythmPage.h"
#include "ui/ArpPage.h"
#include "ui/PresetBar.h"
#include "ui/PresetBrowser.h"
#include "ui/Keyboard.h"
#include "ui/Header.h"

// The actual UI at a fixed size (1100 x 740). The editor scales it as a whole.
class FjordView : public juce::Component,
                  public juce::TooltipClient,
                  private juce::ChangeListener
{
public:
    static constexpr int baseWidth = 1100, baseHeight = 740;

    explicit FjordView (FjordProcessor&);
    ~FjordView() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    juce::String getTooltip() override
    {
        if (hoverGithub) return "Fjord on GitHub (github.com/fjord-sounds/fjord)";
        return hoverLogo ? "Click: about and debug info  |  Double-click: stop all notes" : juce::String();
    }
    void mouseMove (const juce::MouseEvent&) override;

    // For the screenshot tool: switch the page / open the browser like a click would
    // (0 Play, 1 Synth, 2 Mod, 3 FX, 4 Arp, 5 Rhythm)
    void selectPage (int index) { showPage ((Page) juce::jlimit (0, (int) tabNames.size() - 1, index)); }
    void showBrowser (bool shouldShow) { setBrowserVisible (shouldShow); }
    fjord::ui::FxPage& getFxPage() { return fxPage; }
    fjord::ui::PlayPage& getPlayPage() { return playPage; }
    fjord::ui::CompPage& getCompPage() { return compPage; }
    void showAbout() { about.open(); }
    void tickLevelMeter() { levelMeter.tick(); }

private:
    enum class Page { Play, Synth, Osc, Mod, FX, Comp, Arp, Rhythm };

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void applyTheme();
    void showPage (Page);
    void setBrowserVisible (bool);
    juce::Rectangle<int> tabBounds (int index) const;
    juce::Rectangle<int> themeToggleBounds() const;
    juce::Rectangle<int> githubBounds() const { return themeToggleBounds().translated (-38, 0); }
    juce::Rectangle<int> logoBounds() const { return { margin, 0, 88, headerHeight }; }

    FjordProcessor& fjordProcessor;
    bool hoverLogo = false;
    fjord::ui::LookAndFeelFjord lookAndFeel;
    juce::TooltipWindow tooltips { this, 600 };

    fjord::ui::PlayPage playPage;
    fjord::ui::SynthPage synthPage;
    fjord::ui::OscPage oscPage;
    fjord::ui::ModPage modPage;
    fjord::ui::FxPage fxPage;
    fjord::ui::CompPage compPage;
    fjord::ui::ArpPage arpPage;
    fjord::ui::RhythmPage rhythmPage;
    fjord::ui::PresetBar presetBar;
    fjord::ui::Knob master;
    fjord::ui::Keyboard keyboard;
    fjord::ui::PresetBrowser browser;
    fjord::ui::SaveDialog saveDialog;
    fjord::ui::AuroraMeter levelMeter;
    fjord::ui::AboutPanel about;
    int logoClicks = 0;   // a single click opens the About panel only if no double-click follows

    Page page = Page::Synth;
    int hoveredTab = -1;
    bool hoverTheme = false, hoverGithub = false;

    static inline const juce::StringArray tabNames { "Play", "Synth", "Osc", "Mod", "FX", "Comp", "Arp", "Rhythm" };
    static constexpr int headerHeight = 64, tabHeight = 40, keyboardHeight = 72, margin = 16;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FjordView)
};

// Plugin window: freely scalable (75 - 150 %) with a fixed aspect ratio, the size is remembered.
class FjordEditor : public juce::AudioProcessorEditor
{
public:
    explicit FjordEditor (FjordProcessor&);
    void resized() override;

private:
    FjordProcessor& fjordProcessor;
    FjordView view;
    bool ready = false;   // only save after construction – before that JUCE reports intermediate sizes
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FjordEditor)
};
