#pragma once
#include <fjord_base/ui/Theme.h>
#include <fjord_base/ui/Controls.h>
#include <fjord_base/ui/Keyboard.h>
#include <fjord_base/ui/PluginView.h>

// Fjord's UI builds on the shared family UI of fjord-base (palette, Manrope, controls, header, presets).
// The northern lights (green -> teal -> blue) are the brand gradient.
namespace fjord::ui
{
    using namespace fjord::base::ui;

    inline const std::array<juce::Colour, 3>& auroraColours (bool dark) { return gradientColours (dark); }
    inline juce::Colour auroraAt (bool dark, float x) { return gradientAt (dark, x); }
    inline void drawAuroraGlow (juce::Graphics& g, juce::Rectangle<float> area, bool dark, float strength = 1.0f) { drawGlow (g, area, dark, strength); }
}
