#pragma once
#include "Oscillator.h"
#include "Filter.h"
#include "Modulation.h"

namespace fjord::dsp
{
    enum class VoiceMode { Poly, Mono, Legato };

    struct OscParams
    {
        bool on = true;
        Wave wave = Wave::Saw;
        int octave = 0, semi = 0;
        float fine = 0.0f;          // Cent
        float level = 0.8f, pan = 0.0f, pw = 0.5f;
        int unison = 1;
        float detune = 0.25f, spread = 0.5f, blend = 0.75f;
        bool retrig = false;
        int table = 0;
        float wtPos = 0.0f;
    };

    struct EnvParams { float attack = 0.005f, decay = 0.4f, sustain = 0.8f, release = 0.3f; };

    struct FilterParams
    {
        bool on = true;
        FilterType type = FilterType::Ladder24;
        float cutoff = 8000.0f, reso = 0.1f, drive = 0.0f, keyTrack = 0.5f, envAmount = 0.0f;
    };

    struct LfoParams
    {
        LfoShape shape = LfoShape::Sine;
        float rateHz = 1.0f;
        bool sync = false;
        double beats = 1.0;          // cycle length in quarter notes when synced
        bool retrig = false;         // true: per voice from note-on, false: free running and global
        float fade = 0.0f;           // fade-in time in seconds
    };

    // Snapshot of all parameters for one audio block.
    struct EngineParams
    {
        std::array<OscParams, kNumOsc> osc;

        float subLevel = 0.0f;
        int subOctave = -1;
        bool subSquare = false;

        float noiseLevel = 0.0f;
        bool noisePink = false;

        FilterParams filter1, filter2 { false };
        bool filterParallel = false;
        float filterVelocity = 0.0f;

        EnvParams ampEnv, filterEnv, modEnv;
        float ampVelocity = 0.5f;

        std::array<LfoParams, 3> lfo;
        std::array<float, kNumMacros> macros {};
        std::array<ModSlot, kNumModSlots> matrix {};

        VoiceMode voiceMode = VoiceMode::Poly;
        int polyphony = 16;
        float glide = 0.0f;
        float bendRange = 2.0f;
        float analog = 0.0f;         // 0..1 drift and component tolerances
        float keyFollow = 1.0f;      // 0 = fixed pitch (drums), 1 = pitch follows the key
        float ampCurve = 0.0f;       // shape of the amp decay/release
    };

    // Global modulation values for a render segment (from the engine to the voices).
    struct ModContext
    {
        float pitchBend = 0.0f, modWheel = 0.0f, aftertouch = 0.0f;
        std::array<double, 3> lfoPhase {};   // absolute phase of the free LFOs at segment start
        std::array<double, 3> lfoInc {};     // cycles per sample
        double bpm = 120.0;
    };
}
