#include "PluginEditor.h"

using namespace fjord;

namespace
{
    const juce::URL githubUrl { "https://github.com/fjord-sounds/fjord" };

    // GitHub mark (octicon "mark-github", 16 x 16)
    juce::Path githubMark()
    {
        static const juce::Path mark = juce::Drawable::parseSVGPath (
            "M8 0C3.58 0 0 3.58 0 8c0 3.54 2.29 6.53 5.47 7.59.4.07.55-.17.55-.38 0-.19-.01-.82-.01-1.49-2.01.37-2.53-.49"
            "-2.69-.94-.09-.23-.48-.94-.82-1.13-.28-.15-.68-.52-.01-.53.63-.01 1.08.58 1.23.82.72 1.21 1.87.87 2.33.66.07-.52"
            ".28-.87.51-1.07-1.78-.2-3.64-.89-3.64-3.95 0-.87.31-1.59.82-2.15-.08-.2-.36-1.02.08-2.12 0 0 .67-.21 2.2.82.64-.18"
            " 1.32-.27 2-.27.68 0 1.36.09 2 .27 1.53-1.04 2.2-.82 2.2-.82.44 1.1.16 1.92.08 2.12.51.56.82 1.27.82 2.15 0 3.07-1.87"
            " 3.75-3.65 3.95.29.25.54.73.54 1.48 0 1.07-.01 1.93-.01 2.2 0 .21.15.46.55.38A8.013 8.013 0 0016 8c0-4.42-3.58-8-8-8z");
        return mark;
    }
}

FjordView::FjordView (FjordProcessor& p)
    : fjordProcessor (p),
      playPage (p.apvts, p.scope),
      synthPage (p.apvts),
      oscPage (p.apvts),
      modPage (p.apvts),
      fxPage (p.apvts, p.fxPresetManager, [&p] { return p.getFxOrder(); }, [&p] (const fjord::fx::Order& o) { p.setFxOrder (o); },
              [&p] { return p.getCompressorReduction(); }, p.scope),
      compPage (p.apvts, { [&p] { return p.getCompressorInput(); }, [&p] { return p.getCompressorReduction(); },
                           [&p] { return p.getCompressorOutput(); } }),
      arpPage (p.apvts, [&p] { return p.getArpStep(); }),
      rhythmPage (p.apvts, [&p] { return p.getGateStep(); }),
      presetBar (p.presetManager),
      master (p.apvts, params::masterGain, "Master"),
      keyboard (p.keyboardState),
      browser (p.presetManager),
      saveDialog (p.presetManager),
      levelMeter ([&p] { return p.takeOutputPeaks(); }),
      about ([&p] { return p.getDebugReport(); }, [&p] { p.requestPanic(); })
{
    setLookAndFeel (&lookAndFeel);
    ui::Theme::get().addChangeListener (this);

    addAndMakeVisible (synthPage);
    addChildComponent (playPage);
    addChildComponent (oscPage);
    addChildComponent (modPage);
    addChildComponent (fxPage);
    addChildComponent (compPage);
    addChildComponent (arpPage);
    addChildComponent (rhythmPage);
    addAndMakeVisible (presetBar);
    addAndMakeVisible (master);
    addAndMakeVisible (levelMeter);

    keyboard.setAvailableRange (24, 108);
    keyboard.setLowestVisibleKey (36);
    keyboard.setScrollButtonsVisible (false);
    addAndMakeVisible (keyboard);

    addChildComponent (browser);
    addChildComponent (saveDialog);
    addChildComponent (about);
    presetBar.onOpenBrowser = [this] { setBrowserVisible (! browser.isVisible()); };
    presetBar.onSave = [this] { saveDialog.open(); };
    browser.onSave = [this] { saveDialog.open(); };
    browser.onClose = [this] { presetBar.setBrowserOpen (false); };

    applyTheme();
    showPage (Page::Synth);
    setSize (baseWidth, baseHeight);
}

FjordView::~FjordView()
{
    ui::Theme::get().removeChangeListener (this);
    setLookAndFeel (nullptr);
}

void FjordView::changeListenerCallback (juce::ChangeBroadcaster*)
{
    applyTheme();
}

void FjordView::applyTheme()
{
    lookAndFeel.applyPalette();
    sendLookAndFeelChange();
    repaint();
}

void FjordView::setBrowserVisible (bool shouldShow)
{
    if (shouldShow) browser.open();
    else            browser.close();
    presetBar.setBrowserOpen (shouldShow);
}

void FjordView::showPage (Page newPage)
{
    setBrowserVisible (false);
    page = newPage;
    playPage.setVisible (page == Page::Play);
    synthPage.setVisible (page == Page::Synth);
    oscPage.setVisible (page == Page::Osc);
    modPage.setVisible (page == Page::Mod);
    fxPage.setVisible (page == Page::FX);
    compPage.setVisible (page == Page::Comp);
    arpPage.setVisible (page == Page::Arp);
    rhythmPage.setVisible (page == Page::Rhythm);
    repaint();
}

juce::Rectangle<int> FjordView::tabBounds (int index) const
{
    constexpr int tabW = 84;
    return { margin + index * tabW, headerHeight, tabW, tabHeight };
}

juce::Rectangle<int> FjordView::themeToggleBounds() const
{
    return { getWidth() - margin - 130, (headerHeight - 28) / 2, 28, 28 };
}

void FjordView::paint (juce::Graphics& g)
{
    const auto& pal = ui::Theme::get().palette();
    g.fillAll (pal.background);
    ui::drawAuroraGlow (g, getLocalBounds().toFloat().withHeight ((float) (headerHeight + tabHeight) * 1.6f), ui::Theme::get().isDark());

    // Logo: lower case, light, lots of air
    g.setColour (pal.text);
    const auto logoFont = ui::font (30.0f).withExtraKerningFactor (0.04f);
    g.setFont (logoFont);
    g.drawText ("fjord", margin + 4, 0, 120, headerHeight, juce::Justification::centredLeft);
    const float logoW = juce::GlyphArrangement::getStringWidth (logoFont, "fjord");
    g.setColour (pal.accent);
    g.fillEllipse ((float) margin + 4.0f + logoW + 3.0f, (float) headerHeight * 0.5f + 5.0f, 5.0f, 5.0f);


    // Tabs
    for (int i = 0; i < tabNames.size(); ++i)
    {
        const auto r = tabBounds (i).toFloat();
        const bool active = i == (int) page;
        g.setColour (active ? pal.text : (i == hoveredTab ? pal.text.withAlpha (0.75f) : pal.textDim));
        ui::drawSpacedText (g, tabNames[i].toUpperCase(), r, 11.5f, juce::Justification::centred, 0.22f, active);
        if (active)
        {
            g.setColour (pal.accent);
            g.fillRoundedRectangle (r.getCentreX() - 10.0f, r.getBottom() - 9.0f, 20.0f, 2.0f, 1.0f);
        }
    }

    // Theme switch: sun / moon
    {
        const auto r = themeToggleBounds().toFloat().reduced (6.0f);
        g.setColour (hoverTheme ? pal.text : pal.textDim);
        if (ui::Theme::get().isDark())
        {
            g.drawEllipse (r.reduced (2.0f), 1.4f);
            for (int i = 0; i < 8; ++i)
            {
                const float a = (float) i * juce::MathConstants<float>::twoPi / 8.0f;
                const auto c = r.getCentre();
                g.drawLine ({ c.getPointOnCircumference (r.getWidth() * 0.62f, a), c.getPointOnCircumference (r.getWidth() * 0.78f, a) }, 1.2f);
            }
        }
        else
        {
            juce::Path moon;
            moon.addEllipse (r);
            juce::Path cut;
            cut.addEllipse (r.translated (r.getWidth() * 0.38f, -r.getHeight() * 0.22f));
            moon.setUsingNonZeroWinding (false);
            moon.addPath (cut);
            g.fillPath (moon);
        }
    }

    // GitHub link
    {
        auto mark = githubMark();
        const auto r = githubBounds().toFloat().reduced (6.0f);
        mark.applyTransform (mark.getTransformToScaleToFit (r, true));
        g.setColour (hoverGithub ? pal.text : pal.textDim);
        g.fillPath (mark);
    }

    // Divider below the header
    g.setColour (pal.line);
    g.fillRect (margin, headerHeight - 1, getWidth() - 2 * margin, 1);


}

void FjordView::resized()
{
    auto r = getLocalBounds();
    auto header = r.removeFromTop (headerHeight);
    master.setBounds (header.removeFromRight (margin + 92).withTrimmedRight (margin).reduced (0, 6));
    presetBar.setBounds (header.withSizeKeepingCentre (440, 36).withX ((getWidth() - 440) / 2));
    r.removeFromTop (tabHeight);

    r.reduce (margin, 0);
    keyboard.setBounds (r.removeFromBottom (keyboardHeight + margin).withTrimmedBottom (margin));
    keyboard.setKeyWidth ((float) keyboard.getWidth() / 50.0f);   // 50 white keys from C0 to C7
    r.removeFromBottom (12);
    r.removeFromTop (4);
    synthPage.setBounds (r);
    oscPage.setBounds (r);
    playPage.setBounds (r);
    modPage.setBounds (r);
    fxPage.setBounds (r);
    compPage.setBounds (r);
    arpPage.setBounds (r);
    rhythmPage.setBounds (r);
    browser.setBounds (getLocalBounds().reduced (margin, 0).withTrimmedTop (headerHeight + 14)
                                       .withBottom (keyboard.getY() - 12));
    saveDialog.setBounds (getLocalBounds());
    about.setBounds (getLocalBounds());
    levelMeter.setBounds (logoBounds().getRight() + 18, (headerHeight - 30) / 2, 186, 30);
}

void FjordView::mouseDown (const juce::MouseEvent& e)
{
    for (int i = 0; i < tabNames.size(); ++i)
        if (tabBounds (i).contains (e.getPosition()))
            return showPage ((Page) i);

    if (themeToggleBounds().contains (e.getPosition()))
        ui::Theme::get().setDark (! ui::Theme::get().isDark());

    if (githubBounds().contains (e.getPosition()))
        githubUrl.launchInDefaultBrowser();
}

void FjordView::mouseUp (const juce::MouseEvent& e)
{
    if (! logoBounds().contains (e.getPosition()) || e.getNumberOfClicks() > 1 || e.mouseWasDraggedSinceMouseDown())
        return;
    const int click = ++logoClicks;
    juce::Timer::callAfterDelay (juce::MouseEvent::getDoubleClickTimeout() + 20,
                                 [safe = juce::Component::SafePointer<FjordView> (this), click]
                                 {
                                     if (safe != nullptr && safe->logoClicks == click)
                                         safe->about.open();
                                 });
}

void FjordView::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (logoBounds().contains (e.getPosition()))
    {
        ++logoClicks;   // cancels the pending About panel
        fjordProcessor.requestPanic();
    }
}

void FjordView::mouseMove (const juce::MouseEvent& e)
{
    const bool wasLogo = hoverLogo;
    hoverLogo = logoBounds().contains (e.getPosition());
    int tab = -1;
    for (int i = 0; i < tabNames.size(); ++i)
        if (tabBounds (i).contains (e.getPosition()))
            tab = i;

    const bool theme = themeToggleBounds().contains (e.getPosition());
    const bool github = githubBounds().contains (e.getPosition());
    if (tab != hoveredTab || theme != hoverTheme || github != hoverGithub || hoverLogo != wasLogo)
    {
        hoveredTab = tab;
        hoverTheme = theme;
        hoverGithub = github;
        setMouseCursor (tab >= 0 || theme || github || hoverLogo ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        repaint (0, 0, getWidth(), headerHeight + tabHeight);
    }
}

// ---------------------------------------------------------------------------------------------
FjordEditor::FjordEditor (FjordProcessor& p)
    : AudioProcessorEditor (p), fjordProcessor (p), view (p)
{
    addAndMakeVisible (view);
    setResizable (true, true);
    setResizeLimits (FjordView::baseWidth * 3 / 4, FjordView::baseHeight * 3 / 4,
                     FjordView::baseWidth * 3 / 2, FjordView::baseHeight * 3 / 2);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) FjordView::baseWidth / FjordView::baseHeight);

    // Size: the one stored in the project first, otherwise the last one used globally
    const auto& st = p.apvts.state;
    const float saved = st.hasProperty ("uiScale") ? (float) (double) st.getProperty ("uiScale") : ui::Theme::get().getScale();
    const float scale = juce::jlimit (0.75f, 1.5f, saved);
    setSize (juce::roundToInt (FjordView::baseWidth * scale), juce::roundToInt (FjordView::baseHeight * scale));
    ready = true;
}

void FjordEditor::resized()
{
    const float scale = (float) getWidth() / (float) FjordView::baseWidth;
    view.setTransform (juce::AffineTransform::scale (scale));
    view.setBounds (0, 0, FjordView::baseWidth, FjordView::baseHeight);

    if (ready && scale >= 0.74f && scale <= 1.51f)
    {
        ui::Theme::get().setScale (scale);
        fjordProcessor.apvts.state.setProperty ("uiScale", scale, nullptr);
    }
}
