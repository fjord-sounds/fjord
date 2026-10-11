#include "Parameters.h"
#include "dsp/Wavetable.h"

namespace fjord::params
{
    namespace
    {
        using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;
        using Range  = juce::NormalisableRange<float>;

        juce::String fmtHz (float v, int)
        {
            return v < 1000.0f ? juce::String (v, 0) + " Hz" : juce::String (v / 1000.0f, 2) + " kHz";
        }

        juce::String fmtTime (float v, int)
        {
            return v < 1.0f ? juce::String (v * 1000.0f, v < 0.1f ? 1 : 0) + " ms" : juce::String (v, 2) + " s";
        }

        juce::String fmtPercent (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; }
        juce::String fmtBipolar (float v, int) { return (v > 0.0f ? "+" : "") + juce::String (juce::roundToInt (v * 100.0f)) + " %"; }
        juce::String fmtCents (float v, int)   { return (v > 0.0f ? "+" : "") + juce::String (juce::roundToInt (v)) + " ct"; }
        juce::String fmtDb (float v, int)      { return juce::String (v, 1) + " dB"; }

        juce::String fmtPan (float v, int)
        {
            const int p = juce::roundToInt (v * 100.0f);
            return p == 0 ? juce::String ("C") : (p < 0 ? "L" + juce::String (-p) : "R" + juce::String (p));
        }

        Range logRange (float lo, float hi)
        {
            return { lo, hi,
                     [] (float s, float e, float n) { return s * std::pow (e / s, n); },
                     [] (float s, float e, float v) { return std::log (v / s) / std::log (e / s); } };
        }

        Range timeRange (float maxSeconds, float centre)
        {
            Range r (0.0f, maxSeconds);
            r.setSkewForCentre (centre);
            return r;
        }

        void addFloat (Layout& l, const juce::String& id, const juce::String& name, Range range, float def,
                       juce::String (*fmt) (float, int))
        {
            l.add (std::make_unique<juce::AudioParameterFloat> (
                juce::ParameterID { id, 1 }, name, range, def,
                juce::AudioParameterFloatAttributes().withStringFromValueFunction (fmt)));
        }

        void addInt (Layout& l, const juce::String& id, const juce::String& name, int lo, int hi, int def,
                     const juce::String& suffix = {})
        {
            l.add (std::make_unique<juce::AudioParameterInt> (
                juce::ParameterID { id, 1 }, name, lo, hi, def,
                juce::AudioParameterIntAttributes().withStringFromValueFunction (
                    [suffix] (int v, int)
                    {
                        const bool signedValue = suffix == " st" || suffix == " oct";
                        return (v > 0 && signedValue ? "+" : "") + juce::String (v) + suffix;
                    })));
        }

        void addChoice (Layout& l, const juce::String& id, const juce::String& name, const juce::StringArray& items, int def)
        {
            l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id, 1 }, name, items, def));
        }

        void addBool (Layout& l, const juce::String& id, const juce::String& name, bool def)
        {
            l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, 1 }, name, def));
        }
    }

    Layout createLayout()
    {
        Layout layout;

        // Oscillators 1-4 sit at the front; 5-8 were added later and are appended at the end of the layout.
        auto addOsc = [&layout] (int i)
        {
            const auto n = "Osc " + juce::String (i + 1) + " ";
            addBool   (layout, osc (i, "on"),     n + "On", i == 0);
            addChoice (layout, osc (i, "wave"),   n + "Wave", waveNames, 2);
            addInt    (layout, osc (i, "octave"), n + "Octave", -3, 3, 0, " oct");
            addInt    (layout, osc (i, "semi"),   n + "Semi", -12, 12, 0, " st");
            addFloat  (layout, osc (i, "fine"),   n + "Fine", Range (-100.0f, 100.0f), 0.0f, fmtCents);
            addFloat  (layout, osc (i, "level"),  n + "Level", Range (0.0f, 1.0f), 0.8f, fmtPercent);
            addFloat  (layout, osc (i, "pan"),    n + "Pan", Range (-1.0f, 1.0f), 0.0f, fmtPan);
            addFloat  (layout, osc (i, "pw"),     n + "Pulse Width", Range (0.05f, 0.95f), 0.5f, fmtPercent);
            addInt    (layout, osc (i, "unison"), n + "Unison", 1, dsp::kMaxUnison, 1, " voices");
            addFloat  (layout, osc (i, "detune"), n + "Detune", Range (0.0f, 1.0f), 0.25f, fmtPercent);
            addFloat  (layout, osc (i, "spread"), n + "Stereo Spread", Range (0.0f, 1.0f), 0.6f, fmtPercent);
            addFloat  (layout, osc (i, "blend"),  n + "Unison Blend", Range (0.0f, 1.0f), 0.75f, fmtPercent);
            addBool   (layout, osc (i, "retrig"), n + "Phase Retrigger", false);
            addChoice (layout, osc (i, "table"),  n + "Wavetable", dsp::WavetableBank::names(), 0);
            addFloat  (layout, osc (i, "wtpos"),  n + "WT Position", Range (0.0f, 1.0f), 0.0f, fmtPercent);
        };
        for (int i = 0; i < dsp::kNumBaseOsc; ++i)
            addOsc (i);

        addFloat  (layout, subLevel,   "Sub Level", Range (0.0f, 1.0f), 0.0f, fmtPercent);
        addChoice (layout, subOctave,  "Sub Octave", { "-1 oct", "-2 oct" }, 0);
        addChoice (layout, subWave,    "Sub Wave", { "Sine", "Square" }, 0);
        addFloat  (layout, noiseLevel, "Noise Level", Range (0.0f, 1.0f), 0.0f, fmtPercent);
        addChoice (layout, noiseColor, "Noise Color", { "White", "Pink" }, 1);

        addChoice (layout, filterType,   "Filter Type", filterNames, 0);
        addFloat  (layout, cutoff,       "Cutoff", logRange (20.0f, 20000.0f), 6000.0f, fmtHz);
        addFloat  (layout, reso,         "Resonance", Range (0.0f, 1.0f), 0.15f, fmtPercent);
        addFloat  (layout, drive,        "Filter Drive", Range (0.0f, 1.0f), 0.1f, fmtPercent);
        addFloat  (layout, keyTrack,     "Key Track", Range (0.0f, 1.0f), 0.5f, fmtPercent);
        addFloat  (layout, filterEnvAmt, "Filter Env Amount", Range (-1.0f, 1.0f), 0.0f, fmtBipolar);
        addFloat  (layout, filterVel,    "Filter Velocity", Range (0.0f, 1.0f), 0.0f, fmtPercent);

        addBool   (layout, filter2On,     "Filter 2 On", false);
        addChoice (layout, filter2Type,   "Filter 2 Type", filterNames, 4);
        addFloat  (layout, cutoff2,       "Cutoff 2", logRange (20.0f, 20000.0f), 120.0f, fmtHz);
        addFloat  (layout, reso2,         "Resonance 2", Range (0.0f, 1.0f), 0.1f, fmtPercent);
        addFloat  (layout, drive2,        "Filter 2 Drive", Range (0.0f, 1.0f), 0.0f, fmtPercent);
        addFloat  (layout, keyTrack2,     "Key Track 2", Range (0.0f, 1.0f), 0.0f, fmtPercent);
        addFloat  (layout, filter2EnvAmt, "Filter 2 Env Amount", Range (-1.0f, 1.0f), 0.0f, fmtBipolar);
        addChoice (layout, filterRouting, "Filter Routing", routingNames, 0);

        addFloat (layout, ampA, "Amp Attack",  timeRange (20.0f, 0.5f), 0.003f, fmtTime);
        addFloat (layout, ampD, "Amp Decay",   timeRange (20.0f, 0.8f), 0.6f, fmtTime);
        addFloat (layout, ampS, "Amp Sustain", Range (0.0f, 1.0f), 0.8f, fmtPercent);
        addFloat (layout, ampR, "Amp Release", timeRange (20.0f, 0.8f), 0.25f, fmtTime);
        addFloat (layout, ampVel, "Amp Velocity", Range (0.0f, 1.0f), 0.4f, fmtPercent);

        addFloat (layout, fltA, "Filter Env Attack",  timeRange (20.0f, 0.5f), 0.003f, fmtTime);
        addFloat (layout, fltD, "Filter Env Decay",   timeRange (20.0f, 0.8f), 0.5f, fmtTime);
        addFloat (layout, fltS, "Filter Env Sustain", Range (0.0f, 1.0f), 0.3f, fmtPercent);
        addFloat (layout, fltR, "Filter Env Release", timeRange (20.0f, 0.8f), 0.3f, fmtTime);

        addFloat (layout, modA, "Mod Env Attack",  timeRange (20.0f, 0.5f), 0.003f, fmtTime);
        addFloat (layout, modD, "Mod Env Decay",   timeRange (20.0f, 0.8f), 0.5f, fmtTime);
        addFloat (layout, modS, "Mod Env Sustain", Range (0.0f, 1.0f), 0.0f, fmtPercent);
        addFloat (layout, modR, "Mod Env Release", timeRange (20.0f, 0.8f), 0.3f, fmtTime);

        for (int i = 0; i < 3; ++i)
        {
            const auto n = "LFO " + juce::String (i + 1) + " ";
            addChoice (layout, lfo (i, "shape"),  n + "Shape", dsp::lfoShapeNames(), 0);
            addFloat  (layout, lfo (i, "rate"),   n + "Rate", logRange (0.02f, 40.0f), 1.0f,
                       [] (float v, int) { return juce::String (v, v < 1.0f ? 2 : 1) + " Hz"; });
            addBool   (layout, lfo (i, "sync"),   n + "Sync", false);
            addChoice (layout, lfo (i, "div"),    n + "Division", dsp::divisionNames(), 7);
            addBool   (layout, lfo (i, "retrig"), n + "Retrigger", false);
            addFloat  (layout, lfo (i, "fade"),   n + "Fade In", timeRange (10.0f, 1.0f), 0.0f, fmtTime);
        }

        for (int i = 0; i < dsp::kNumMacros; ++i)
            addFloat (layout, macro (i), "Macro " + juce::String (i + 1), Range (0.0f, 1.0f), 0.0f, fmtPercent);

        for (int i = 0; i < dsp::kNumModSlots; ++i)
        {
            const auto n = "Mod " + juce::String (i + 1) + " ";
            addChoice (layout, mod (i, "src"), n + "Source", dsp::modSourceNames(), 0);
            addChoice (layout, mod (i, "dst"), n + "Destination", dsp::modDestNames(), 0);
            addFloat  (layout, mod (i, "amt"), n + "Amount", Range (-1.0f, 1.0f), 0.0f, fmtBipolar);
            addChoice (layout, mod (i, "via"), n + "Via", dsp::modSourceNames(), 0);
        }

        // ---------------------------------------------------------------- Effects
        using fx::FxType;
        auto fxOn = [&] (FxType t, bool def) { addBool (layout, fx ((int) t, "on"), fx::fxNames()[(int) t] + " On", def); };
        auto fxName = [] (FxType t, const char* n) { return fx::fxNames()[(int) t] + " " + n; };
        auto fmtMs = [] (float v, int) { return juce::String (juce::roundToInt (v)) + " ms"; };

        fxOn (FxType::Drive, false);
        addChoice (layout, fx (0, "type"),   fxName (FxType::Drive, "Type"), driveTypeNames, 0);
        addFloat  (layout, fx (0, "amount"), fxName (FxType::Drive, "Amount"), Range (0.0f, 1.0f), 0.3f, fmtPercent);
        addFloat  (layout, fx (0, "tone"),   fxName (FxType::Drive, "Tone"), Range (0.0f, 1.0f), 0.75f, fmtPercent);
        addFloat  (layout, fx (0, "mix"),    fxName (FxType::Drive, "Mix"), Range (0.0f, 1.0f), 1.0f, fmtPercent);

        fxOn (FxType::Crush, false);
        addFloat (layout, fx (1, "bits"), fxName (FxType::Crush, "Bits"), Range (2.0f, 16.0f), 10.0f,
                  [] (float v, int) { return juce::String (v, 1) + " bit"; });
        addFloat (layout, fx (1, "rate"), fxName (FxType::Crush, "Downsample"), logRange (1.0f, 40.0f), 1.0f,
                  [] (float v, int) { return juce::String (v, 1) + "x"; });
        addFloat (layout, fx (1, "mix"),  fxName (FxType::Crush, "Mix"), Range (0.0f, 1.0f), 1.0f, fmtPercent);

        fxOn (FxType::Eq, false);
        // Parametric EQ. IDs of the original five-band EQ are kept, so older presets load unchanged.
        auto fmtQ = [] (float v, int) { return "Q " + juce::String (v, 2); };
        const juce::StringArray slopeNames { "12 dB", "24 dB" };
        addFloat  (layout, fx (2, "lowcut"),   fxName (FxType::Eq, "Low Cut"), logRange (20.0f, 2000.0f), 20.0f, fmtHz);
        addChoice (layout, fx (2, "lcslope"),  fxName (FxType::Eq, "Low Cut Slope"), slopeNames, 0);
        addFloat  (layout, fx (2, "lowfreq"),  fxName (FxType::Eq, "Low Freq"), logRange (30.0f, 1000.0f), 120.0f, fmtHz);
        addFloat  (layout, fx (2, "low"),      fxName (FxType::Eq, "Low"), Range (-18.0f, 18.0f), 0.0f, fmtDb);
        addFloat  (layout, fx (2, "midfreq"),  fxName (FxType::Eq, "Mid Freq"), logRange (40.0f, 16000.0f), 1000.0f, fmtHz);
        addFloat  (layout, fx (2, "mid"),      fxName (FxType::Eq, "Mid"), Range (-18.0f, 18.0f), 0.0f, fmtDb);
        addFloat  (layout, fx (2, "midq"),     fxName (FxType::Eq, "Mid Q"), logRange (0.2f, 12.0f), 0.9f, fmtQ);
        addFloat  (layout, fx (2, "mid2freq"), fxName (FxType::Eq, "Mid 2 Freq"), logRange (40.0f, 16000.0f), 3000.0f, fmtHz);
        addFloat  (layout, fx (2, "mid2"),     fxName (FxType::Eq, "Mid 2"), Range (-18.0f, 18.0f), 0.0f, fmtDb);
        addFloat  (layout, fx (2, "mid2q"),    fxName (FxType::Eq, "Mid 2 Q"), logRange (0.2f, 12.0f), 0.9f, fmtQ);
        addFloat  (layout, fx (2, "highfreq"), fxName (FxType::Eq, "High Freq"), logRange (1000.0f, 16000.0f), 7000.0f, fmtHz);
        addFloat  (layout, fx (2, "high"),     fxName (FxType::Eq, "High"), Range (-18.0f, 18.0f), 0.0f, fmtDb);
        addFloat  (layout, fx (2, "highcut"),  fxName (FxType::Eq, "High Cut"), logRange (200.0f, 20000.0f), 20000.0f, fmtHz);
        addChoice (layout, fx (2, "hcslope"),  fxName (FxType::Eq, "High Cut Slope"), slopeNames, 0);

        fxOn (FxType::Comp, false);
        addFloat (layout, fx (3, "thresh"),  fxName (FxType::Comp, "Threshold"), Range (-48.0f, 0.0f), -18.0f, fmtDb);
        addFloat (layout, fx (3, "ratio"),   fxName (FxType::Comp, "Ratio"), logRange (1.0f, 20.0f), 3.0f,
                  [] (float v, int) { return juce::String (v, 1) + ":1"; });
        addFloat (layout, fx (3, "attack"),  fxName (FxType::Comp, "Attack"), timeRange (0.2f, 0.02f), 0.01f, fmtTime);
        addFloat (layout, fx (3, "release"), fxName (FxType::Comp, "Release"), timeRange (1.5f, 0.2f), 0.15f, fmtTime);
        addFloat (layout, fx (3, "makeup"),  fxName (FxType::Comp, "Makeup"), Range (0.0f, 24.0f), 0.0f, fmtDb);
        addFloat (layout, fx (3, "mix"),     fxName (FxType::Comp, "Mix"), Range (0.0f, 1.0f), 1.0f, fmtPercent);

        fxOn (FxType::Chorus, false);
        addFloat (layout, fx (4, "rate"),  fxName (FxType::Chorus, "Rate"), logRange (0.05f, 5.0f), 0.5f,
                  [] (float v, int) { return juce::String (v, 2) + " Hz"; });
        addFloat (layout, fx (4, "depth"), fxName (FxType::Chorus, "Depth"), Range (0.0f, 1.0f), 0.5f, fmtPercent);
        addFloat (layout, fx (4, "mix"),   fxName (FxType::Chorus, "Mix"), Range (0.0f, 1.0f), 0.5f, fmtPercent);

        fxOn (FxType::Phaser, false);
        addFloat (layout, fx (5, "rate"),     fxName (FxType::Phaser, "Rate"), logRange (0.02f, 5.0f), 0.3f,
                  [] (float v, int) { return juce::String (v, 2) + " Hz"; });
        addFloat (layout, fx (5, "depth"),    fxName (FxType::Phaser, "Depth"), Range (0.0f, 1.0f), 0.6f, fmtPercent);
        addFloat (layout, fx (5, "feedback"), fxName (FxType::Phaser, "Feedback"), Range (0.0f, 1.0f), 0.4f, fmtPercent);
        addFloat (layout, fx (5, "mix"),      fxName (FxType::Phaser, "Mix"), Range (0.0f, 1.0f), 0.6f, fmtPercent);

        fxOn (FxType::Flanger, false);
        addFloat (layout, fx (6, "rate"),     fxName (FxType::Flanger, "Rate"), logRange (0.02f, 5.0f), 0.2f,
                  [] (float v, int) { return juce::String (v, 2) + " Hz"; });
        addFloat (layout, fx (6, "depth"),    fxName (FxType::Flanger, "Depth"), Range (0.0f, 1.0f), 0.6f, fmtPercent);
        addFloat (layout, fx (6, "feedback"), fxName (FxType::Flanger, "Feedback"), Range (0.0f, 1.0f), 0.5f, fmtPercent);
        addFloat (layout, fx (6, "mix"),      fxName (FxType::Flanger, "Mix"), Range (0.0f, 1.0f), 0.5f, fmtPercent);

        fxOn (FxType::Delay, false);
        addBool   (layout, fx (7, "sync"),     fxName (FxType::Delay, "Sync"), true);
        addChoice (layout, fx (7, "div"),      fxName (FxType::Delay, "Division"), dsp::divisionNames(), 11);
        addFloat  (layout, fx (7, "time"),     fxName (FxType::Delay, "Time"), logRange (10.0f, 2000.0f), 350.0f, fmtMs);
        addFloat  (layout, fx (7, "feedback"), fxName (FxType::Delay, "Feedback"), Range (0.0f, 0.95f), 0.35f, fmtPercent);
        addBool   (layout, fx (7, "pingpong"), fxName (FxType::Delay, "Ping Pong"), true);
        addFloat  (layout, fx (7, "damp"),     fxName (FxType::Delay, "Damp"), Range (0.0f, 1.0f), 0.4f, fmtPercent);
        addFloat  (layout, fx (7, "duck"),     fxName (FxType::Delay, "Ducking"), Range (0.0f, 1.0f), 0.0f, fmtPercent);
        addFloat  (layout, fx (7, "mix"),      fxName (FxType::Delay, "Mix"), Range (0.0f, 1.0f), 0.25f, fmtPercent);

        fxOn (FxType::Reverb, false);
        addFloat (layout, fx (8, "size"),     fxName (FxType::Reverb, "Size"), Range (0.0f, 1.0f), 0.6f, fmtPercent);
        addFloat (layout, fx (8, "decay"),    fxName (FxType::Reverb, "Decay"), logRange (0.3f, 20.0f), 3.0f, fmtTime);
        addFloat (layout, fx (8, "damp"),     fxName (FxType::Reverb, "Damp"), Range (0.0f, 1.0f), 0.5f, fmtPercent);
        addFloat (layout, fx (8, "predelay"), fxName (FxType::Reverb, "Pre-Delay"), Range (0.0f, 0.25f), 0.02f, fmtTime);
        addFloat (layout, fx (8, "width"),    fxName (FxType::Reverb, "Width"), Range (0.0f, 1.5f), 1.0f, fmtPercent);
        addFloat (layout, fx (8, "mix"),      fxName (FxType::Reverb, "Mix"), Range (0.0f, 1.0f), 0.25f, fmtPercent);

        fxOn (FxType::Width, false);
        addFloat (layout, fx (9, "amount"), fxName (FxType::Width, "Amount"), Range (0.0f, 2.0f), 1.0f, fmtPercent);

        // ---------------------------------------------------------------- Rhythm
        addBool   (layout, "arp_on",     "Arp On", false);
        addChoice (layout, "arp_mode",   "Arp Mode", arpModeNames, 0);
        addChoice (layout, "arp_rate",   "Arp Rate", dsp::divisionNames(), 13);
        addInt    (layout, "arp_oct",    "Arp Octaves", 1, 4, 1, " oct");
        addFloat  (layout, "arp_gate",   "Arp Gate", Range (0.05f, 1.0f), 0.6f, fmtPercent);
        addFloat  (layout, "arp_swing",  "Arp Swing", Range (0.0f, 0.7f), 0.0f, fmtPercent);
        addBool   (layout, "arp_latch",  "Arp Latch", false);
        addInt    (layout, "arp_steps",  "Arp Steps", 1, rhythm::kArpSteps, rhythm::kArpSteps, " steps");
        addInt    (layout, "arp_repeat", "Arp Repeats", 1, 4, 1, "x");
        addFloat  (layout, "arp_chance", "Arp Chance", Range (0.0f, 1.0f), 1.0f, fmtPercent);
        addBool   (layout, "arp_retrig", "Arp Retrigger", false);
        for (int i = 0; i < rhythm::kArpSteps; ++i)
        {
            const auto n = "Arp Step " + juce::String (i + 1) + " ";
            addFloat (layout, arpStep (i, "vel"),   n + "Velocity", Range (0.0f, 1.0f), 1.0f, fmtPercent);
            addInt   (layout, arpStep (i, "pitch"), n + "Pitch", -12, 12, 0, " st");
            addBool  (layout, arpStep (i, "tie"),   n + "Tie", false);
        }

        addBool   (layout, "gate_on",     "Gate On", false);
        addChoice (layout, "gate_steps",  "Gate Steps", gateStepNames, 1);
        addChoice (layout, "gate_rate",   "Gate Rate", dsp::divisionNames(), 13);
        addFloat  (layout, "gate_length", "Gate Length", Range (0.1f, 1.0f), 0.85f, fmtPercent);
        addFloat  (layout, "gate_smooth", "Gate Smooth", logRange (0.5f, 40.0f), 3.0f,
                   [] (float v, int) { return juce::String (v, 1) + " ms"; });
        addFloat  (layout, "gate_depth",  "Gate Depth", Range (0.0f, 1.0f), 1.0f, fmtPercent);
        addFloat  (layout, "gate_swing",  "Gate Swing", Range (0.0f, 0.7f), 0.0f, fmtPercent);
        for (int i = 0; i < rhythm::kMaxSteps; ++i)
            addFloat (layout, gateStep (i), "Gate Step " + juce::String (i + 1), Range (0.0f, 1.0f),
                      rhythm::patternValue (rhythm::gatePatterns[0].steps, i), fmtPercent);

        addBool   (layout, "stutter_on",       "Stutter On", false);
        addChoice (layout, "stutter_trigger",  "Stutter Trigger", { "Auto", "Mod Wheel" }, 0);
        addChoice (layout, "stutter_interval", "Stutter Every", rhythm::stutterEveryNames, 2);
        addChoice (layout, "stutter_slice",    "Stutter Slice", rhythm::stutterSliceNames, 2);
        addChoice (layout, "stutter_start",    "Stutter Start", rhythm::stutterStartNames, 2);
        addFloat  (layout, "stutter_chance",   "Stutter Chance", Range (0.0f, 1.0f), 0.5f, fmtPercent);
        addFloat  (layout, "stutter_duration", "Stutter Length", Range (0.1f, 1.0f), 1.0f, fmtPercent);
        addFloat  (layout, "stutter_gate",     "Stutter Gate", Range (0.1f, 1.0f), 1.0f, fmtPercent);
        addFloat  (layout, "stutter_pitch",    "Stutter Pitch", Range (-24.0f, 24.0f), 0.0f,
                   [] (float v, int) { return (v > 0 ? "+" : "") + juce::String (v, 1) + " st"; });
        addFloat  (layout, "stutter_decay",    "Stutter Decay", Range (0.0f, 0.5f), 0.0f, fmtPercent);
        addBool   (layout, "stutter_reverse",  "Stutter Reverse", false);
        addFloat  (layout, "stutter_mix",      "Stutter Mix", Range (0.0f, 1.0f), 1.0f, fmtPercent);

        addBool   (layout, "pump_on",      "Pump On", false);
        addChoice (layout, "pump_rate",    "Pump Rate", dsp::divisionNames(), 7);
        addFloat  (layout, "pump_depth",   "Pump Depth", Range (0.0f, 1.0f), 0.6f, fmtPercent);
        addFloat  (layout, "pump_release", "Pump Release", Range (0.1f, 1.0f), 0.6f, fmtPercent);
        addFloat  (layout, "pump_curve",   "Pump Curve", logRange (0.3f, 3.0f), 1.0f,
                   [] (float v, int) { return juce::String (v, 2); });

        addChoice (layout, voiceMode, "Voice Mode", modeNames, 0);
        addInt    (layout, polyphony, "Polyphony", 1, dsp::kMaxVoices, dsp::kMaxVoices, " voices");
        addFloat  (layout, glide,     "Glide", timeRange (5.0f, 0.3f), 0.0f, fmtTime);
        addInt    (layout, bendRange, "Bend Range", 0, 24, 2, " st");
        addFloat  (layout, analog,    "Analog", Range (0.0f, 1.0f), 0.0f, fmtPercent);
        addFloat  (layout, keyFollow, "Key Follow", Range (0.0f, 1.0f), 1.0f, fmtPercent);
        addFloat  (layout, ampCurve,  "Amp Curve", Range (0.0f, 1.0f), 0.0f, fmtPercent);

        addFloat (layout, masterGain, "Master", Range (-48.0f, 6.0f, 0.01f), -3.0f, fmtDb);
        addFloat (layout, presetLevel, "Preset Level", Range (-24.0f, 24.0f, 0.01f), 0.0f, fmtDb);

        for (int i = dsp::kNumBaseOsc; i < dsp::kNumOsc; ++i)
            addOsc (i);

        // Compressor additions (defaults keep the original behaviour: 6 dB knee, no sidechain filter, manual makeup)
        addFloat (layout, fx (3, "knee"),     fxName (FxType::Comp, "Knee"), Range (0.0f, 24.0f), 6.0f, fmtDb);
        addFloat (layout, fx (3, "schpf"),    fxName (FxType::Comp, "Sidechain HPF"), logRange (20.0f, 500.0f), 20.0f,
                  [] (float v, int) { return v < 20.5f ? juce::String ("Off") : juce::String (juce::roundToInt (v)) + " Hz"; });
        addBool  (layout, fx (3, "autogain"), fxName (FxType::Comp, "Auto Gain"), false);

        return layout;
    }

    EngineParamReader::EngineParamReader (juce::AudioProcessorValueTreeState& s)
    {
        auto get = [&s] (const juce::String& id)
        {
            auto* p = s.getRawParameterValue (id);
            jassert (p != nullptr);
            return p;
        };

        for (int i = 0; i < dsp::kNumOsc; ++i)
        {
            auto o = [&] (const char* n) { return get (params::osc (i, n)); };
            osc[(size_t) i] = { o ("on"), o ("wave"), o ("octave"), o ("semi"), o ("fine"), o ("level"), o ("pan"), o ("pw"),
                                o ("unison"), o ("detune"), o ("spread"), o ("blend"), o ("retrig"), o ("table"), o ("wtpos") };
        }

        filter1 = { nullptr, get (filterType), get (cutoff), get (reso), get (drive), get (keyTrack), get (filterEnvAmt) };
        filter2 = { get (filter2On), get (filter2Type), get (cutoff2), get (reso2), get (drive2), get (keyTrack2), get (filter2EnvAmt) };

        for (int i = 0; i < 3; ++i)
        {
            auto l = [&] (const char* n) { return get (params::lfo (i, n)); };
            lfo[(size_t) i] = { l ("shape"), l ("rate"), l ("sync"), l ("div"), l ("retrig"), l ("fade") };
        }

        for (int i = 0; i < dsp::kNumModSlots; ++i)
        {
            auto m = [&] (const char* n) { return get (params::mod (i, n)); };
            matrix[(size_t) i] = { m ("src"), m ("dst"), m ("amt"), m ("via") };
        }

        for (int i = 0; i < dsp::kNumMacros; ++i)
            macros[(size_t) i] = get (params::macro (i));

        subLevel = get (params::subLevel);   subOctave = get (params::subOctave); subWave = get (params::subWave);
        noiseLevel = get (params::noiseLevel); noiseColor = get (params::noiseColor);
        filterVel = get (params::filterVel); routing = get (params::filterRouting);
        ampA = get (params::ampA); ampD = get (params::ampD); ampS = get (params::ampS); ampR = get (params::ampR);
        fltA = get (params::fltA); fltD = get (params::fltD); fltS = get (params::fltS); fltR = get (params::fltR);
        modA = get (params::modA); modD = get (params::modD); modS = get (params::modS); modR = get (params::modR);
        ampVel = get (params::ampVel);
        voiceMode = get (params::voiceMode); polyphony = get (params::polyphony);
        glide = get (params::glide); bendRange = get (params::bendRange); analog = get (params::analog);
        keyFollow = get (params::keyFollow); ampCurve = get (params::ampCurve);
    }

    void EngineParamReader::read (dsp::EngineParams& p) const
    {
        auto f = [] (Ref a) { return a->load (std::memory_order_relaxed); };
        auto i = [&f] (Ref a) { return juce::roundToInt (f (a)); };
        auto b = [&f] (Ref a) { return f (a) > 0.5f; };

        for (size_t k = 0; k < osc.size(); ++k)
        {
            const auto& r = osc[k];
            auto& o = p.osc[k];
            o.on = b (r.on);
            o.wave = (dsp::Wave) i (r.wave);
            o.octave = i (r.octave);
            o.semi = i (r.semi);
            o.fine = f (r.fine);
            o.level = f (r.level);
            o.pan = f (r.pan);
            o.pw = f (r.pw);
            o.unison = i (r.unison);
            o.detune = f (r.detune);
            o.spread = f (r.spread);
            o.blend = f (r.blend);
            o.retrig = b (r.retrig);
            o.table = i (r.table);
            o.wtPos = f (r.wtPos);
        }

        auto readFilter = [&] (const FilterRefs& r, dsp::FilterParams& out)
        {
            out.on = r.on == nullptr || b (r.on);
            out.type = (dsp::FilterType) i (r.type);
            out.cutoff = f (r.cutoff);
            out.reso = f (r.reso);
            out.drive = f (r.drive);
            out.keyTrack = f (r.keyTrack);
            out.envAmount = f (r.env);
        };
        readFilter (filter1, p.filter1);
        readFilter (filter2, p.filter2);
        p.filterParallel = i (routing) == 1;
        p.filterVelocity = f (filterVel);

        for (size_t k = 0; k < lfo.size(); ++k)
        {
            const auto& r = lfo[k];
            auto& l = p.lfo[k];
            l.shape = (dsp::LfoShape) i (r.shape);
            l.rateHz = f (r.rate);
            l.sync = b (r.sync);
            l.beats = dsp::divisions[(size_t) std::clamp (i (r.div), 0, dsp::numDivisions - 1)].beats;
            l.retrig = b (r.retrig);
            l.fade = f (r.fade);
        }

        for (size_t k = 0; k < matrix.size(); ++k)
        {
            const auto& r = matrix[k];
            p.matrix[k] = { (dsp::ModSource) i (r.src), (dsp::ModDest) i (r.dst), f (r.amt), (dsp::ModSource) i (r.via) };
        }

        for (size_t k = 0; k < macros.size(); ++k)
            p.macros[k] = f (macros[k]);

        p.subLevel = f (subLevel) * 0.8f;
        p.subOctave = i (subOctave) == 0 ? -1 : -2;
        p.subSquare = i (subWave) == 1;
        p.noiseLevel = f (noiseLevel) * f (noiseLevel);
        p.noisePink = i (noiseColor) == 1;

        p.ampEnv = { f (ampA), f (ampD), f (ampS), f (ampR) };
        p.filterEnv = { f (fltA), f (fltD), f (fltS), f (fltR) };
        p.modEnv = { f (modA), f (modD), f (modS), f (modR) };
        p.ampVelocity = f (ampVel);

        p.voiceMode = (dsp::VoiceMode) i (voiceMode);
        p.polyphony = i (polyphony);
        p.glide = f (glide);
        p.bendRange = (float) i (bendRange);
        p.analog = f (analog);
        p.keyFollow = f (keyFollow);
        p.ampCurve = f (ampCurve);
    }

    // ---------------------------------------------------------------------------------------------
    FxParamReader::FxParamReader (juce::AudioProcessorValueTreeState& s)
    {
        for (int t = 0; t < fx::kNumFx; ++t)
            onRefs[(size_t) t] = s.getRawParameterValue (fx (t, "on"));
        r0_amount = s.getRawParameterValue (fx (0, "amount"));
        r0_mix = s.getRawParameterValue (fx (0, "mix"));
        r0_tone = s.getRawParameterValue (fx (0, "tone"));
        r0_type = s.getRawParameterValue (fx (0, "type"));
        r1_bits = s.getRawParameterValue (fx (1, "bits"));
        r1_mix = s.getRawParameterValue (fx (1, "mix"));
        r1_rate = s.getRawParameterValue (fx (1, "rate"));
        r2_high = s.getRawParameterValue (fx (2, "high"));
        r2_highcut = s.getRawParameterValue (fx (2, "highcut"));
        r2_low = s.getRawParameterValue (fx (2, "low"));
        r2_lowcut = s.getRawParameterValue (fx (2, "lowcut"));
        r2_mid = s.getRawParameterValue (fx (2, "mid"));
        r2_midfreq = s.getRawParameterValue (fx (2, "midfreq"));
        r2_lcslope = s.getRawParameterValue (fx (2, "lcslope"));
        r2_lowfreq = s.getRawParameterValue (fx (2, "lowfreq"));
        r2_midq = s.getRawParameterValue (fx (2, "midq"));
        r2_mid2freq = s.getRawParameterValue (fx (2, "mid2freq"));
        r2_mid2 = s.getRawParameterValue (fx (2, "mid2"));
        r2_mid2q = s.getRawParameterValue (fx (2, "mid2q"));
        r2_highfreq = s.getRawParameterValue (fx (2, "highfreq"));
        r2_hcslope = s.getRawParameterValue (fx (2, "hcslope"));
        r3_attack = s.getRawParameterValue (fx (3, "attack"));
        r3_makeup = s.getRawParameterValue (fx (3, "makeup"));
        r3_mix = s.getRawParameterValue (fx (3, "mix"));
        r3_ratio = s.getRawParameterValue (fx (3, "ratio"));
        r3_release = s.getRawParameterValue (fx (3, "release"));
        r3_thresh = s.getRawParameterValue (fx (3, "thresh"));
        r4_depth = s.getRawParameterValue (fx (4, "depth"));
        r4_mix = s.getRawParameterValue (fx (4, "mix"));
        r4_rate = s.getRawParameterValue (fx (4, "rate"));
        r5_depth = s.getRawParameterValue (fx (5, "depth"));
        r5_feedback = s.getRawParameterValue (fx (5, "feedback"));
        r5_mix = s.getRawParameterValue (fx (5, "mix"));
        r5_rate = s.getRawParameterValue (fx (5, "rate"));
        r6_depth = s.getRawParameterValue (fx (6, "depth"));
        r6_feedback = s.getRawParameterValue (fx (6, "feedback"));
        r6_mix = s.getRawParameterValue (fx (6, "mix"));
        r6_rate = s.getRawParameterValue (fx (6, "rate"));
        r7_damp = s.getRawParameterValue (fx (7, "damp"));
        r7_div = s.getRawParameterValue (fx (7, "div"));
        r7_duck = s.getRawParameterValue (fx (7, "duck"));
        r7_feedback = s.getRawParameterValue (fx (7, "feedback"));
        r7_mix = s.getRawParameterValue (fx (7, "mix"));
        r7_pingpong = s.getRawParameterValue (fx (7, "pingpong"));
        r7_sync = s.getRawParameterValue (fx (7, "sync"));
        r7_time = s.getRawParameterValue (fx (7, "time"));
        r8_damp = s.getRawParameterValue (fx (8, "damp"));
        r8_decay = s.getRawParameterValue (fx (8, "decay"));
        r8_mix = s.getRawParameterValue (fx (8, "mix"));
        r8_predelay = s.getRawParameterValue (fx (8, "predelay"));
        r8_size = s.getRawParameterValue (fx (8, "size"));
        r8_width = s.getRawParameterValue (fx (8, "width"));
        r9_amount = s.getRawParameterValue (fx (9, "amount"));
        r3_knee = s.getRawParameterValue (fx (3, "knee"));
        r3_schpf = s.getRawParameterValue (fx (3, "schpf"));
        r3_autogain = s.getRawParameterValue (fx (3, "autogain"));
    }

    void FxParamReader::read (fx::FxParams& p, const dsp::EngineParams& ep,
                              const std::function<float (dsp::ModSource)>& globalSource) const
    {
        using dsp::ModDest;

        // Global modulation of the effects from the matrix
        std::array<float, (size_t) ModDest::Count> m {};
        for (const auto& slot : ep.matrix)
        {
            if ((int) slot.dest < dsp::kFirstFxDest || (int) slot.dest > dsp::kLastFxDest
                || slot.source == dsp::ModSource::None || slot.amount == 0.0f)
                continue;
            float v = globalSource (slot.source) * slot.amount;
            if (slot.via != dsp::ModSource::None)
                v *= globalSource (slot.via);
            m[(size_t) slot.dest] += v;
        }
        auto mod01 = [&m] (float base, ModDest d) { return std::clamp (base + m[(size_t) d], 0.0f, 1.0f); };

        for (int t = 0; t < fx::kNumFx; ++t)
            p.on[(size_t) t] = v (onRefs[(size_t) t]) > 0.5f;

        p.driveType = juce::roundToInt (v (r0_type));
        p.driveAmount = mod01 (v (r0_amount), ModDest::FxDrive);
        p.driveTone = v (r0_tone);
        p.driveMix = v (r0_mix);

        p.crushBits = v (r1_bits);
        p.crushRate = v (r1_rate);
        p.crushMix = mod01 (v (r1_mix), ModDest::CrushMix);

        auto& e = p.eq;
        e.lowCut = v (r2_lowcut);
        e.lowCutSlope = juce::roundToInt (v (r2_lcslope));
        e.lowFreq = v (r2_lowfreq);
        e.low = v (r2_low);
        e.midFreq = v (r2_midfreq);
        e.mid = v (r2_mid);
        e.midQ = v (r2_midq);
        e.mid2Freq = v (r2_mid2freq);
        e.mid2 = v (r2_mid2);
        e.mid2Q = v (r2_mid2q);
        e.highFreq = v (r2_highfreq);
        e.high = v (r2_high);
        e.highCut = v (r2_highcut);
        e.highCutSlope = juce::roundToInt (v (r2_hcslope));

        p.compThreshold = v (r3_thresh);
        p.compRatio = v (r3_ratio);
        p.compAttack = v (r3_attack);
        p.compRelease = v (r3_release);
        p.compMakeup = v (r3_makeup);
        p.compMix = v (r3_mix);
        p.compKnee = v (r3_knee);
        p.compScHpf = v (r3_schpf);
        p.compAutoGain = v (r3_autogain) > 0.5f;

        p.chorusRate = v (r4_rate);
        p.chorusDepth = v (r4_depth);
        p.chorusMix = mod01 (v (r4_mix), ModDest::ChorusMix);

        p.phaserRate = v (r5_rate);
        p.phaserDepth = v (r5_depth);
        p.phaserFeedback = v (r5_feedback);
        p.phaserMix = mod01 (v (r5_mix), ModDest::PhaserMix);

        p.flangerRate = v (r6_rate);
        p.flangerDepth = v (r6_depth);
        p.flangerFeedback = v (r6_feedback);
        p.flangerMix = mod01 (v (r6_mix), ModDest::FlangerMix);

        p.delaySync = v (r7_sync) > 0.5f;
        p.delayBeats = dsp::divisions[(size_t) std::clamp (juce::roundToInt (v (r7_div)), 0, dsp::numDivisions - 1)].beats;
        p.delayTimeMs = v (r7_time);
        p.delayFeedback = std::clamp (v (r7_feedback) + m[(size_t) ModDest::DelayFeedback], 0.0f, 0.95f);
        p.delayPingPong = v (r7_pingpong) > 0.5f;
        p.delayDamp = v (r7_damp);
        p.delayDuck = v (r7_duck);
        p.delayMix = mod01 (v (r7_mix), ModDest::DelayMix);

        p.reverbSize = mod01 (v (r8_size), ModDest::ReverbSize);
        p.reverbDecay = v (r8_decay);
        p.reverbDamp = v (r8_damp);
        p.reverbPredelay = v (r8_predelay);
        p.reverbWidth = v (r8_width);
        p.reverbMix = mod01 (v (r8_mix), ModDest::ReverbMix);

        p.width = std::clamp (v (r9_amount) + m[(size_t) ModDest::StereoWidth], 0.0f, 2.0f);
    }

    // ---------------------------------------------------------------------------------------------
    RhythmParamReader::RhythmParamReader (juce::AudioProcessorValueTreeState& s)
    {
        auto g = [&s] (const juce::String& id) { auto* p = s.getRawParameterValue (id); jassert (p != nullptr); return p; };
        arpOn = g ("arp_on"); arpMode = g ("arp_mode"); arpRate = g ("arp_rate"); arpOct = g ("arp_oct");
        arpGate = g ("arp_gate"); arpSwing = g ("arp_swing"); arpLatch = g ("arp_latch");
        arpSteps = g ("arp_steps"); arpRepeat = g ("arp_repeat"); arpChance = g ("arp_chance"); arpRetrig = g ("arp_retrig");
        for (int i = 0; i < rhythm::kArpSteps; ++i)
        {
            arpVel[(size_t) i] = g (arpStep (i, "vel"));
            arpPitch[(size_t) i] = g (arpStep (i, "pitch"));
            arpTie[(size_t) i] = g (arpStep (i, "tie"));
        }
        gateOn = g ("gate_on"); gateSteps = g ("gate_steps"); gateRate = g ("gate_rate"); gateLength = g ("gate_length");
        gateSmooth = g ("gate_smooth"); gateDepth = g ("gate_depth"); gateSwing = g ("gate_swing");
        stOn = g ("stutter_on"); stInterval = g ("stutter_interval"); stSlice = g ("stutter_slice");
        stTrigger = g ("stutter_trigger"); stStart = g ("stutter_start"); stGate = g ("stutter_gate");
        stChance = g ("stutter_chance"); stDuration = g ("stutter_duration"); stPitch = g ("stutter_pitch");
        stDecay = g ("stutter_decay"); stReverse = g ("stutter_reverse"); stMix = g ("stutter_mix");
        pumpOn = g ("pump_on"); pumpRate = g ("pump_rate"); pumpDepth = g ("pump_depth");
        pumpRelease = g ("pump_release"); pumpCurve = g ("pump_curve");
        for (int i = 0; i < rhythm::kMaxSteps; ++i)
            steps[(size_t) i] = g (gateStep (i));
    }

    void RhythmParamReader::read (RhythmParams& p) const
    {
        auto beats = [] (Ref r) { return dsp::divisions[(size_t) std::clamp (juce::roundToInt (v (r)), 0, dsp::numDivisions - 1)].beats; };

        p.arp.on = v (arpOn) > 0.5f;
        p.arp.mode = juce::roundToInt (v (arpMode));
        p.arp.stepBeats = beats (arpRate);
        p.arp.octaves = juce::roundToInt (v (arpOct));
        p.arp.gate = v (arpGate);
        p.arp.swing = v (arpSwing);
        p.arp.latch = v (arpLatch) > 0.5f;
        p.arp.steps = juce::roundToInt (v (arpSteps));
        p.arp.repeats = juce::roundToInt (v (arpRepeat));
        p.arp.chance = v (arpChance);
        p.arp.retrigger = v (arpRetrig) > 0.5f;
        for (size_t i = 0; i < (size_t) rhythm::kArpSteps; ++i)
        {
            p.arp.velocity[i] = v (arpVel[i]);
            p.arp.pitch[i] = juce::roundToInt (v (arpPitch[i]));
            p.arp.tie[i] = v (arpTie[i]) > 0.5f;
        }

        p.gate.on = v (gateOn) > 0.5f;
        p.gate.steps = 8 << std::clamp (juce::roundToInt (v (gateSteps)), 0, 2);
        p.gate.stepBeats = beats (gateRate);
        p.gate.length = v (gateLength);
        p.gate.smooth = v (gateSmooth);
        p.gate.depth = v (gateDepth);
        p.gate.swing = v (gateSwing);
        for (size_t i = 0; i < steps.size(); ++i)
            p.gate.values[i] = v (steps[i]);

        p.stutter.on = v (stOn) > 0.5f;
        p.stutter.modWheelTrigger = juce::roundToInt (v (stTrigger)) == 1;
        p.stutter.intervalBeats = rhythm::stutterEveryBeats[std::clamp (juce::roundToInt (v (stInterval)), 0, 3)];
        p.stutter.sliceBeats = rhythm::stutterSliceBeats[std::clamp (juce::roundToInt (v (stSlice)), 0, 4)];
        p.stutter.startFraction = rhythm::stutterStartFraction[std::clamp (juce::roundToInt (v (stStart)), 0, 3)];
        p.stutter.chance = v (stChance);
        p.stutter.length = v (stDuration);
        p.stutter.gate = v (stGate);
        p.stutter.pitch = v (stPitch);
        p.stutter.decay = v (stDecay);
        p.stutter.reverse = v (stReverse) > 0.5f;
        p.stutter.mix = v (stMix);

        p.pump.on = v (pumpOn) > 0.5f;
        p.pump.beats = beats (pumpRate);
        p.pump.depth = v (pumpDepth);
        p.pump.release = v (pumpRelease);
        p.pump.curve = v (pumpCurve);
    }
}
