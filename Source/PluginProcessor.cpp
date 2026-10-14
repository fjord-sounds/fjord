#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ui/Theme.h"
#include "Parameters.h"

// Fjord inside the family design: northern lights in both themes (a deep aurora green by day, a glowing one at night).
const fjord::base::Brand& fjord::base::brand()
{
    static const Brand b = []
    {
        Brand x;
        x.name = "Fjord";
        x.logo = "fjord";
        x.tagline = "SOFT SYNTHESIZER";
        x.githubUrl = "https://github.com/fjord-sounds/fjord";
        x.settingsFolder = "Fjord/Fjord";
        x.legacySettingsFolders = { "Fjord", "Hell Audio/Fjord" };
        x.userPresetFolder = "Fjord/Presets";
        x.light = familyLight (juce::Colour (0xff0e7566));   // deep aurora green
        x.dark = familyDark (juce::Colour (0xff58d1a4));     // glowing aurora green
        x.gradientDark = { juce::Colour (0xff58d1a4), juce::Colour (0xff3fb8c8), juce::Colour (0xff5b8fe6) };
        x.gradientLight = { juce::Colour (0xff1f9a7c), juce::Colour (0xff1d7f93), juce::Colour (0xff2f68b0) };
        x.glow = { juce::Colour (0xff3fd8a0), juce::Colour (0xff2fb7c8), juce::Colour (0xff7a6fd8) };
        x.glowDark = 0.16f;
        x.glowLight = 0.10f;
        x.flash = juce::Colour (0xffd8fff0);
        x.categories = { "Pad", "Atmos", "Lead", "Pluck", "Keys", "Bass", "Arp", "Gated", "Drums", "FX" };
        x.categoryLabels = { { "Pad", "Pads" }, { "Lead", "Leads" }, { "Pluck", "Plucks" }, { "Arp", "Arps" } };
        x.tags = { "Warm", "Bright", "Dark", "Soft", "Airy", "Glassy", "Vocal", "Bell", "Gritty", "Acid", "Supersaw",
                   "Evolving", "Pulsing", "Arpeggiated", "Melody", "Gated", "Pumping", "Stutter",
                   "Wide", "Spacey", "Echo", "Dry",
                   "Short", "Slow Attack", "Long Tail", "Mono", "Glide", "Sub", "Layered", "Chord", "Velocity",
                   "Disco", "House", "Bedroom", "Retro", "Lo-Fi", "Ambient" };
        return x;
    }();
    return b;
}

FjordProcessor::FjordProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, &undoManager, "FjordState", fjord::params::createLayout()),
      paramReader (apvts),
      fxReader (apvts),
      rhythmReader (apvts)
{
    fjord::dsp::WavetableBank::get();   // compute the wavetables once up front, not on the audio thread
}

void FjordProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock);
    fxChain.prepare (sampleRate, samplesPerBlock);
    splitter.prepare (std::max (16, samplesPerBlock));
    currentSampleRate = sampleRate;
    maxBlockSize = std::max (16, samplesPerBlock);
    scope.sampleRate.store (sampleRate);
    arp.reset();
    arpWasOn = false;
    stepGate.prepare (sampleRate);
    stutter.prepare (sampleRate);
    keysDown = 0;
    masterGain.reset (sampleRate, 0.02);
    // Start at the set level right away (otherwise the first note fades in over 20 ms)
    masterGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (
        apvts.getRawParameterValue (fjord::params::masterGain)->load()));
    presetLevel.reset (sampleRate, 0.02);
    presetLevel.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (
        apvts.getRawParameterValue (fjord::params::presetLevel)->load()));
    dcCoef = (float) std::exp (-juce::MathConstants<double>::twoPi * 5.0 / sampleRate);
    dcX.fill (0.0f);
    dcY.fill (0.0f);
}

bool FjordProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void FjordProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    keyboardState.processNextMidiBuffer (midi, 0, buffer.getNumSamples(), true);

    if (panicRequested.exchange (false))
    {
        midi.clear();
        midi.addEvent (juce::MidiMessage::allNotesOff (1), 0);   // also clears the arp
        engine.releaseAll();
        keysDown = 0;
    }

    fjord::dsp::TimeInfo time;
    if (auto* head = getPlayHead())
        if (auto pos = head->getPosition())
        {
            time.playing = pos->getIsPlaying();
            time.bpm = pos->getBpm().orFallback (120.0);
            time.ppq = pos->getPpqPosition().orFallback (0.0);
        }
    if (! (time.bpm > 1.0 && time.bpm < 2000.0))
        time.bpm = 120.0;   // some hosts report 0 or nonsense while stopped

    // Larger block than prepared: split it (MIDI and song position move along).
    splitter.process (buffer, midi, [&] (juce::AudioBuffer<float>& part, juce::MidiBuffer& partMidi, int offset)
    {
        auto partTime = time;
        partTime.ppq += time.bpm / 60.0 * offset / currentSampleRate;
        processChunk (part, partMidi, partTime);
    });
}

void FjordProcessor::processChunk (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi, const fjord::dsp::TimeInfo& time)
{
    paramReader.read (engineParams);
    rhythmReader.read (rhythmParams);
    const auto clock = makeClock (time, midi, buffer.getNumSamples());

    // Arp is switched on while directly played notes are sounding: the arp would swallow their
    // note-offs. So these notes are released cleanly now.
    if (rhythmParams.arp.on && ! arpWasOn)
        engine.releaseAll();
    arpWasOn = rhythmParams.arp.on;

    arp.process (midi, buffer.getNumSamples(), clock, rhythmParams.arp);
    arpStep.store (arp.currentPatternStep());

    // Note-ons (played and from the arp): the effect filter's envelope, the logo dot
    int numNotes = 0;
    for (const auto meta : midi)
        if (meta.getMessage().isNoteOn() && numNotes < (int) noteOnPositions.size())
        {
            noteOnPositions[(size_t) numNotes++] = meta.samplePosition;
            lastVelocity.store (meta.getMessage().getFloatVelocity(), std::memory_order_relaxed);
        }
    if (numNotes > 0) noteCount.fetch_add ((uint32_t) numNotes, std::memory_order_relaxed);

    engine.process (buffer, midi, engineParams, time);
    midi.clear();


    stepGate.process (buffer, clock, rhythmParams.gate);
    stutter.process (buffer, clock, rhythmParams.stutter, engine.getModWheel());
    gateStep.store (stepGate.currentStep());

    fxReader.read (fxValues, engineParams, [this] (fjord::dsp::ModSource src) { return engine.globalSource (src, engineParams); });
    fjord::base::fx::Context ctx;
    ctx.bpm = time.bpm;
    ctx.ppq = time.ppq;
    ctx.playing = time.playing;
    ctx.triggers = noteOnPositions.data();
    ctx.numTriggers = numNotes;
    fxChain.process ({ buffer.getWritePointer (0), buffer.getWritePointer (1), buffer.getNumSamples() },
                     { fxValues.data() }, fxOrder.get(), ctx, &fxMeters);
    pump.process (buffer, clock, rhythmParams.pump);   // after the effects: reverb and delay pump along

    // Per-preset level trim (the factory presets' loudness matching), after the effects so it does not change how
    // hard the saturation and the compressor work; then the user's master level
    presetLevel.setTargetValue (juce::Decibels::decibelsToGain (apvts.getRawParameterValue (fjord::params::presetLevel)->load()));
    presetLevel.applyGain (buffer, buffer.getNumSamples());
    masterGain.setTargetValue (juce::Decibels::decibelsToGain (
        apvts.getRawParameterValue (fjord::params::masterGain)->load()));
    masterGain.applyGain (buffer, buffer.getNumSamples());

    // DC blocker (5 Hz) and gentle safety limiter: transparent below 0.7, soft saturation above.
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        auto* d = buffer.getWritePointer (ch);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const float in = d[i];
            dcY[(size_t) ch] = in - dcX[(size_t) ch] + dcCoef * dcY[(size_t) ch];
            dcX[(size_t) ch] = in;
            const float x = dcY[(size_t) ch], a = std::abs (x);
            d[i] = x;
            if (a > 0.7f)
                d[i] = std::copysign (0.7f + 0.3f * fjord::dsp::softClip ((a - 0.7f) / 0.3f), x);
        }
    }

    scope.push (buffer.getReadPointer (0), buffer.getReadPointer (1), buffer.getNumSamples());

    outputPeaks.push (buffer, 0, buffer.getNumSamples());
}

// Time base for arp, gate, stutter and pump. When the host is playing, its song position is used.
// When it is stopped, an internal clock starts with the first note (step 1 lands exactly on the hit).
fjord::rhythm::Clock FjordProcessor::makeClock (const fjord::dsp::TimeInfo& time, const juce::MidiBuffer& midi, int numSamples)
{
    fjord::rhythm::Clock clock;
    clock.beatsPerSample = time.bpm / 60.0 / currentSampleRate;

    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isNoteOn())
        {
            if (keysDown == 0 && ! arp.hasHeldNotes() && ! time.playing)
                freePpq = -clock.beatsPerSample * meta.samplePosition;
            ++keysDown;
        }
        else if (m.isNoteOff())
        {
            keysDown = std::max (0, keysDown - 1);
        }
        else if (m.isAllNotesOff() || m.isAllSoundOff())
        {
            keysDown = 0;
        }
    }

    const bool notesActive = keysDown > 0 || arp.hasHeldNotes();
    beatsSinceRelease = notesActive ? 0.0 : beatsSinceRelease + clock.beatsPerSample * numSamples;

    clock.ppq = time.playing ? time.ppq : freePpq;
    clock.running = time.playing || notesActive || beatsSinceRelease < 8.0;   // tails stay in time
    freePpq += clock.beatsPerSample * numSamples;
    return clock;
}


int FjordProcessor::getNumPrograms()
{
    int n = 0;
    for (const auto& p : presetManager.getPresets())
        n += p.isFactory ? 1 : 0;
    return std::max (1, n);
}

int FjordProcessor::getCurrentProgram()
{
    return std::max (0, presetManager.getCurrentIndex());
}

void FjordProcessor::setCurrentProgram (int index)
{
    if (index != presetManager.getCurrentIndex() && index < getNumPrograms())
        presetManager.load (index);
}

const juce::String FjordProcessor::getProgramName (int index)
{
    const auto& list = presetManager.getPresets();
    return juce::isPositiveAndBelow (index, (int) list.size()) ? list[(size_t) index].name : juce::String ("Init");
}

juce::String FjordProcessor::getDebugReport() const
{
    fjord::base::plugin::DebugReport report (*this, const_cast<fjord::PresetManager*> (&presetManager), maxBlockSize);
    report.add ("FX order", apvts.state.getProperty ("fxOrder").toString());
    return report.finish();
}

juce::AudioProcessorEditor* FjordProcessor::createEditor()
{
    return new FjordEditor (*this);
}

namespace
{
    // Version of the saved project state (not the plugin version). Raise it when a change needs the old state
    // to be converted (fjord-base/source/fjord_base/presets/Migration.h helps), and convert it in setStateInformation.
    constexpr int kStateVersion = 1;
}

void FjordProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    apvts.state.setProperty ("stateVersion", kStateVersion, nullptr);
    apvts.state.setProperty ("darkTheme", fjord::ui::Theme::get().isDark(), nullptr);   // remember the mode in the project too
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void FjordProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            presetManager.restoreFromState();
            fxOrder.sync();
            undoManager.clearUndoHistory();

            if (apvts.state.hasProperty ("darkTheme"))
            {
                const bool dark = apvts.state.getProperty ("darkTheme");
                juce::MessageManager::callAsync ([dark] { fjord::ui::Theme::get().setDark (dark); });
            }
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FjordProcessor();
}
