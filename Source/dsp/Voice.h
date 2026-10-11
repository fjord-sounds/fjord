#pragma once
#include "EngineParams.h"
#include "Envelope.h"
#include <vector>

namespace fjord::dsp
{
    class Voice
    {
    public:
        void prepare (float sampleRate, int maxBlock, uint32_t seed);

        // startNote: pitch to glide from (= note when there is no glide).
        void start (int note, float velocity, float startNote, bool retrigger, const EngineParams&);
        void glideTo (int note);
        void release();
        void kill();

        bool isActive() const    { return ampEnv.isActive(); }
        bool isReleasing() const { return ampEnv.isReleasing(); }
        int getNote() const      { return note; }
        uint64_t getAge() const  { return age; }
        float getLevel() const   { return ampEnv.getLevel(); }
        void setPolyAftertouch (float v) { polyAftertouch = v; }

        // Adds the voice to outL/outR.
        void render (float* outL, float* outR, int numSamples, const EngineParams&, const ModContext&);

    private:
        using Mods = std::array<float, (size_t) ModDest::Count>;

        void computeModulation (const EngineParams&, const ModContext&, int offset, int samples, Mods& mods);
        void updateControl (const EngineParams&, const ModContext&, const Mods&, int samples);
        void updateDrift (float analog, int samples);
        float nextNoise (bool pink);

        float sampleRate = 44100.0f, noiseScale = 1.0f;
        int note = 60;
        float velocity = 1.0f, polyAftertouch = 0.0f, noteRandom = 0.0f;
        float currentPitch = 60.0f, targetPitch = 60.0f;
        uint64_t age = 0;
        static inline uint64_t ageCounter = 0;
        double samplesSinceStart = 0.0;

        std::array<UnisonOscillator, kNumOsc> osc;
        float subPhase = 0.0f, subInc = 0.0f;
        std::array<float, 3> pinkState {};

        Filter filter1, filter2;
        Envelope ampEnv, filterEnv, modEnv;
        Random rng;
        Random rngLate;   // oscillators 5-8 draw from their own generator, so 4-oscillator sounds stay exactly as before

        Random& rngFor (int oscIndex) { return oscIndex < kNumBaseOsc ? rng : rngLate; }

        // Per-voice LFOs (retrigger only) – absolute phase in cycles
        std::array<double, 3> lfoPhase {};
        std::array<float, 3> lfoOut {};

        // Analog character: fixed deviations per note + slow drift
        std::array<float, kNumOsc> staticCents {}, driftCents {}, driftTarget {};
        float staticCutoff = 0.0f;
        int driftCountdown = 0;

        std::array<Smoother, kNumOsc> oscLevel;
        Smoother subLevel, noiseLevel, cutoff1, cutoff2, reso1, reso2, drive1, drive2;
        float volume = 1.0f, panL = 1.0f, panR = 1.0f;   // end values of the last control block

        // Values needed per sample within the control block
        float cutoff1Target = 0.0f, cutoff2Target = 0.0f, envRange1 = 0.0f, envRange2 = 0.0f;
        float reso1Target = 0.0f, reso2Target = 0.0f, drive1Target = 0.0f, drive2Target = 0.0f;

        std::array<float, kControlBlock> bufL {}, bufR {};
    };
}
