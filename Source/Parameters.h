#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/EngineParams.h"
#include <fjord_base/fx/FxParams.h>
#include "fx/Rhythm.h"

namespace fjord::params
{
    // Parameter IDs. Indexed groups are named "osc1_wave", "lfo2_rate", "mod5_amt", "macro3" etc.
    inline juce::String osc (int index, const char* name)   { return "osc" + juce::String (index + 1) + "_" + name; }
    inline juce::String lfo (int index, const char* name)   { return "lfo" + juce::String (index + 1) + "_" + name; }
    inline juce::String mod (int slot, const char* name)    { return "mod" + juce::String (slot + 1) + "_" + name; }
    inline juce::String macro (int index)                   { return "macro" + juce::String (index + 1); }

    inline constexpr auto masterGain   = "master_gain";
    inline constexpr auto presetLevel  = "preset_level";   // per-preset level trim (set by the factory presets' loudness matching)

    inline constexpr auto subLevel     = "sub_level";
    inline constexpr auto subOctave    = "sub_octave";
    inline constexpr auto subWave      = "sub_wave";
    inline constexpr auto noiseLevel   = "noise_level";
    inline constexpr auto noiseColor   = "noise_color";

    inline constexpr auto filterType   = "filter_type";
    inline constexpr auto cutoff       = "filter_cutoff";
    inline constexpr auto reso         = "filter_reso";
    inline constexpr auto drive        = "filter_drive";
    inline constexpr auto keyTrack     = "filter_keytrack";
    inline constexpr auto filterEnvAmt = "filter_env";
    inline constexpr auto filterVel    = "filter_vel";

    inline constexpr auto filter2On       = "filter2_on";
    inline constexpr auto filter2Type     = "filter2_type";
    inline constexpr auto cutoff2         = "filter2_cutoff";
    inline constexpr auto reso2           = "filter2_reso";
    inline constexpr auto drive2          = "filter2_drive";
    inline constexpr auto keyTrack2       = "filter2_keytrack";
    inline constexpr auto filter2EnvAmt   = "filter2_env";
    inline constexpr auto filterRouting   = "filter_routing";

    inline constexpr auto ampA = "amp_attack",  ampD = "amp_decay",  ampS = "amp_sustain",  ampR = "amp_release";
    inline constexpr auto fltA = "fenv_attack", fltD = "fenv_decay", fltS = "fenv_sustain", fltR = "fenv_release";
    inline constexpr auto modA = "menv_attack", modD = "menv_decay", modS = "menv_sustain", modR = "menv_release";
    inline constexpr auto ampVel = "amp_vel";

    inline constexpr auto voiceMode  = "voice_mode";
    inline constexpr auto polyphony  = "voice_poly";
    inline constexpr auto glide      = "voice_glide";
    inline constexpr auto bendRange  = "voice_bend";
    inline constexpr auto analog     = "voice_analog";
    inline constexpr auto keyFollow  = "voice_keyfollow";
    inline constexpr auto ampCurve   = "amp_curve";

    inline const juce::StringArray waveNames    { "Sine", "Triangle", "Saw", "Square", "Table" };
    inline const juce::StringArray filterNames  { "Ladder 24", "Ladder 12", "SVF Low", "SVF Band", "SVF High", "SVF Notch", "Comb", "Formant" };
    inline const juce::StringArray modeNames    { "Poly", "Mono", "Legato" };
    inline const juce::StringArray routingNames { "Serial", "Parallel" };

    using rhythm::arpModeNames;
    inline juce::String arpStep (int i, const char* lane) { return "arp_s" + juce::String (i + 1) + "_" + lane; }
    inline const juce::StringArray gateStepNames     { "8", "16", "32" };
    inline juce::String gateStep (int i) { return "gate_s" + juce::String (i + 1); }

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    struct RhythmParams
    {
        rhythm::ArpParams arp;
        rhythm::GateParams gate;
        rhythm::StutterParams stutter;
        rhythm::PumpParams pump;
    };

    class RhythmParamReader
    {
    public:
        explicit RhythmParamReader (juce::AudioProcessorValueTreeState&);
        void read (RhythmParams&) const;

    private:
        using Ref = std::atomic<float>*;
        static float v (Ref r) { return r->load (std::memory_order_relaxed); }
        Ref arpOn, arpMode, arpRate, arpOct, arpGate, arpSwing, arpLatch, arpSteps, arpRepeat, arpChance, arpRetrig,
            gateOn, gateSteps, gateRate, gateLength, gateSmooth, gateDepth, gateSwing,
            stOn, stInterval, stSlice, stChance, stDuration, stPitch, stDecay, stReverse, stMix, stTrigger, stStart, stGate,
            pumpOn, pumpRate, pumpDepth, pumpRelease, pumpCurve;
        std::array<Ref, rhythm::kMaxSteps> steps {};
        std::array<Ref, rhythm::kArpSteps> arpVel {}, arpPitch {}, arpTie {};
    };

    // The shared effect block of fjord-base (plain values) + Fjord's global modulation of the effects from the matrix
    class FxParamReader
    {
    public:
        using Values = std::array<float, fjord::base::fx::NumParams>;
        explicit FxParamReader (juce::AudioProcessorValueTreeState&);

        // globalSource returns the current value of a global mod source (or 0 for voice sources)
        void read (Values&, const dsp::EngineParams&, const std::function<float (dsp::ModSource)>& globalSource) const;

    private:
        std::array<std::atomic<float>*, fjord::base::fx::NumParams> raw {};
    };

    // Holds direct pointers to the parameter values and builds the engine parameters from them per block.
    class EngineParamReader
    {
    public:
        explicit EngineParamReader (juce::AudioProcessorValueTreeState&);
        void read (dsp::EngineParams&) const;

    private:
        using Ref = std::atomic<float>*;

        struct OscRefs { Ref on, wave, octave, semi, fine, level, pan, pw, unison, detune, spread, blend, retrig, table, wtPos; };
        struct FilterRefs { Ref on, type, cutoff, reso, drive, keyTrack, env; };
        struct LfoRefs { Ref shape, rate, sync, div, retrig, fade; };
        struct ModRefs { Ref src, dst, amt, via; };

        std::array<OscRefs, dsp::kNumOsc> osc;
        FilterRefs filter1, filter2;
        std::array<LfoRefs, 3> lfo;
        std::array<ModRefs, dsp::kNumModSlots> matrix;
        std::array<Ref, dsp::kNumMacros> macros;

        Ref subLevel, subOctave, subWave, noiseLevel, noiseColor, filterVel, routing,
            ampA, ampD, ampS, ampR, fltA, fltD, fltS, fltR, modA, modD, modS, modR, ampVel,
            voiceMode, polyphony, glide, bendRange, analog, keyFollow, ampCurve;
    };
}
