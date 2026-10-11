#pragma once
#include <juce_core/juce_core.h>
#include "DspCommon.h"

namespace fjord::dsp
{
    // ------------------------------------------------------------------ LFO
    enum class LfoShape { Sine, Triangle, SawUp, SawDown, Square, SampleHold, SmoothRandom };

    inline const juce::StringArray& lfoShapeNames()
    {
        static const juce::StringArray n { "Sine", "Triangle", "Saw Up", "Saw Down", "Square", "S&H", "Smooth" };
        return n;
    }

    // Tempo divisions: name and length of an LFO cycle in quarter notes
    struct Division { const char* name; double beats; };
    inline constexpr Division divisions[] = {
        { "8 Bars", 32.0 }, { "4 Bars", 16.0 }, { "2 Bars", 8.0 }, { "1 Bar", 4.0 },
        { "1/2", 2.0 }, { "1/2 D", 3.0 }, { "1/2 T", 4.0 / 3.0 },
        { "1/4", 1.0 }, { "1/4 D", 1.5 }, { "1/4 T", 2.0 / 3.0 },
        { "1/8", 0.5 }, { "1/8 D", 0.75 }, { "1/8 T", 1.0 / 3.0 },
        { "1/16", 0.25 }, { "1/16 D", 0.375 }, { "1/16 T", 1.0 / 6.0 },
        { "1/32", 0.125 } };
    inline constexpr int numDivisions = (int) std::size (divisions);

    inline const juce::StringArray& divisionNames()
    {
        static const juce::StringArray n = []
        {
            juce::StringArray a;
            for (const auto& d : divisions) a.add (d.name);
            return a;
        }();
        return n;
    }

    inline float hashBipolar (int64_t n, uint32_t seed)
    {
        auto x = (uint32_t) (n * 0x9E3779B1ll) ^ (seed * 0x85EBCA77u);
        x ^= x >> 15; x *= 0x2C1B3C6Du; x ^= x >> 12; x *= 0x297A2D39u; x ^= x >> 15;
        return (float) (x & 0xffffff) / 8388607.5f - 1.0f;
    }

    // Stateless LFO evaluation from the absolute phase (cycles since start).
    // This keeps free-running LFOs in sync across voices, and S&H yields the same value everywhere.
    inline float lfoValue (LfoShape shape, double absPhase, uint32_t seed)
    {
        const double cycle = std::floor (absPhase);
        const float p = (float) (absPhase - cycle);
        switch (shape)
        {
            case LfoShape::Sine:       return SineTable::get().lookup (p);
            case LfoShape::Triangle:   return p < 0.25f ? 4.0f * p : (p < 0.75f ? 2.0f - 4.0f * p : 4.0f * p - 4.0f);
            case LfoShape::SawUp:      return 2.0f * p - 1.0f;
            case LfoShape::SawDown:    return 1.0f - 2.0f * p;
            case LfoShape::Square:     return p < 0.5f ? 1.0f : -1.0f;
            case LfoShape::SampleHold: return hashBipolar ((int64_t) cycle, seed);
            case LfoShape::SmoothRandom:
            {
                const float a = hashBipolar ((int64_t) cycle, seed), b = hashBipolar ((int64_t) cycle + 1, seed);
                const float w = 0.5f - 0.5f * std::cos (kPi * p);
                return a + (b - a) * w;
            }
        }
        return 0.0f;
    }

    // ------------------------------------------------------------------ Mod-Matrix
    constexpr int kNumModSlots = 16;
    constexpr int kNumMacros = 8;

    enum class ModSource
    {
        None, Lfo1, Lfo2, Lfo3, AmpEnv, FilterEnv, ModEnv, Velocity, Note, ModWheel, Aftertouch, PitchBend, Random,
        Macro1, Macro2, Macro3, Macro4, Macro5, Macro6, Macro7, Macro8, Count
    };

    enum class ModDest
    {
        None, Pitch, Osc1Pitch, Osc2Pitch, Osc3Pitch, Osc1Level, Osc2Level, Osc3Level,
        Osc1WtPos, Osc2WtPos, Osc3WtPos, Osc1Pw, Osc2Pw, Osc3Pw, Detune, Spread, SubLevel, NoiseLevel,
        Cutoff1, Reso1, Drive1, Cutoff2, Reso2, FilterEnvAmount, Volume, Pan,
        Lfo1Rate, Lfo2Rate, Lfo3Rate, EnvAttack, EnvRelease,
        // Global effect destinations (only global sources apply: macros, free-running LFOs, mod wheel, aftertouch, bend)
        FxDrive, ChorusMix, PhaserMix, FlangerMix, DelayMix, DelayFeedback, ReverbMix, ReverbSize, CrushMix, StereoWidth,
        // Oscillator 4 was added later; its destinations are appended so stored presets keep their indices
        Osc4Pitch, Osc4Level, Osc4WtPos, Osc4Pw,
        // Oscillators 5-8, appended the same way (4 targets each)
        Osc5Pitch, Osc5Level, Osc5WtPos, Osc5Pw, Osc6Pitch, Osc6Level, Osc6WtPos, Osc6Pw,
        Osc7Pitch, Osc7Level, Osc7WtPos, Osc7Pw, Osc8Pitch, Osc8Level, Osc8WtPos, Osc8Pw,
        Count
    };

    constexpr int kFirstFxDest = (int) ModDest::FxDrive;
    constexpr int kLastFxDest  = (int) ModDest::StereoWidth;

    enum class OscMod { Pitch, Level, WtPos, Pw };

    // Matrix destination of a per-oscillator target (oscillators 1-3 are contiguous, 4-8 are appended in blocks of 4).
    inline ModDest oscDest (OscMod kind, int osc)
    {
        if (osc >= 3)
            return (ModDest) ((int) ModDest::Osc4Pitch + (osc - 3) * 4 + (int) kind);
        static constexpr ModDest first[] = { ModDest::Osc1Pitch, ModDest::Osc1Level, ModDest::Osc1WtPos, ModDest::Osc1Pw };
        return (ModDest) ((int) first[(int) kind] + osc);
    }

    inline const juce::StringArray& modSourceNames()
    {
        static const juce::StringArray n { "None", "LFO 1", "LFO 2", "LFO 3", "Amp Env", "Filter Env", "Mod Env",
                                           "Velocity", "Note", "Mod Wheel", "Aftertouch", "Pitch Bend", "Random",
                                           "Macro 1", "Macro 2", "Macro 3", "Macro 4", "Macro 5", "Macro 6", "Macro 7", "Macro 8" };
        return n;
    }

    inline const juce::StringArray& modDestNames()
    {
        static const juce::StringArray n { "None", "Pitch", "Osc 1 Pitch", "Osc 2 Pitch", "Osc 3 Pitch",
                                           "Osc 1 Level", "Osc 2 Level", "Osc 3 Level",
                                           "Osc 1 WT Pos", "Osc 2 WT Pos", "Osc 3 WT Pos",
                                           "Osc 1 Width", "Osc 2 Width", "Osc 3 Width", "Detune", "Spread",
                                           "Sub Level", "Noise Level", "Cutoff 1", "Reso 1", "Drive 1",
                                           "Cutoff 2", "Reso 2", "Filter Env Amt", "Volume", "Pan",
                                           "LFO 1 Rate", "LFO 2 Rate", "LFO 3 Rate", "Env Attack", "Env Release",
                                           "FX Drive", "Chorus Mix", "Phaser Mix", "Flanger Mix", "Delay Mix", "Delay Feedback",
                                           "Reverb Mix", "Reverb Size", "Crush Mix", "Stereo Width",
                                           "Osc 4 Pitch", "Osc 4 Level", "Osc 4 WT Pos", "Osc 4 Width",
                                           "Osc 5 Pitch", "Osc 5 Level", "Osc 5 WT Pos", "Osc 5 Width",
                                           "Osc 6 Pitch", "Osc 6 Level", "Osc 6 WT Pos", "Osc 6 Width",
                                           "Osc 7 Pitch", "Osc 7 Level", "Osc 7 WT Pos", "Osc 7 Width",
                                           "Osc 8 Pitch", "Osc 8 Level", "Osc 8 WT Pos", "Osc 8 Width" };
        return n;
    }

    struct ModSlot
    {
        ModSource source = ModSource::None;
        ModDest dest = ModDest::None;
        float amount = 0.0f;                 // -1..1
        ModSource via = ModSource::None;     // scales the amount (e.g. macro or mod wheel)
    };
}
