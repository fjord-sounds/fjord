#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ui/Theme.h"
#include "Parameters.h"

FjordProcessor::FjordProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "FjordState", fjord::params::createLayout()),
      paramReader (apvts),
      fxReader (apvts),
      rhythmReader (apvts)
{
    apvts.state.addListener (this);
    syncFxOrderFromState();
    fjord::dsp::WavetableBank::get();   // compute the wavetables once up front, not on the audio thread
}

void FjordProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock);
    fxChain.prepare (sampleRate, samplesPerBlock);
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

    const int total = buffer.getNumSamples();
    if (total <= maxBlockSize)
        return processChunk (buffer, midi, time);

    // Larger block than prepared: split it (MIDI and song position move along).
    for (int start = 0; start < total; start += maxBlockSize)
    {
        const int n = std::min (maxBlockSize, total - start);
        juce::AudioBuffer<float> part (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), start, n);
        juce::MidiBuffer partMidi;
        for (const auto meta : midi)
            if (meta.samplePosition >= start && meta.samplePosition < start + n)
                partMidi.addEvent (meta.getMessage(), meta.samplePosition - start);
        auto partTime = time;
        partTime.ppq += time.bpm / 60.0 * start / currentSampleRate;
        processChunk (part, partMidi, partTime);
    }
    midi.clear();
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

    engine.process (buffer, midi, engineParams, time);
    midi.clear();

    // Per-preset level trim (before the rhythm effects and the effect chain)
    presetLevel.setTargetValue (juce::Decibels::decibelsToGain (apvts.getRawParameterValue (fjord::params::presetLevel)->load()));
    presetLevel.applyGain (buffer, buffer.getNumSamples());

    stepGate.process (buffer, clock, rhythmParams.gate);
    stutter.process (buffer, clock, rhythmParams.stutter, engine.getModWheel());
    gateStep.store (stepGate.currentStep());

    fxReader.read (fxParams, engineParams, [this] (fjord::dsp::ModSource src) { return engine.globalSource (src, engineParams); });
    fjord::fx::Order order;
    for (size_t i = 0; i < order.size(); ++i)
        order[i] = fxOrder[i].load (std::memory_order_relaxed);
    fxChain.process (buffer, fxParams, order, time.bpm);
    const bool compOn = fxParams.on[(size_t) fjord::fx::FxType::Comp];
    compReduction.store (compOn ? fxChain.getCompressorReduction() : 0.0f);
    compInput.store (compOn ? fxChain.getCompressorInput() : 0.0f);
    compOutput.store (compOn ? fxChain.getCompressorOutput() : 0.0f);
    pump.process (buffer, clock, rhythmParams.pump);   // after the effects: reverb and delay pump along

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

    // Peak meter: keep the highest value until the UI picks it up (lock-free max)
    for (int ch = 0; ch < 2; ++ch)
    {
        const float peak = buffer.getMagnitude (ch, 0, buffer.getNumSamples());
        auto& slot = outputPeak[(size_t) ch];
        float prev = slot.load (std::memory_order_relaxed);
        while (peak > prev && ! slot.compare_exchange_weak (prev, peak, std::memory_order_relaxed)) {}
    }
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

fjord::fx::Order FjordProcessor::getFxOrder() const
{
    fjord::fx::Order o;
    for (size_t i = 0; i < o.size(); ++i)
        o[i] = fxOrder[i].load();
    return o;
}

void FjordProcessor::setFxOrder (const fjord::fx::Order& o)
{
    apvts.state.setProperty ("fxOrder", fjord::fx::orderToString (o), nullptr);   // triggers syncFxOrderFromState
}

void FjordProcessor::syncFxOrderFromState()
{
    const auto o = fjord::fx::orderFromString (apvts.state.getProperty ("fxOrder").toString());
    for (size_t i = 0; i < o.size(); ++i)
        fxOrder[i].store (o[i]);
}

void FjordProcessor::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& id)
{
    if (tree == apvts.state && id == juce::Identifier ("fxOrder"))
        syncFxOrderFromState();
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
   #ifdef JucePlugin_VersionString
    juce::String version = JucePlugin_VersionString;
   #else
    juce::String version = "dev";
   #endif
   #if JUCE_ARM
    const juce::String arch = "arm64";
   #elif JUCE_64BIT
    const juce::String arch = "x86_64";
   #else
    const juce::String arch = "32-bit";
   #endif

    juce::StringArray lines;
    // The home folder is shortened to "~" so a pasted report does not show the user name
    const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getFullPathName();
    auto add = [&] (const juce::String& key, const juce::String& value)
    {
        lines.add (key.paddedRight (' ', 16) + (home.length() > 1 ? value.replace (home, "~") : value));
    };

    lines.add ("Fjord debug info");
    lines.add ("----------------");
   #ifdef FJORD_GIT_HASH
    const juce::String build = juce::String (FJORD_GIT_HASH) + ", ";
   #else
    const juce::String build;
   #endif
    add ("Version", version + " (" + build + arch + ", built " + __DATE__ + ")");
    add ("Format", juce::AudioProcessor::getWrapperTypeDescription (wrapperType));
    add ("Host", juce::PluginHostType().getHostDescription());
    add ("Plugin", juce::File::getSpecialLocation (juce::File::currentApplicationFile).getFullPathName());
    add ("User presets", fjord::PresetManager::getUserPresetFolder().getFullPathName());
    add ("Settings", fjord::ui::Theme::get().getSettingsFile().getFullPathName());
    add ("Sample rate", juce::String (getSampleRate(), 0) + " Hz, block " + juce::String (getBlockSize())
                        + " (prepared " + juce::String (maxBlockSize) + ")");
    add ("OS", juce::SystemStats::getOperatingSystemName() + (juce::SystemStats::isOperatingSystem64Bit() ? " 64-bit" : ""));
    add ("CPU", juce::SystemStats::getCpuModel().trim() + ", " + juce::String (juce::SystemStats::getNumCpus()) + " cores, "
                + juce::String (juce::SystemStats::getMemorySizeInMegabytes() / 1024) + " GB RAM");
    add ("JUCE", juce::SystemStats::getJUCEVersion());
    add ("Theme / scale", juce::String (fjord::ui::Theme::get().isDark() ? "dark" : "light") + ", "
                          + juce::String (juce::roundToInt (fjord::ui::Theme::get().getScale() * 100.0f)) + " %");

    int factory = 0, user = 0;
    for (const auto& p : presetManager.getPresets())
        (p.isFactory ? factory : user)++;
    add ("Presets", juce::String (factory) + " factory, " + juce::String (user) + " user");
    add ("Preset", presetManager.getCurrentName() + " (" + presetManager.getCurrentCategory() + ")");
    add ("FX order", apvts.state.getProperty ("fxOrder").toString());

    // Every parameter that differs from its default: enough to rebuild the sound for a bug report
    juce::StringArray changed;
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (std::abs (rp->getValue() - rp->getDefaultValue()) > 1.0e-5f)
                changed.add (rp->getParameterID() + "=" + juce::String (rp->convertFrom0to1 (rp->getValue()), 4));
    lines.add ("");
    lines.add ("Changed parameters (" + juce::String (changed.size()) + "):");
    lines.add (changed.joinIntoString (" "));
    return lines.joinIntoString ("\n");
}

juce::AudioProcessorEditor* FjordProcessor::createEditor()
{
    return new FjordEditor (*this);
}

namespace
{
    // Version of the saved project state (not the plugin version). Raise it when a change needs the old state
    // to be converted, and add the conversion to migrateState(). 0 = saved before the number existed.
    constexpr int kStateVersion = 1;

    void migrateState (juce::ValueTree& state)
    {
        const int saved = state.getProperty ("stateVersion", 0);
        // if (saved < 2) { ...convert what version 1 stored...; }
        juce::ignoreUnused (saved);
        state.setProperty ("stateVersion", kStateVersion, nullptr);
    }
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
            migrateState (apvts.state);
            presetManager.restoreFromState();
            syncFxOrderFromState();

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
