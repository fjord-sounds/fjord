#pragma once
#include <fjord_base/presets/Locks.h>

namespace fjord::locks
{
    // Fjord's parts of the sound that can be locked (kept when switching sound presets): an arp pattern, one oscillator,
    // an effect chain you want to try on other sounds. The mechanism (LockSet, padlock menu, LockButton, preset manager
    // hook) is fjord-base; this file is Fjord's list. The state lives as properties in the plugin state (saved with the
    // project); the property names never change.
    struct TableRow
    {
        const char* property;   // state property ("fxLock" is also used by the FX chain presets)
        const char* label;
        const char* prefixes;   // parameter ID prefixes, separated by '|'
    };

    // The indices are used by the UI (LockButton) – new groups are appended.
    inline constexpr TableRow table[] = {
        { "fxLock",         "Whole effect chain (incl. order)", "fx_" },   // 0
        { "arpLock",        "Arpeggiator",  "arp_" },    // 1
        { "gateLock",       "Step Gate",    "gate_" },    // 2
        { "stutterLock",    "Stutter",      "stutter_" },    // 3
        { "pumpLock",       "Pump",         "pump_" },    // 4
        { "osc1Lock",       "Oscillator 1", "osc1_" },     // 5
        { "osc2Lock",       "Oscillator 2", "osc2_" },     // 6
        { "osc3Lock",       "Oscillator 3", "osc3_" },     // 7
        { "osc4Lock",       "Oscillator 4", "osc4_" },     // 8
        { "subNoiseLock",   "Sub + Noise",  "sub_|noise_" },     // 9
        { "filterLock",     "Filters",      "filter" },     // 10
        { "envLock",        "Envelopes",    "amp_|fenv_|menv_" },     // 11
        { "fxLock_saturation", "Saturation",    "fx_saturation_" },    // 12 .. 21: effects 0-9 (fjord-base fx::Module order)
        { "fxLock_lofi",      "Lo-Fi",         "fx_lofi_" },
        { "fxLock_transient", "Transient",     "fx_transient_" },
        { "fxLock_compressor", "Compressor",    "fx_compressor_" },
        { "fxLock_squash",    "Squash",        "fx_squash_" },
        { "fxLock_eq",        "EQ",            "fx_eq_" },
        { "fxLock_filter",    "Filter",        "fx_filter_" },
        { "fxLock_motion",    "Motion",        "fx_motion_" },
        { "fxLock_delay",     "Delay",         "fx_delay_" },
        { "fxLock_reverb",    "Reverb",        "fx_reverb_" },
        { "osc5Lock",       "Oscillator 5", "osc5_" },     // 22 .. 25: oscillators 5-8
        { "osc6Lock",       "Oscillator 6", "osc6_" },
        { "osc7Lock",       "Oscillator 7", "osc7_" },
        { "osc8Lock",       "Oscillator 8", "osc8_" },
        { "ampEnvLock",     "Amp Env",      "amp_" },     // 26
        { "filterEnvLock",  "Filter Env",   "fenv_|filter_env" },     // 27
        { "modEnvLock",     "Mod Env",      "menv_" },     // 28
        { "voiceLock",      "Voice",        "voice_" },     // 29
        { "lfo1Lock",       "LFO 1",        "lfo1_" },     // 30 .. 32
        { "lfo2Lock",       "LFO 2",        "lfo2_" },
        { "lfo3Lock",       "LFO 3",        "lfo3_" },
        { "matrixLock",     "Mod Matrix",   "mod1_|mod2_|mod3_|mod4_|mod5_|mod6_|mod7_|mod8_|mod9_|mod10_|mod11_|mod12_|mod13_|mod14_|mod15_|mod16_" },  // 33
        { "macroLock",      "Macros",       "macro" },     // 34 (also keeps the macro names)
        { "fxLock_width",     "Width",         "fx_width_" },    // 35 .. 40: effects 10-15
        { "fxLock_tremolo",   "Tremolo",       "fx_tremolo_" },
        { "fxLock_shifter",   "Frequency Shifter", "fx_shifter_" },
        { "fxLock_exciter",   "Exciter",       "fx_exciter_" },
        { "fxLock_harmonizer", "Harmonizer",    "fx_harmonizer_" },
        { "fxLock_out",       "Clipper",       "fx_out_" },
    };
    constexpr int kNumGroups = (int) std::size (table);
    constexpr int kAllEffects = 0, kArp = 1, kGate = 2, kStutter = 3, kPump = 4, kFirstOsc = 5,
                  kSubNoise = 9, kFilters = 10, kEnvelopes = 11, kFirstEffect = 12, kFirstLateOsc = 22,
                  kAmpEnv = 26, kFilterEnv = 27, kModEnv = 28, kVoice = 29, kFirstLfo = 30, kMatrix = 33, kMacros = 34,
                  kFirstLateEffect = 35, kNumEffects = 16;

    inline int oscGroup (int oscIndex)  { return oscIndex < 4 ? kFirstOsc + oscIndex : kFirstLateOsc + oscIndex - 4; }
    inline int effectGroup (int module) { return module < 10 ? kFirstEffect + module : kFirstLateEffect + module - 10; }
    inline int lfoGroup (int lfoIndex)  { return kFirstLfo + lfoIndex; }

    inline const fjord::base::locks::LockSet& set()
    {
        static const auto lockSet = []
        {
            fjord::base::locks::LockSet s;
            for (const auto& row : table) s.add ({ row.property, row.label, row.prefixes });
            return s;
        }();
        return lockSet;
    }

    inline bool isLocked (const juce::ValueTree& state, int group)       { return set().isLocked (state, group); }
    inline void setLocked (juce::ValueTree state, int group, bool locked) { set().setLocked (state, group, locked); }
    inline void unlockAll (juce::ValueTree state)                        { set().unlockAll (state); }
}
