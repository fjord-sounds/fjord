#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "BinaryData.h"

namespace fjord::ui
{
    // Scandinavian palette: warm paper / anthracite, one muted accent.
    struct Palette
    {
        juce::Colour background, panel, line, text, textDim, accent, accentSoft, track, keyWhite, keyBlack;
    };

    // Northern lights in both themes: arctic day (icy white, deep aurora green) and polar night
    // (night blue, glowing aurora green).
    inline Palette lightPalette()
    {
        return { juce::Colour (0xffedf3f4),   // background: icy white
                 juce::Colour (0xfff8fbfc),   // panel
                 juce::Colour (0xffcfdce1),   // hairlines
                 juce::Colour (0xff15253a),   // text: deep night blue
                 juce::Colour (0xff6f8291),   // secondary text
                 juce::Colour (0xff0e7566),   // accent: deep aurora green
                 juce::Colour (0x290e7566),
                 juce::Colour (0xffdde7eb),   // knob track
                 juce::Colour (0xfffafcfd),
                 juce::Colour (0xff1c2b3d) };
    }

    inline Palette darkPalette()
    {
        return { juce::Colour (0xff0d1520),   // background: polar night
                 juce::Colour (0xff131e2c),
                 juce::Colour (0xff22314a),
                 juce::Colour (0xffe2edf0),   // text: icy white
                 juce::Colour (0xff7d8fa3),
                 juce::Colour (0xff58d1a4),   // accent: aurora green
                 juce::Colour (0x3358d1a4),
                 juce::Colour (0xff1f2d40),
                 juce::Colour (0xffc3d0d8),
                 juce::Colour (0xff0a1018) };
    }

    // Soft aurora band (green → teal → violet) used behind the header and in backdrops.
    inline void drawAuroraGlow (juce::Graphics& g, juce::Rectangle<float> area, bool dark, float strength = 1.0f)
    {
        const float a = (dark ? 0.16f : 0.10f) * strength;
        const juce::Colour colours[] = { juce::Colour (0xff3fd8a0), juce::Colour (0xff2fb7c8), juce::Colour (0xff7a6fd8) };
        for (int i = 0; i < 3; ++i)
        {
            const float cx = area.getX() + area.getWidth() * (0.18f + 0.32f * (float) i);
            const float cy = area.getY() + area.getHeight() * (0.15f + 0.1f * (float) (i % 2));
            juce::ColourGradient grad (colours[i].withAlpha (a), cx, cy, colours[i].withAlpha (0.0f),
                                       cx + area.getWidth() * 0.32f, cy, true);
            g.setGradientFill (grad);
            g.fillEllipse (juce::Rectangle<float> (area.getWidth() * 0.64f, area.getHeight() * 1.4f).withCentre ({ cx, cy }));
        }
    }

    // Global theme, stored in the user folder and shared by all instances.
    class Theme : public juce::ChangeBroadcaster
    {
    public:
        static Theme& get()
        {
            static Theme instance;
            return instance;
        }

        const Palette& palette() const { return current; }

        // Tests switch saving off so they do not change the user's settings.
        void setPersistent (bool shouldPersist) { persistent = shouldPersist; }
        bool isDark() const { return dark; }

        // Window size (scale) is remembered globally
        float getScale() const
        {
            auto* file = const_cast<juce::ApplicationProperties&> (settings).getUserSettings();
            return file != nullptr ? (float) file->getDoubleValue ("uiScale", 1.0) : 1.0f;
        }

        juce::File getSettingsFile() const
        {
            auto* file = const_cast<juce::ApplicationProperties&> (settings).getUserSettings();
            return file != nullptr ? file->getFile() : juce::File();
        }

        void setScale (float s)
        {
            if (auto* file = persistent ? settings.getUserSettings() : nullptr)
            {
                file->setValue ("uiScale", s);
                file->saveIfNeeded();
            }
        }

        void setDark (bool shouldBeDark)
        {
            if (dark == shouldBeDark)
                return;
            dark = shouldBeDark;
            current = dark ? darkPalette() : lightPalette();
            if (auto* file = persistent ? settings.getUserSettings() : nullptr)
            {
                file->setValue ("darkTheme", dark);
                file->saveIfNeeded();   // write immediately – hosts may end the process without cleaning up
            }
            sendChangeMessage();
        }

    private:
        Theme()
        {
            juce::PropertiesFile::Options o;
            o.applicationName = "Fjord";
            o.filenameSuffix = ".settings";
            o.folderName = "Fjord";
            o.osxLibrarySubFolder = "Application Support";
            o.millisecondsBeforeSaving = 0;
            settings.setStorageParameters (o);
            migrateOldSettings (o);

            if (auto* file = settings.getUserSettings())
                dark = file->getBoolValue ("darkTheme", false);
            current = dark ? darkPalette() : lightPalette();
        }

        // Settings used to live in "Hell Audio/Fjord" – copy them over once.
        static void migrateOldSettings (juce::PropertiesFile::Options o)
        {
            const auto newFile = o.getDefaultFile();
            o.folderName = "Hell Audio/Fjord";
            const auto oldFile = o.getDefaultFile();
            if (! newFile.existsAsFile() && oldFile.existsAsFile())
            {
                newFile.getParentDirectory().createDirectory();
                oldFile.copyFileTo (newFile);
            }
        }

        juce::ApplicationProperties settings;
        bool dark = false, persistent = true;
        Palette current;
    };

    // String literals with non-ASCII characters are UTF-8 – this displays them correctly.
    inline juce::String utf8 (const char* text) { return juce::String::fromUTF8 (text); }

    // Manrope (embedded): Regular for text, Medium for all-caps labels, SemiBold for emphasis.
    enum class Weight { Regular, Medium, SemiBold };

    inline juce::Typeface::Ptr typeface (Weight w)
    {
        static const juce::Typeface::Ptr faces[] = {
            juce::Typeface::createSystemTypefaceFor (BinaryData::ManropeRegular_ttf, BinaryData::ManropeRegular_ttfSize),
            juce::Typeface::createSystemTypefaceFor (BinaryData::ManropeMedium_ttf, BinaryData::ManropeMedium_ttfSize),
            juce::Typeface::createSystemTypefaceFor (BinaryData::ManropeSemiBold_ttf, BinaryData::ManropeSemiBold_ttfSize) };
        return faces[(int) w];
    }

    inline juce::Font font (float height, Weight w)
    {
        return juce::Font (juce::FontOptions().withTypeface (typeface (w)).withHeight (height));
    }

    inline juce::Font font (float height, bool bold = false)
    {
        return font (height, bold ? Weight::SemiBold : Weight::Regular);
    }

    // Small all-caps headings with letter spacing – typical Scandinavian typography.
    inline void drawSpacedText (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area,
                                float height, juce::Justification just, float kerning = 0.18f, bool bold = false)
    {
        g.setFont (font (height, bold ? Weight::SemiBold : Weight::Medium).withExtraKerningFactor (kerning));
        g.drawText (text, area, just, false);
    }
}
