#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include "Parameters.h"
#include "dsp/Engine.h"
#include "presets/PresetManager.h"
#include "presets/FxPresetManager.h"
#include "ui/Displays.h"
#include "fx/FxChain.h"

class FjordProcessor : public juce::AudioProcessor,
                       private juce::ValueTree::Listener
{
public:
    FjordProcessor();
    ~FjordProcessor() override { apvts.state.removeListener (this); }

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    // Factory presets also show up in the host's preset / program list.
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int) override;
    const juce::String getProgramName (int) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    double getTailLengthSeconds() const override { return 8.0; }   // reverb and delay tails

    juce::AudioProcessorValueTreeState apvts;
    juce::MidiKeyboardState keyboardState;   // on-screen keyboard in the editor
    fjord::PresetManager presetManager { apvts };
    fjord::FxPresetManager fxPresetManager { apvts };
    fjord::ui::ScopeBuffer scope;            // output signal for the oscilloscope

    // Effect order (stored in the state and in presets as "fxOrder")
    fjord::fx::Order getFxOrder() const;
    void setFxOrder (const fjord::fx::Order&);
    float getCompressorReduction() const { return compReduction.load(); }
    // Compressor meters for the Comp page: linear peaks of the last block (0 while the compressor is off)
    float getCompressorInput() const  { return compInput.load(); }
    float getCompressorOutput() const { return compOutput.load(); }
    int getGateStep() const { return gateStep.load(); }
    int getArpStep() const { return arpStep.load(); }

    // Panic: ends all notes on the next audio block (including arp and latched notes).
    void requestPanic() { panicRequested.store (true); }

    // Output peaks (linear, per channel) since the last call – for the level display in the header.
    std::array<float, 2> takeOutputPeaks()
    {
        return { outputPeak[0].exchange (0.0f, std::memory_order_relaxed), outputPeak[1].exchange (0.0f, std::memory_order_relaxed) };
    }

    // Plain-text report for the About panel (version, host, paths, system, current sound).
    juce::String getDebugReport() const;

private:
    // Renders at most maxBlockSize samples (hosts may send larger blocks than announced).
    void processChunk (juce::AudioBuffer<float>&, juce::MidiBuffer&, const fjord::dsp::TimeInfo&);
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void syncFxOrderFromState();

    fjord::params::EngineParamReader paramReader;
    fjord::params::FxParamReader fxReader;
    fjord::fx::FxParams fxParams;
    fjord::fx::FxChain fxChain;
    std::array<std::atomic<int>, fjord::fx::kNumFx> fxOrder {};
    std::atomic<float> compReduction { 0.0f }, compInput { 0.0f }, compOutput { 0.0f };

    // Rhythm
    fjord::rhythm::Clock makeClock (const fjord::dsp::TimeInfo&, const juce::MidiBuffer&, int numSamples);
    fjord::params::RhythmParamReader rhythmReader;
    fjord::params::RhythmParams rhythmParams;
    fjord::rhythm::Arpeggiator arp;
    fjord::rhythm::StepGate stepGate;
    fjord::rhythm::Stutter stutter;
    fjord::rhythm::Pump pump;
    double freePpq = 0.0, currentSampleRate = 44100.0, beatsSinceRelease = 1.0e9;
    int maxBlockSize = 512;
    int keysDown = 0;
    bool arpWasOn = false;
    std::atomic<int> gateStep { -1 }, arpStep { -1 };
    std::atomic<bool> panicRequested { false };
    std::array<std::atomic<float>, 2> outputPeak {};
    fjord::dsp::EngineParams engineParams;
    fjord::dsp::Engine engine;
    juce::SmoothedValue<float> masterGain, presetLevel;
    std::array<float, 2> dcX {}, dcY {};
    float dcCoef = 0.9995f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FjordProcessor)
};
