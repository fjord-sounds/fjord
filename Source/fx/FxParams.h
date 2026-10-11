#pragma once
#include <array>
#include <juce_core/juce_core.h>

namespace fjord::fx
{
    enum class FxType { Drive, Crush, Eq, Comp, Chorus, Phaser, Flanger, Delay, Reverb, Width, Count };
    constexpr int kNumFx = (int) FxType::Count;

    // Internal identifiers (for parameter IDs and the stored order) and display names
    inline const juce::StringArray& fxIds()
    {
        static const juce::StringArray ids { "drive", "crush", "eq", "comp", "chorus", "phaser", "flanger", "delay", "reverb", "width" };
        return ids;
    }

    inline const juce::StringArray& fxNames()
    {
        static const juce::StringArray names { "Drive", "Bitcrush", "EQ", "Compressor", "Chorus", "Phaser", "Flanger", "Delay", "Reverb", "Width" };
        return names;
    }

    using Order = std::array<int, kNumFx>;

    inline Order defaultOrder() { return { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 }; }

    inline juce::String orderToString (const Order& o)
    {
        juce::StringArray a;
        for (int i : o) a.add (fxIds()[i]);
        return a.joinIntoString (",");
    }

    // Unknown or missing entries are filled in using the default order.
    inline Order orderFromString (const juce::String& s)
    {
        Order o {};
        int n = 0;
        std::array<bool, kNumFx> used {};
        for (const auto& t : juce::StringArray::fromTokens (s, ",", ""))
        {
            const int i = fxIds().indexOf (t.trim());
            if (i >= 0 && ! used[(size_t) i] && n < kNumFx) { o[(size_t) n++] = i; used[(size_t) i] = true; }
        }
        for (int i = 0; i < kNumFx; ++i)
            if (! used[(size_t) i]) o[(size_t) n++] = i;
        return o;
    }

    struct FxParams
    {
        std::array<bool, kNumFx> on {};

        int driveType = 0;
        float driveAmount = 0.3f, driveTone = 0.7f, driveMix = 1.0f;

        float crushBits = 12.0f, crushRate = 1.0f, crushMix = 1.0f;

        // Parametric EQ: low cut, low shelf, two peaks, high shelf, high cut (slopes: 0 = 12 dB/oct, 1 = 24 dB/oct)
        struct EqSettings
        {
            float lowCut = 20.0f, highCut = 20000.0f;
            int lowCutSlope = 0, highCutSlope = 0;
            float lowFreq = 120.0f, low = 0.0f;
            float midFreq = 1000.0f, mid = 0.0f, midQ = 0.9f;
            float mid2Freq = 3000.0f, mid2 = 0.0f, mid2Q = 0.9f;
            float highFreq = 7000.0f, high = 0.0f;

            // Exact comparison on purpose: the filters are only redesigned when a value really changed.
            JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE ("-Wfloat-equal")
            bool operator== (const EqSettings&) const = default;
            JUCE_END_IGNORE_WARNINGS_GCC_LIKE
        } eq;

        float compThreshold = -18.0f, compRatio = 3.0f, compAttack = 0.01f, compRelease = 0.15f, compMakeup = 0.0f, compMix = 1.0f;
        float compKnee = 6.0f, compScHpf = 20.0f;   // knee width in dB, sidechain high-pass (20 Hz = off)
        bool compAutoGain = false;

        float chorusRate = 0.5f, chorusDepth = 0.5f, chorusMix = 0.5f;

        float phaserRate = 0.3f, phaserDepth = 0.6f, phaserFeedback = 0.4f, phaserMix = 0.5f;

        float flangerRate = 0.2f, flangerDepth = 0.6f, flangerFeedback = 0.5f, flangerMix = 0.5f;

        bool delaySync = true, delayPingPong = true;
        double delayBeats = 0.75;
        float delayTimeMs = 350.0f, delayFeedback = 0.35f, delayDamp = 0.4f, delayDuck = 0.0f, delayMix = 0.25f;

        float reverbSize = 0.6f, reverbDecay = 3.0f, reverbDamp = 0.5f, reverbPredelay = 0.02f, reverbWidth = 1.0f, reverbMix = 0.25f;

        float width = 1.0f;
    };
}
