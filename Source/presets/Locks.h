#pragma once
#include <juce_data_structures/juce_data_structures.h>

namespace fjord::locks
{
    // Parts of the sound that can be locked: they are kept when switching sound presets,
    // e.g. an arp pattern, one oscillator or an effect chain you want to try on other sounds.
    // The state lives as a property in the plugin state (saved with the project).
    enum class Kind { Effects, Effect, Rhythm, Sound };

    struct Group
    {
        const char* property;   // state property ("fxLock" is also used by the FX preset manager)
        const char* label;
        const char* prefixes;   // parameter ID prefixes, separated by '|'
        Kind kind;
    };

    // The indices are used by the UI (LockButton) – new groups are appended.
    inline constexpr Group groups[] = {
        { "fxLock",         "Effects",      "fx_",                    Kind::Effects },   // 0
        { "arpLock",        "Arpeggiator",  "arp_",                   Kind::Rhythm },    // 1
        { "gateLock",       "Step Gate",    "gate_",                  Kind::Rhythm },    // 2
        { "stutterLock",    "Stutter",      "stutter_",               Kind::Rhythm },    // 3
        { "pumpLock",       "Pump",         "pump_",                  Kind::Rhythm },    // 4
        { "osc1Lock",       "Oscillator 1", "osc1_",                  Kind::Sound },     // 5
        { "osc2Lock",       "Oscillator 2", "osc2_",                  Kind::Sound },     // 6
        { "osc3Lock",       "Oscillator 3", "osc3_",                  Kind::Sound },     // 7
        { "osc4Lock",       "Oscillator 4", "osc4_",                  Kind::Sound },     // 8
        { "subNoiseLock",   "Sub + Noise",  "sub_|noise_",            Kind::Sound },     // 9
        { "filterLock",     "Filters",      "filter",                 Kind::Sound },     // 10
        { "envLock",        "Envelopes",    "amp_|fenv_|menv_",       Kind::Sound },     // 11
        { "fxLock_drive",   "Drive",        "fx_drive_",              Kind::Effect },    // 12 .. 21: one per effect,
        { "fxLock_crush",   "Bitcrush",     "fx_crush_",              Kind::Effect },    //           same order as FxType
        { "fxLock_eq",      "EQ",           "fx_eq_",                 Kind::Effect },
        { "fxLock_comp",    "Compressor",   "fx_comp_",               Kind::Effect },
        { "fxLock_chorus",  "Chorus",       "fx_chorus_",             Kind::Effect },
        { "fxLock_phaser",  "Phaser",       "fx_phaser_",             Kind::Effect },
        { "fxLock_flanger", "Flanger",      "fx_flanger_",            Kind::Effect },
        { "fxLock_delay",   "Delay",        "fx_delay_",              Kind::Effect },
        { "fxLock_reverb",  "Reverb",       "fx_reverb_",             Kind::Effect },
        { "fxLock_width",   "Width",        "fx_width_",              Kind::Effect },
        { "osc5Lock",       "Oscillator 5", "osc5_",                  Kind::Sound },     // 22 .. 25: oscillators 5-8
        { "osc6Lock",       "Oscillator 6", "osc6_",                  Kind::Sound },
        { "osc7Lock",       "Oscillator 7", "osc7_",                  Kind::Sound },
        { "osc8Lock",       "Oscillator 8", "osc8_",                  Kind::Sound },
        { "ampEnvLock",     "Amp Env",      "amp_",                   Kind::Sound },     // 26
        { "filterEnvLock",  "Filter Env",   "fenv_|filter_env",       Kind::Sound },     // 27
        { "modEnvLock",     "Mod Env",      "menv_",                  Kind::Sound },     // 28
        { "voiceLock",      "Voice",        "voice_",                 Kind::Sound },     // 29
        { "lfo1Lock",       "LFO 1",        "lfo1_",                  Kind::Sound },     // 30 .. 32
        { "lfo2Lock",       "LFO 2",        "lfo2_",                  Kind::Sound },
        { "lfo3Lock",       "LFO 3",        "lfo3_",                  Kind::Sound },
        { "matrixLock",     "Mod Matrix",   "mod1_|mod2_|mod3_|mod4_|mod5_|mod6_|mod7_|mod8_|mod9_|mod10_|mod11_|mod12_|mod13_|mod14_|mod15_|mod16_", Kind::Sound },  // 33
        { "macroLock",      "Macros",       "macro",                  Kind::Sound },     // 34 (also keeps the macro names)
    };
    constexpr int kNumGroups = (int) std::size (groups);
    constexpr int kAllEffects = 0, kArp = 1, kGate = 2, kStutter = 3, kPump = 4, kFirstOsc = 5,
                  kSubNoise = 9, kFilters = 10, kEnvelopes = 11, kFirstEffect = 12, kFirstLateOsc = 22,
                  kAmpEnv = 26, kFilterEnv = 27, kModEnv = 28, kVoice = 29, kFirstLfo = 30, kMatrix = 33, kMacros = 34;

    inline int oscGroup (int oscIndex)  { return oscIndex < 4 ? kFirstOsc + oscIndex : kFirstLateOsc + oscIndex - 4; }
    inline int effectGroup (int fxType) { return kFirstEffect + fxType; }
    inline int lfoGroup (int lfoIndex)  { return kFirstLfo + lfoIndex; }

    inline bool isLocked (const juce::ValueTree& state, int group)
    {
        return (bool) state.getProperty (groups[group].property, false);
    }

    inline void setLocked (juce::ValueTree& state, int group, bool locked)
    {
        state.setProperty (groups[group].property, locked, nullptr);
    }

    inline bool matches (int group, const juce::String& paramId)
    {
        for (const auto& prefix : juce::StringArray::fromTokens (groups[group].prefixes, "|", ""))
            if (paramId.startsWith (prefix)) return true;
        return false;
    }

    // Is this parameter protected by an active lock?
    inline bool isParameterLocked (const juce::ValueTree& state, const juce::String& paramId)
    {
        for (int g = 0; g < kNumGroups; ++g)
            if (isLocked (state, g) && matches (g, paramId))
                return true;
        return false;
    }

    inline juce::StringArray lockedLabels (const juce::ValueTree& state)
    {
        juce::StringArray a;
        for (int g = 0; g < kNumGroups; ++g)
            if (isLocked (state, g)) a.add (groups[g].label);
        return a;
    }

    inline void unlockAll (juce::ValueTree& state)
    {
        for (int g = 0; g < kNumGroups; ++g) setLocked (state, g, false);
    }
}
