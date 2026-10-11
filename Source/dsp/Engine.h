#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include "Voice.h"

namespace fjord::dsp
{
    struct TimeInfo
    {
        bool playing = false;
        double bpm = 120.0;
        double ppq = 0.0;          // position in quarter notes at block start
    };

    // Voice management: poly/mono/legato, voice stealing, glide, pitch bend, sustain pedal.
    class Engine
    {
    public:
        void prepare (double sampleRate, int maxBlockSize);
        void process (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi, const EngineParams& params,
                      const TimeInfo& time);
        void allNotesOff();
        void releaseAll();   // send all voices into their release stage (click-free)

        // Value of a global mod source at block start (for effect modulation); voice sources return 0.
        float globalSource (ModSource, const EngineParams&) const;
        float getModWheel() const { return ctx.modWheel; }

    private:
        void handleMidi (const juce::MidiMessage&, const EngineParams&);
        void noteOn (int note, float velocity, const EngineParams&);
        void noteOff (int note, const EngineParams&);
        Voice* findVoiceToUse (const EngineParams&);
        void renderVoices (juce::AudioBuffer<float>&, int start, int numSamples, const EngineParams&);
        void updateLfoClock (const EngineParams&, const TimeInfo&, int numSamples);

        std::array<Voice, kMaxVoices> voices;
        std::array<bool, 128> keyDown {};
        std::array<bool, 128> sustainedNote {};
        std::vector<int> noteStack;   // held keys in the order they were played (mono modes)

        bool sustainPedal = false;
        double sampleRate = 44100.0;
        ModContext ctx;
        std::array<double, 3> freePhase {};   // free LFO phase when the host is not playing
        std::array<double, 3> blockPhase {};  // LFO phase at the start of the current block
        float lastNote = -1.0f;
    };
}
