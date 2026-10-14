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

        // The effects: the shared block of fjord-base (the same effects in every Fjord plugin)
        fjord::base::params::addToLayout (layout, fjord::base::fx::specs());

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
        for (int i = 0; i < fjord::base::fx::NumParams; ++i)
        {
            raw[(size_t) i] = s.getRawParameterValue (fjord::base::fx::paramId ((fjord::base::fx::Param) i));
            jassert (raw[(size_t) i] != nullptr);
        }
    }

    void FxParamReader::read (Values& out, const dsp::EngineParams& ep,
                              const std::function<float (dsp::ModSource)>& globalSource) const
    {
        using dsp::ModDest;
        namespace fx = fjord::base::fx;

        for (size_t i = 0; i < raw.size(); ++i)
            out[i] = raw[i]->load (std::memory_order_relaxed);

        // Global modulation of the effects from the matrix (Fjord's effect destinations)
        std::array<float, (size_t) ModDest::Count> m {};
        bool any = false;
        for (const auto& slot : ep.matrix)
        {
            if ((int) slot.dest < dsp::kFirstFxDest || (int) slot.dest > dsp::kLastFxDest
                || slot.source == dsp::ModSource::None || slot.amount == 0.0f)
                continue;
            float v = globalSource (slot.source) * slot.amount;
            if (slot.via != dsp::ModSource::None)
                v *= globalSource (slot.via);
            m[(size_t) slot.dest] += v;
            any = true;
        }
        if (! any) return;

        auto add01 = [&] (fx::Param p, float amount) { out[(size_t) p] = std::clamp (out[(size_t) p] + amount, 0.0f, 1.0f); };
        add01 (fx::FxSatDrive, m[(size_t) ModDest::FxDrive]);
        add01 (fx::FxMotMix, m[(size_t) ModDest::ChorusMix] + m[(size_t) ModDest::PhaserMix] + m[(size_t) ModDest::FlangerMix]);
        add01 (fx::FxDlyMix, m[(size_t) ModDest::DelayMix]);
        add01 (fx::FxVerbMix, m[(size_t) ModDest::ReverbMix]);
        add01 (fx::FxVerbSize, m[(size_t) ModDest::ReverbSize]);
        add01 (fx::FxLofiMix, m[(size_t) ModDest::CrushMix]);
        out[(size_t) fx::FxDlyFeedback] = std::clamp (out[(size_t) fx::FxDlyFeedback] + m[(size_t) ModDest::DelayFeedback], 0.0f, 0.95f);
        out[(size_t) fx::FxWidthAmount] = std::clamp (out[(size_t) fx::FxWidthAmount] + m[(size_t) ModDest::StereoWidth], 0.0f, 2.0f);
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
