// Offline test: renders Fjord without a DAW and checks for NaN/clipping/DC/silence and stuck notes.
#include <juce_audio_formats/juce_audio_formats.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ui/Theme.h"
#include "ui/PresetBrowser.h"
#include "ui/ModPage.h"
#include "ui/PlayPage.h"
#include "ui/FxPage.h"
#include "ui/RhythmPage.h"
#include "ui/ArpPage.h"

namespace
{
    struct Case
    {
        juce::String name;
        std::function<void (juce::AudioProcessorValueTreeState&)> setup;
        std::vector<int> notes;
        bool stress = false;   // full load may reach the limiter
        int presetIndex = -1;
    };

    void set (juce::AudioProcessorValueTreeState& s, const juce::String& id, float value)
    {
        auto* p = s.getParameter (id);
        jassert (p != nullptr);
        p->setValueNotifyingHost (p->convertTo0to1 (value));
    }

    double testSampleRate = 48000.0;

    bool render (const Case& c, const juce::File& outDir)
    {
        FjordProcessor proc;
        const double sr = testSampleRate;
        const int block = 512;
        proc.setPlayConfigDetails (0, 2, sr, block);
        proc.prepareToPlay (sr, block);
        if (c.presetIndex >= 0) proc.presetManager.load (c.presetIndex);
        c.setup (proc.apvts);

        const int total = (int) (sr * 4.0);
        juce::AudioBuffer<float> out (2, total), buf (2, block);
        double sum = 0.0, cpuSeconds = 0.0;
        float peak = 0.0f;
        bool finite = true;

        for (int pos = 0; pos < total; pos += block)
        {
            juce::MidiBuffer midi;
            const int n = std::min (block, total - pos);
            for (int note : c.notes)
            {
                if (pos == 0) midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 100), 0);
                if (pos <= (int) (sr * 2.5) && pos + block > (int) (sr * 2.5))
                    midi.addEvent (juce::MidiMessage::noteOff (1, note), (int) (sr * 2.5) - pos);
            }
            buf.setSize (2, n, false, false, true);
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            proc.processBlock (buf, midi);
            cpuSeconds += (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;

            for (int ch = 0; ch < 2; ++ch)
            {
                out.copyFrom (ch, pos, buf, ch, 0, n);
                for (int i = 0; i < n; ++i)
                {
                    const float v = buf.getSample (ch, i);
                    finite &= std::isfinite (v);
                    peak = std::max (peak, std::abs (v));
                    sum += v;
                }
            }
        }

        const double dc = sum / (2.0 * total);
        const double cpu = 100.0 * cpuSeconds / (total / sr);
        const bool ok = finite && (peak < 1.0f || c.stress) && peak > 0.01f && std::abs (dc) < 0.01;
        printf ("%-22s peak %.3f  dc %+.4f  cpu %5.2f%%  %s\n", c.name.toRawUTF8(), peak, dc, cpu, ok ? "OK" : "FAIL");

        // WAV files only on request (FJORD_WAV=1), otherwise each run writes several GB
        if (! juce::SystemStats::getEnvironmentVariable ("FJORD_WAV", {}).getIntValue())
            return ok;

        juce::WavAudioFormat wav;
        auto file = outDir.getChildFile (c.name + ".wav");
        file.deleteFile();
        if (auto stream = file.createOutputStream())
            if (auto writer = std::unique_ptr<juce::AudioFormatWriter> (wav.createWriterFor (stream.get(), sr, 2, 24, {}, 0)))
            {
                stream.release();
                writer->writeFromAudioSampleBuffer (out, 0, total);
            }
        return ok;
    }
}

// Checks scenarios in which notes used to get stuck. There must be silence at the end.
static int stuckNoteTests()
{
    const double sr = 48000.0;
    const int block = 512;
    struct Step { double seconds; std::function<void (FjordProcessor&, juce::MidiBuffer&)> action; };

    auto run = [&] (const char* name, std::vector<Step> steps)
    {
        FjordProcessor proc;
        proc.setPlayConfigDetails (0, 2, sr, block);
        proc.prepareToPlay (sr, block);
        set (proc.apvts, "amp_release", 0.1f);
        juce::AudioBuffer<float> buf (2, block);
        double tailEnergy = 0.0;
        long tailCount = 0;

        for (size_t si = 0; si < steps.size(); ++si)
        {
            juce::MidiBuffer midi;
            if (steps[si].action) steps[si].action (proc, midi);
            const int blocks = (int) (steps[si].seconds * sr / block);
            for (int b = 0; b < blocks; ++b)
            {
                proc.processBlock (buf, midi);
                midi.clear();
                if (si == steps.size() - 1 && b > blocks / 2)
                    for (int ch = 0; ch < 2; ++ch)
                        for (int k = 0; k < block; ++k) { tailEnergy += buf.getSample (ch, k) * buf.getSample (ch, k); ++tailCount; }
            }
        }
        const double db = 10.0 * std::log10 (tailEnergy / std::max (1L, tailCount) + 1.0e-20);
        const bool ok = db < -90.0;
        printf ("stuck_%-28s tail %7.1f dB  %s\n", name, db, ok ? "OK" : "FAIL");
        return ok ? 0 : 1;
    };

    auto noteOn = [] (FjordProcessor&, juce::MidiBuffer& m) { m.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0); };
    auto noteOff = [] (FjordProcessor&, juce::MidiBuffer& m) { m.addEvent (juce::MidiMessage::noteOff (1, 60), 0); };

    int fails = 0;
    fails += run ("arp_enabled_while_held", {
        { 0.5, noteOn },
        { 0.3, [] (FjordProcessor& p, juce::MidiBuffer&) { set (p.apvts, "arp_on", 1.0f); } },
        { 0.2, noteOff },
        { 1.5, nullptr } });
    fails += run ("all_notes_off_with_arp", {
        { 0.1, [] (FjordProcessor& p, juce::MidiBuffer& m) { set (p.apvts, "arp_on", 1.0f); m.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0); } },
        { 1.0, nullptr },
        { 1.5, [] (FjordProcessor&, juce::MidiBuffer& m) { m.addEvent (juce::MidiMessage::allNotesOff (1), 0); } } });
    fails += run ("latch_arp_switched_off", {
        { 0.1, [] (FjordProcessor& p, juce::MidiBuffer& m) { set (p.apvts, "arp_on", 1.0f); set (p.apvts, "arp_latch", 1.0f);
                                                             m.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0); } },
        { 0.5, noteOff },
        { 1.0, nullptr },
        { 1.5, [] (FjordProcessor& p, juce::MidiBuffer&) { set (p.apvts, "arp_on", 0.0f); } } });
    fails += run ("mono_legato_overlap", {
        { 0.2, [] (FjordProcessor& p, juce::MidiBuffer& m) { set (p.apvts, "voice_mode", 2.0f); m.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0); } },
        { 0.2, [] (FjordProcessor&, juce::MidiBuffer& m) { m.addEvent (juce::MidiMessage::noteOn (1, 64, (juce::uint8) 100), 0); } },
        { 0.2, noteOff },
        { 0.2, [] (FjordProcessor&, juce::MidiBuffer& m) { m.addEvent (juce::MidiMessage::noteOff (1, 64), 0); } },
        { 1.5, nullptr } });
    fails += run ("sustain_pedal", {
        { 0.2, [] (FjordProcessor&, juce::MidiBuffer& m) { m.addEvent (juce::MidiMessage::controllerEvent (1, 64, 127), 0);
                                                          m.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 1); } },
        { 0.2, noteOff },
        { 0.2, noteOn },
        { 0.2, noteOff },
        { 0.2, [] (FjordProcessor&, juce::MidiBuffer& m) { m.addEvent (juce::MidiMessage::controllerEvent (1, 64, 0), 0); } },
        { 1.5, nullptr } });
    fails += run ("panic_button", {
        { 0.1, [] (FjordProcessor& p, juce::MidiBuffer& m) { set (p.apvts, "arp_on", 1.0f); set (p.apvts, "arp_latch", 1.0f);
                                                             m.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
                                                             m.addEvent (juce::MidiMessage::noteOn (1, 64, (juce::uint8) 100), 0); } },
        { 1.0, nullptr },
        { 1.5, [] (FjordProcessor& p, juce::MidiBuffer&) { p.requestPanic(); } } });
    return fails;
}

// Switching the arp off (e.g. a preset change) while a key is pressed in the same block: the new note must sound.
static int arpSwitchTest()
{
    FjordProcessor proc;
    proc.setPlayConfigDetails (0, 2, 48000.0, 512);
    proc.prepareToPlay (48000.0, 512);
    juce::AudioBuffer<float> buf (2, 512);
    set (proc.apvts, "arp_on", 1.0f);
    for (int k = 0; k < 40; ++k)
    {
        juce::MidiBuffer m;
        if (k == 0) m.addEvent (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 100), 0);
        if (k == 30) m.addEvent (juce::MidiMessage::noteOff (1, 36), 0);
        proc.processBlock (buf, m);
    }
    set (proc.apvts, "arp_on", 0.0f);
    float peak = 0.0f;
    for (int k = 0; k < 40; ++k)
    {
        juce::MidiBuffer m;
        if (k == 0) m.addEvent (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 100), 0);
        proc.processBlock (buf, m);
        if (k > 20) peak = std::max (peak, buf.getMagnitude (0, 512));
    }
    const bool ok = peak > 0.01f;
    printf ("arp_switch_off_same_key peak %.3f  %s\n", peak, ok ? "OK" : "FAIL");
    return ok ? 0 : 1;
}

// Load FX presets + Lock FX when switching sound presets
static int fxPresetTests()
{
    FjordProcessor proc;
    auto& fxm = proc.fxPresetManager;
    auto value = [&] (const char* id) { auto* p = proc.apvts.getParameter (id); return p->convertFrom0to1 (p->getValue()); };
    int bigHall = -1;
    for (int i = 0; i < (int) fxm.getPresets().size(); ++i)
        if (fxm.getPresets()[(size_t) i].name == "Big Hall") bigHall = i;

    fxm.load (bigHall);
    const bool loaded = value ("fx_reverb_on") > 0.5f && std::abs (value ("fx_reverb_decay") - 6.0f) < 0.3f;
    fxm.setLocked (true);
    proc.presetManager.load (0);
    const bool kept = std::abs (value ("fx_reverb_decay") - 6.0f) < 0.3f && fxm.getCurrentName() == "Big Hall";
    fxm.setLocked (false);
    proc.presetManager.load (0);
    const bool replaced = fxm.getCurrentName().isEmpty();
    const bool ok = bigHall >= 0 && loaded && kept && replaced;
    printf ("fx_presets: %d found, load %s, lock %s, unlocked %s  %s\n", (int) fxm.getPresets().size(),
            loaded ? "ok" : "FEHLER", kept ? "ok" : "FEHLER", replaced ? "ok" : "FEHLER", ok ? "OK" : "FAIL");
    return ok ? 0 : 1;
}

// Measures the loudness (RMS in dBFS at master -3 dB) of every factory preset and writes it as JSON.
static int measureLoudness (const juce::File& outFile)
{
    const double sr = 48000.0;
    const int block = 512;
    auto* obj = new juce::DynamicObject();
    const auto numPresets = (int) FjordProcessor().presetManager.getPresets().size();

    for (int i = 0; i < numPresets; ++i)
    {
        // A fresh processor per preset: free-running LFOs and effect tails must not depend on the preset before
        FjordProcessor proc;
        proc.setPlayConfigDetails (0, 2, sr, block);
        const auto preset = proc.presetManager.getPresets()[(size_t) i];
        proc.presetManager.load (i);
        set (proc.apvts, "master_gain", -3.0f);
        set (proc.apvts, "preset_level", 0.0f);   // measured without the trim the generator adds
        proc.prepareToPlay (sr, block);   // after loading: level starts correctly right away

        std::vector<int> notes { 48, 55, 60, 64 };
        if (preset.category == "Bass") notes = { 36 };
        if (preset.category == "Lead") notes = { 60 };
        if (preset.category == "Drums") notes = { 36 };

        juce::AudioBuffer<float> buf (2, block);
        double sum = 0.0, peak = 0.0;
        long count = 0;
        // RMS over the first 2 s with held notes; the peak over 4 s (notes released after 2.5 s, like the
        // render test), so slow attacks and reverb build-up are part of the headroom estimate.
        const int rmsLength = (int) (sr * 2.0), total = (int) (sr * 4.0), release = (int) (sr * 2.5);
        for (int pos = 0; pos < total; pos += block)
        {
            juce::MidiBuffer midi;
            if (pos == 0)
                for (int n : notes) midi.addEvent (juce::MidiMessage::noteOn (1, n, (juce::uint8) 100), 0);
            if (pos <= release && pos + block > release)
                for (int n : notes) midi.addEvent (juce::MidiMessage::noteOff (1, n), release - pos);
            proc.processBlock (buf, midi);
            for (int ch = 0; ch < 2; ++ch)
                for (int k = 0; k < block; ++k)
                {
                    const double v = buf.getSample (ch, k);
                    if (pos + k < rmsLength) { sum += v * v; ++count; }
                    peak = std::max (peak, std::abs (v));
                }
        }

        const double db = 10.0 * std::log10 (std::max (1.0e-12, sum / (double) count));
        const double peakDb = 20.0 * std::log10 (std::max (1.0e-9, peak));
        juce::Array<juce::var> entry { db, peakDb };
        obj->setProperty (preset.name, entry);
        printf ("%-24s rms %6.1f dB  peak %6.1f dB\n", preset.name.toRawUTF8(), db, peakDb);
    }

    outFile.replaceWithText (juce::JSON::toString (juce::var (obj)));
    return 0;
}

// Renders a single note of a preset and writes it as WAV (for checking/analysis).
static int renderSingle (const juce::String& presetName, int note, const juce::File& outFile)
{
    FjordProcessor proc;
    const double sr = 48000.0;
    const int block = 512;
    proc.setPlayConfigDetails (0, 2, sr, block);
    const auto& list = proc.presetManager.getPresets();
    int index = -1;
    for (int i = 0; i < (int) list.size(); ++i)
        if (list[(size_t) i].name == presetName) index = i;
    if (index < 0) { printf ("Preset not found\n"); return 1; }
    proc.presetManager.load (index);
    proc.prepareToPlay (sr, block);

    const int total = (int) (sr * 2.0);
    juce::AudioBuffer<float> out (2, total), buf (2, block);
    for (int pos = 0; pos < total; pos += block)
    {
        juce::MidiBuffer midi;
        if (pos == 0) midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 100), 0);
        if (pos == block * 80) midi.addEvent (juce::MidiMessage::noteOff (1, note), 0);
        proc.processBlock (buf, midi);
        for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, pos, buf, ch, 0, std::min (block, total - pos));
    }
    juce::WavAudioFormat wav;
    outFile.deleteFile();
    if (auto stream = outFile.createOutputStream())
        if (auto writer = std::unique_ptr<juce::AudioFormatWriter> (wav.createWriterFor (stream.get(), sr, 2, 24, {}, 0)))
        {
            stream.release();
            writer->writeFromAudioSampleBuffer (out, 0, total);
        }
    return 0;
}

// Plays a short score with a preset, with a running host transport (arp, gate and LFOs are in sync).
// Score file: first line "bpm <tempo>", then one note per line: "<start beat> <length in beats> <midi note> <velocity 1-127>".
// Used by tools/demos/make_demos.py for the audio examples in the README.
namespace
{
    struct DemoPlayHead : juce::AudioPlayHead
    {
        double bpm = 120.0, ppq = 0.0;
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo info;
            info.setBpm (bpm);
            info.setPpqPosition (ppq);
            info.setIsPlaying (true);
            info.setTimeSignature (TimeSignature { 4, 4 });
            return info;
        }
    };
}

static int renderDemo (const juce::String& presetName, const juce::File& scoreFile, const juce::File& outFile)
{
    struct Note { double start, length; int note, velocity; };
    std::vector<Note> notes;
    double bpm = 110.0, endBeat = 0.0;
    for (auto line : juce::StringArray::fromLines (scoreFile.loadFileAsString()))
    {
        line = line.upToFirstOccurrenceOf ("#", false, false).trim();
        if (line.isEmpty()) continue;
        auto tok = juce::StringArray::fromTokens (line, " \t", "");
        tok.removeEmptyStrings();
        if (tok[0] == "bpm") { bpm = tok[1].getDoubleValue(); continue; }
        if (tok.size() < 4) continue;
        notes.push_back ({ tok[0].getDoubleValue(), tok[1].getDoubleValue(), tok[2].getIntValue(), tok[3].getIntValue() });
        endBeat = std::max (endBeat, notes.back().start + notes.back().length);
    }
    if (notes.empty()) { printf ("Empty score\n"); return 1; }

    FjordProcessor proc;
    const double sr = 48000.0;
    const int block = 256;
    proc.setPlayConfigDetails (0, 2, sr, block);
    const auto& list = proc.presetManager.getPresets();
    int index = -1;
    for (int i = 0; i < (int) list.size(); ++i)
        if (list[(size_t) i].name == presetName && list[(size_t) i].isFactory) index = i;
    if (index < 0) { printf ("Preset not found: %s\n", presetName.toRawUTF8()); return 1; }
    proc.presetManager.load (index);
    proc.prepareToPlay (sr, block);
    DemoPlayHead head;
    head.bpm = bpm;
    proc.setPlayHead (&head);

    const double samplesPerBeat = sr * 60.0 / bpm;
    const int total = (int) (endBeat * samplesPerBeat + sr * 3.5);   // + tail
    juce::AudioBuffer<float> out (2, total), buf (2, block);
    for (int pos = 0; pos < total; pos += block)
    {
        const int n = std::min (block, total - pos);
        buf.setSize (2, n, false, false, true);
        juce::MidiBuffer midi;
        for (const auto& nt : notes)
        {
            const int on = (int) (nt.start * samplesPerBeat), off = (int) ((nt.start + nt.length) * samplesPerBeat);
            if (on >= pos && on < pos + n)   midi.addEvent (juce::MidiMessage::noteOn (1, nt.note, (juce::uint8) nt.velocity), on - pos);
            if (off >= pos && off < pos + n) midi.addEvent (juce::MidiMessage::noteOff (1, nt.note), off - pos);
        }
        head.ppq = pos / samplesPerBeat;
        proc.processBlock (buf, midi);
        for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, pos, buf, ch, 0, n);
    }
    proc.setPlayHead (nullptr);

    // Short fade at the end of the tail
    const int fade = (int) (sr * 0.8);
    for (int ch = 0; ch < 2; ++ch)
        out.applyGainRamp (ch, total - fade, fade, 1.0f, 0.0f);

    juce::WavAudioFormat wav;
    outFile.deleteFile();
    if (auto stream = outFile.createOutputStream())
        if (auto writer = std::unique_ptr<juce::AudioFormatWriter> (wav.createWriterFor (stream.get(), sr, 2, 24, {}, 0)))
        {
            stream.release();
            writer->writeFromAudioSampleBuffer (out, 0, total);
        }
    printf ("%s -> %s (%.1f s, peak %.2f)\n", presetName.toRawUTF8(), outFile.getFileName().toRawUTF8(), total / sr,
            out.getMagnitude (0, total));
    return 0;
}

// One UI screenshot of a factory preset: page = play|synth|osc|mod|fx|comp|arp|rhythm|browser|about, theme = light|dark.
// The notes are held on the on-screen keyboard so the keys and the oscilloscope show something.
// Used by tools/screenshots/shoot.mjs.
static int takeScreenshot (const juce::String& pageName, const juce::String& presetName, const juce::String& theme,
                           const juce::File& outFile, int fxIndex, const juce::Array<int>& notes)
{
    static const juce::StringArray pages { "play", "synth", "osc", "mod", "fx", "comp", "arp", "rhythm", "browser", "about" };
    const int pageIndex = pages.indexOf (pageName);
    if (pageIndex < 0) { printf ("Unknown page: %s\n", pageName.toRawUTF8()); return 1; }

    FjordProcessor proc;
    const auto& list = proc.presetManager.getPresets();
    int index = -1;
    for (int i = 0; i < (int) list.size(); ++i)
        if (list[(size_t) i].name == presetName) index = i;
    if (index < 0) { printf ("Preset not found: %s\n", presetName.toRawUTF8()); return 1; }
    proc.presetManager.load (index);

    proc.prepareToPlay (48000.0, 512);
    for (int n : notes)
        proc.keyboardState.noteOn (1, n, 0.8f);
    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    for (int k = 0; k < 60; ++k) { buffer.clear(); proc.processBlock (buffer, midi); midi.clear(); }

    fjord::ui::Theme::get().setDark (theme == "dark");
    std::unique_ptr<juce::AudioProcessorEditor> editorWindow (proc.createEditor());
    editorWindow->setSize (FjordView::baseWidth, FjordView::baseHeight);
    auto* view = dynamic_cast<FjordView*> (editorWindow->getChildComponent (0));
    if (view == nullptr) return 1;

    if (pageIndex == 8)
    {
        view->selectPage (1);
        view->showBrowser (true);
    }
    else if (pageIndex == 9)
    {
        // Only for checking the layout – it shows this machine's data, so it is not a README image
        proc.setRateAndBufferSizeDetails (48000.0, 512);
        view->selectPage (0);
        view->showAbout();
    }
    else
    {
        view->selectPage (pageIndex);
    }
    if (pageIndex == 4 && fxIndex >= 0)
        view->getFxPage().selectEffect (fxIndex);
    if (pageIndex == 0)
    {
        // Keep the audio running while the visual "plays" a little, so it shows the sound
        for (int k = 0; k < 20; ++k)
        {
            buffer.clear();
            proc.processBlock (buffer, midi);
            view->getPlayPage().prepareSnapshot();
        }
    }
    if (pageIndex == 5)
    {
        // Comp page: a few seconds of re-struck chords, so the history and meters show the compressor working
        for (int k = 0; k < 180; ++k)
        {
            if (k % 24 == 0)
                for (int n : notes) { proc.keyboardState.noteOff (1, n, 0.0f); proc.keyboardState.noteOn (1, n, 0.9f); }
            for (int b = 0; b < 3; ++b) { buffer.clear(); proc.processBlock (buffer, midi); }
            view->getCompPage().prepareSnapshotFrame();
        }
    }

    // A few frames of the level meter in the header
    for (int k = 0; k < 8; ++k)
    {
        buffer.clear();
        proc.processBlock (buffer, midi);
        view->tickLevelMeter();
    }

    const auto image = view->createComponentSnapshot (view->getLocalBounds(), true, 2.0f);
    outFile.getParentDirectory().createDirectory();
    outFile.deleteFile();
    if (auto stream = outFile.createOutputStream())
        juce::PNGImageFormat().writeImageToStream (image, *stream);
    editorWindow.reset();
    fjord::ui::Theme::get().setDark (false);
    return outFile.existsAsFile() ? 0 : 1;
}

// Locks: locked parts survive a preset change, unlocked parts are replaced.
static int lockTests()
{
    FjordProcessor proc;
    auto value = [&] (const char* id) { auto* p = proc.apvts.getParameter (id); return p->convertFrom0to1 (p->getValue()); };
    auto setPlain = [&] (const char* id, float v) { auto* p = proc.apvts.getParameter (id); p->setValueNotifyingHost (p->convertTo0to1 (v)); };

    setPlain ("arp_on", 1.0f); setPlain ("arp_mode", 8.0f); setPlain ("arp_s3_pitch", 7.0f);
    setPlain ("pump_on", 1.0f); setPlain ("pump_depth", 0.123f);
    fjord::locks::setLocked (proc.apvts.state, 1, true);    // arp
    proc.presetManager.load (3);
    const bool arpKept = value ("arp_on") > 0.5f && juce::roundToInt (value ("arp_mode")) == 8 && juce::roundToInt (value ("arp_s3_pitch")) == 7;
    const bool pumpReplaced = std::abs (value ("pump_depth") - 0.123f) > 0.001f;
    fjord::locks::setLocked (proc.apvts.state, 1, false);
    proc.presetManager.load (3);
    const bool arpReplaced = juce::roundToInt (value ("arp_s3_pitch")) == 0;

    // Oscillator 2 and the delay alone locked: they stay, oscillator 1 and the reverb change
    setPlain ("osc2_on", 1.0f); setPlain ("osc2_semi", 5.0f); setPlain ("osc1_semi", -7.0f);
    setPlain ("fx_delay_on", 1.0f); setPlain ("fx_delay_feedback", 0.777f); setPlain ("fx_reverb_mix", 0.777f);
    fjord::locks::setLocked (proc.apvts.state, fjord::locks::oscGroup (1), true);
    setPlain ("osc7_on", 1.0f); setPlain ("osc7_semi", 3.0f);
    fjord::locks::setLocked (proc.apvts.state, fjord::locks::oscGroup (6), true);
    fjord::locks::setLocked (proc.apvts.state, fjord::locks::effectGroup ((int) fjord::fx::FxType::Delay), true);
    proc.presetManager.load (5);
    const bool partsKept = juce::roundToInt (value ("osc2_semi")) == 5 && juce::roundToInt (value ("osc7_semi")) == 3 && std::abs (value ("fx_delay_feedback") - 0.777f) < 0.001f;
    const bool restReplaced = juce::roundToInt (value ("osc1_semi")) != -7 && std::abs (value ("fx_reverb_mix") - 0.777f) > 0.001f;
    fjord::locks::unlockAll (proc.apvts.state);

    const bool ok = arpKept && pumpReplaced && arpReplaced && partsKept && restReplaced;
    printf ("locks: arp kept %s, pump replaced %s, unlocked %s, osc/single fx kept %s, rest replaced %s  %s\n", arpKept ? "ok" : "FAIL",
            pumpReplaced ? "ok" : "FAIL", arpReplaced ? "ok" : "FAIL", partsKept ? "ok" : "FAIL", restReplaced ? "ok" : "FAIL",
            ok ? "OK" : "FAIL");
    return ok ? 0 : 1;
}

// Effect module presets: at least 20 per effect, every one loads and switches only its effect on
static int modulePresetTests()
{
    FjordProcessor proc;
    int fails = 0, total = 0;
    for (int t = 0; t < fjord::fx::kNumFx; ++t)
    {
        const auto& list = fjord::FxModulePresets::get().getPresets (t);
        int factory = 0;
        for (const auto& p : list) factory += p.file == juce::File() ? 1 : 0;
        total += factory;
        if (factory < 20) { printf ("module presets %s: only %d  FAIL\n", fjord::fx::fxIds()[t].toRawUTF8(), factory); ++fails; }
        for (const auto& p : list)
        {
            fjord::FxModulePresets::apply (proc.apvts, t, p);
            for (const auto& [id, v] : p.values)
            {
                auto* param = proc.apvts.getParameter ("fx_" + fjord::fx::fxIds()[t] + "_" + id);
                if (param == nullptr || std::abs (param->convertFrom0to1 (param->getValue()) - v) > 0.01f * std::max (1.0f, std::abs (v)))
                {
                    printf ("module preset %s / %s: %s not applied  FAIL\n", fjord::fx::fxIds()[t].toRawUTF8(), p.name.toRawUTF8(), id.toRawUTF8());
                    ++fails;
                }
            }
        }
    }
    printf ("module_presets: %d factory presets  %s\n", total, fails == 0 ? "OK" : "FAIL");
    return fails == 0 ? 0 : 1;
}

// Hosts may send bigger blocks than announced in prepareToPlay (and odd sizes): must stay clean.
static int oversizedBlockTest()
{
    FjordProcessor proc;
    const double sr = 48000.0;
    proc.setPlayConfigDetails (0, 2, sr, 256);
    proc.prepareToPlay (sr, 256);
    set (proc.apvts, "fx_drive_on", 1); set (proc.apvts, "arp_on", 1); set (proc.apvts, "fx_reverb_on", 1);
    float peak = 0.0f;
    bool finite = true;
    for (int k = 0; k < 40; ++k)
    {
        const int n = k % 3 == 0 ? 4096 : (k % 3 == 1 ? 1000 : 37);
        juce::AudioBuffer<float> buf (2, n);
        juce::MidiBuffer midi;
        if (k == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 3000);
        proc.processBlock (buf, midi);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < n; ++i) { finite &= std::isfinite (buf.getSample (ch, i)); peak = std::max (peak, std::abs (buf.getSample (ch, i))); }
    }
    const bool ok = finite && peak > 0.01f && peak < 1.0f;
    printf ("oversized_blocks peak %.3f  %s\n", peak, ok ? "OK" : "FAIL");
    return ok ? 0 : 1;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    fjord::ui::Theme::get().setPersistent (false);   // do not touch the user settings
    if (argc > 2 && juce::String (argv[1]) == "--params")
    {
        FjordProcessor proc;
        juce::StringArray ids;
        for (auto* p : proc.getParameters())
            if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (p))
                ids.add (r->getParameterID());
        juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]).replaceWithText (ids.joinIntoString ("\n"));
        return 0;
    }
    if (argc > 2 && juce::String (argv[1]) == "--param-info")
    {
        // id <TAB> kind <TAB> min <TAB> max <TAB> default – used by tools/preset-gen/jitter.py
        FjordProcessor proc;
        juce::StringArray lines;
        for (auto* p : proc.getParameters())
            if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (p))
            {
                const char* kind = dynamic_cast<juce::AudioParameterFloat*> (r)  ? "float"
                                 : dynamic_cast<juce::AudioParameterInt*> (r)    ? "int"
                                 : dynamic_cast<juce::AudioParameterBool*> (r)   ? "bool"
                                 : dynamic_cast<juce::AudioParameterChoice*> (r) ? "choice" : "other";
                const auto range = r->getNormalisableRange();
                lines.add (r->getParameterID() + "\t" + kind + "\t" + juce::String (range.start, 6) + "\t"
                           + juce::String (range.end, 6) + "\t" + juce::String (r->convertFrom0to1 (r->getDefaultValue()), 6));
            }
        juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]).replaceWithText (lines.joinIntoString ("\n") + "\n");
        return 0;
    }
    if (argc > 4 && juce::String (argv[1]) == "--single")
        return renderSingle (argv[2], juce::String (argv[3]).getIntValue(),
                             juce::File::getCurrentWorkingDirectory().getChildFile (argv[4]));
    if (argc > 5 && juce::String (argv[1]) == "--screenshot")
    {
        // --screenshot <page> <preset> <light|dark> <out.png> [fx index] [notes, e.g. 48,55,60]
        juce::Array<int> notes { 48, 55, 60, 64 };
        if (argc > 7)
        {
            notes.clear();
            for (auto& n : juce::StringArray::fromTokens (argv[7], ",", ""))
                if (n.getIntValue() > 0)   // "0" = no notes
                    notes.add (n.getIntValue());
        }
        return takeScreenshot (argv[2], argv[3], argv[4], juce::File::getCurrentWorkingDirectory().getChildFile (argv[5]),
                               argc > 6 ? juce::String (argv[6]).getIntValue() : -1, notes);
    }
    if (argc > 4 && juce::String (argv[1]) == "--demo")
        return renderDemo (argv[2], juce::File::getCurrentWorkingDirectory().getChildFile (argv[3]),
                           juce::File::getCurrentWorkingDirectory().getChildFile (argv[4]));
    if (argc > 2 && juce::String (argv[1]) == "--loudness")
        return measureLoudness (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]));

    const auto outDir = juce::File (argc > 1 ? argv[1] : "build/renders");
    if (argc > 3 && juce::String (argv[2]) == "--sr")
        testSampleRate = juce::String (argv[3]).getDoubleValue();
    outDir.createDirectory();

    using S = juce::AudioProcessorValueTreeState;
    std::vector<Case> cases {
        { "init_chord", [] (S&) {}, { 48, 55, 60, 64 } },
        { "supersaw_pad", [] (S& s) {
              set (s, "osc1_unison", 7); set (s, "osc1_detune", 0.45f);
              set (s, "osc2_on", 1); set (s, "osc2_unison", 7); set (s, "osc2_octave", 1); set (s, "osc2_level", 0.5f);
              set (s, "filter_cutoff", 2500); set (s, "amp_attack", 0.6f); set (s, "amp_release", 1.5f);
          }, { 48, 55, 60, 64, 67, 72 } },
        { "ladder_bass", [] (S& s) {
              set (s, "voice_mode", 1); set (s, "osc1_wave", 3); set (s, "sub_level", 0.7f);
              set (s, "filter_cutoff", 180); set (s, "filter_reso", 0.55f); set (s, "filter_env", 0.45f);
              set (s, "fenv_decay", 0.25f); set (s, "fenv_sustain", 0.0f); set (s, "filter_drive", 0.5f);
          }, { 36 } },
        { "self_osc", [] (S& s) {
              set (s, "filter_reso", 1.0f); set (s, "filter_cutoff", 800); set (s, "filter_drive", 1.0f);
              set (s, "noise_level", 1.0f);
          }, { 60 } },
        { "svf_band_noise", [] (S& s) {
              set (s, "filter_type", 3); set (s, "osc1_on", 0); set (s, "noise_level", 1.0f); set (s, "filter_reso", 0.8f);
          }, { 60, 67 } },
        { "wt_vowels_lfo", [] (S& s) {
              set (s, "osc1_wave", 4); set (s, "osc1_table", 8); set (s, "osc1_unison", 5);
              set (s, "lfo1_rate", 0.7f); set (s, "mod1_src", 1); set (s, "mod1_dst", 8); set (s, "mod1_amt", 0.5f);
              set (s, "osc1_wtpos", 0.5f);
          }, { 48, 55, 60 } },
        { "wt_all_tables", [] (S& s) {
              set (s, "osc1_wave", 4); set (s, "osc1_table", 13); set (s, "osc1_wtpos", 0.6f);
              set (s, "osc2_on", 1); set (s, "osc2_wave", 4); set (s, "osc2_table", 23); set (s, "osc2_wtpos", 1.0f);
              set (s, "osc3_on", 1); set (s, "osc3_wave", 4); set (s, "osc3_table", 19); set (s, "osc3_wtpos", 0.3f);
              set (s, "filter_cutoff", 12000);
          }, { 36, 60, 96 } },
        { "comb_filter", [] (S& s) {
              set (s, "filter_type", 6); set (s, "filter_reso", 0.9f); set (s, "filter_cutoff", 220); set (s, "filter_keytrack", 1.0f);
              set (s, "osc1_on", 0); set (s, "noise_level", 0.8f);
          }, { 48, 55 } },
        { "formant_filter2_parallel", [] (S& s) {
              set (s, "filter_type", 7); set (s, "filter_reso", 0.6f); set (s, "filter2_on", 1); set (s, "filter_routing", 1);
              set (s, "lfo2_shape", 6); set (s, "lfo2_rate", 1.5f); set (s, "mod2_src", 2); set (s, "mod2_dst", 18); set (s, "mod2_amt", 0.4f);
          }, { 48, 52, 55 } },
        { "analog_vibrato_macro", [] (S& s) {
              set (s, "voice_analog", 1.0f); set (s, "osc2_on", 1); set (s, "macro1", 1.0f);
              set (s, "lfo1_rate", 5.5f); set (s, "mod1_src", 1); set (s, "mod1_dst", 1); set (s, "mod1_amt", 0.02f); set (s, "mod1_via", 13);
              set (s, "mod2_src", 3); set (s, "mod2_dst", 24); set (s, "mod2_amt", -0.5f); set (s, "lfo3_shape", 4); set (s, "lfo3_rate", 4.0f);
          }, { 57, 64 } },
        { "fx_drive_tape", [] (S& s) { set (s, "fx_drive_on", 1); set (s, "fx_drive_amount", 0.8f); }, { 48, 55 } },
        { "fx_drive_fold", [] (S& s) { set (s, "fx_drive_on", 1); set (s, "fx_drive_type", 2); set (s, "fx_drive_amount", 1.0f); }, { 36 } },
        { "fx_crush", [] (S& s) { set (s, "fx_crush_on", 1); set (s, "fx_crush_bits", 4.0f); set (s, "fx_crush_rate", 12.0f); }, { 60 } },
        { "fx_eq", [] (S& s) {
              set (s, "fx_eq_on", 1); set (s, "fx_eq_lowcut", 200.0f); set (s, "fx_eq_low", 12.0f);
              set (s, "fx_eq_mid", -12.0f); set (s, "fx_eq_high", 10.0f); set (s, "fx_eq_highcut", 6000.0f);
          }, { 48, 60 } },
        { "fx_eq_parametric", [] (S& s) {
              set (s, "fx_eq_on", 1); set (s, "fx_eq_lowcut", 300.0f); set (s, "fx_eq_lcslope", 1);
              set (s, "fx_eq_midfreq", 900.0f); set (s, "fx_eq_mid", 18.0f); set (s, "fx_eq_midq", 12.0f);
              set (s, "fx_eq_mid2freq", 4000.0f); set (s, "fx_eq_mid2", -18.0f); set (s, "fx_eq_mid2q", 0.2f);
              set (s, "fx_eq_highcut", 5000.0f); set (s, "fx_eq_hcslope", 1); set (s, "osc1_unison", 5);
          }, { 48, 60 } },
        { "fx_comp", [] (S& s) { set (s, "fx_comp_on", 1); set (s, "fx_comp_thresh", -30.0f); set (s, "fx_comp_ratio", 10.0f); set (s, "fx_comp_makeup", 6.0f); }, { 48, 55, 60 } },
        { "fx_chorus", [] (S& s) { set (s, "fx_chorus_on", 1); set (s, "fx_chorus_depth", 1.0f); set (s, "fx_chorus_mix", 0.8f); }, { 48, 55, 60 } },
        { "fx_phaser", [] (S& s) { set (s, "fx_phaser_on", 1); set (s, "fx_phaser_feedback", 1.0f); set (s, "fx_phaser_rate", 2.0f); }, { 48, 60 } },
        { "fx_flanger", [] (S& s) { set (s, "fx_flanger_on", 1); set (s, "fx_flanger_feedback", 1.0f); set (s, "fx_flanger_mix", 1.0f); }, { 48, 60 } },
        { "fx_delay_pingpong", [] (S& s) {
              set (s, "fx_delay_on", 1); set (s, "fx_delay_feedback", 0.9f); set (s, "fx_delay_mix", 0.5f);
              set (s, "amp_decay", 0.2f); set (s, "amp_sustain", 0.0f);
          }, { 60 } },
        { "fx_reverb_huge", [] (S& s) {
              set (s, "fx_reverb_on", 1); set (s, "fx_reverb_size", 1.0f); set (s, "fx_reverb_decay", 20.0f); set (s, "fx_reverb_mix", 1.0f);
              set (s, "amp_decay", 0.2f); set (s, "amp_sustain", 0.0f);
          }, { 48, 55, 60, 64 } },
        { "fx_all_chain", [] (S& s) {
              for (auto* id : { "drive", "crush", "eq", "comp", "chorus", "phaser", "flanger", "delay", "reverb", "width" })
                  set (s, juce::String ("fx_") + id + "_on", 1);
              set (s, "fx_width_amount", 2.0f); set (s, "mod1_src", 13); set (s, "mod1_dst", 37); set (s, "mod1_amt", 0.5f);
              set (s, "osc1_unison", 8);
          }, { 48, 55, 60, 64, 67 } },
        { "rhythm_arp_updown", [] (S& s) {
              set (s, "arp_on", 1); set (s, "arp_mode", 2); set (s, "arp_oct", 2); set (s, "arp_rate", 13);
              set (s, "osc1_retrig", 1); set (s, "amp_decay", 0.2f); set (s, "amp_sustain", 0.3f);
          }, { 48, 52, 55 } },
        { "rhythm_arp_pattern", [] (S& s) {
              set (s, "arp_on", 1); set (s, "arp_mode", 7); set (s, "arp_oct", 2); set (s, "arp_repeat", 2);
              set (s, "arp_chance", 0.7f); set (s, "arp_steps", 5); set (s, "arp_retrig", 1); set (s, "arp_swing", 0.4f);
              set (s, "voice_mode", 2); set (s, "voice_glide", 0.05f);
              for (int i = 0; i < 16; ++i)
              {
                  set (s, "arp_s" + juce::String (i + 1) + "_vel", i % 3 == 2 ? 0.0f : 0.5f + 0.1f * (float) (i % 5));
                  set (s, "arp_s" + juce::String (i + 1) + "_pitch", (float) ((i * 5) % 25 - 12));
                  set (s, "arp_s" + juce::String (i + 1) + "_tie", i % 4 == 1 ? 1.0f : 0.0f);
              }
          }, { 48, 52, 55, 59 } },
        { "rhythm_arp_modes", [] (S& s) {
              set (s, "arp_on", 1); set (s, "arp_mode", 10); set (s, "arp_rate", 16); set (s, "amp_decay", 0.1f); set (s, "amp_sustain", 0.0f);
          }, { 48, 52, 55, 60, 64 } },
        { "rhythm_gate_trance", [] (S& s) {
              set (s, "gate_on", 1); set (s, "gate_length", 1.0f); set (s, "gate_smooth", 1.0f);
              for (int i = 0; i < 32; ++i) set (s, "gate_s" + juce::String (i + 1), i % 2 == 0 ? 1.0f : 0.0f);
              set (s, "osc1_unison", 5);
          }, { 48, 55, 60 } },
        { "rhythm_stutter", [] (S& s) {
              set (s, "stutter_on", 1); set (s, "stutter_chance", 1.0f); set (s, "stutter_interval", 1); set (s, "stutter_pitch", 12.0f);
              set (s, "stutter_decay", 0.1f); set (s, "arp_on", 1);
          }, { 48, 55, 60 } },
        { "rhythm_pump", [] (S& s) {
              set (s, "pump_on", 1); set (s, "pump_depth", 0.9f); set (s, "fx_reverb_on", 1);
          }, { 48, 55, 60 } },
        { "full_poly_load", [] (S& s) {
              for (int i = 1; i <= 4; ++i)
              {
                  set (s, "osc" + juce::String (i) + "_on", 1);
                  set (s, "osc" + juce::String (i) + "_unison", 8);
              }
          }, { 36, 40, 43, 48, 52, 55, 60, 64, 67, 72, 76, 79, 84, 88, 91, 96 }, true },
        { "eight_osc_load", [] (S& s) {
              for (int i = 1; i <= 8; ++i)
              {
                  set (s, "osc" + juce::String (i) + "_on", 1);
                  set (s, "osc" + juce::String (i) + "_unison", 4);
                  set (s, "osc" + juce::String (i) + "_level", 0.4f);
                  set (s, "osc" + juce::String (i) + "_wave", i % 2 == 0 ? 4.0f : 2.0f);
                  set (s, "osc" + juce::String (i) + "_fine", (float) (i * 5 - 20));
              }
              set (s, "mod1_src", 1); set (s, "mod1_dst", (float) (int) fjord::dsp::ModDest::Osc8WtPos); set (s, "mod1_amt", 0.5f);
          }, { 36, 40, 43, 48, 52, 55, 60, 64, 67, 72, 76, 79, 84, 88, 91, 96 }, true },
        { "osc8_alone", [] (S& s) {
              set (s, "osc1_on", 0); set (s, "osc8_on", 1); set (s, "osc8_wave", 3); set (s, "osc8_octave", -1);
              set (s, "mod1_src", 1); set (s, "mod1_dst", (float) (int) fjord::dsp::ModDest::Osc8Pitch); set (s, "mod1_amt", 0.1f);
          }, { 48, 55, 60 } },
    };

    // UI snapshots in both themes
    {
        FjordProcessor proc;
        proc.presetManager.load (1);
        set (proc.apvts, "osc2_wave", 4); set (proc.apvts, "osc2_table", 8); set (proc.apvts, "osc2_wtpos", 0.4f);
        set (proc.apvts, "mod1_src", 1); set (proc.apvts, "mod1_dst", 18); set (proc.apvts, "mod1_amt", 0.25f);
        set (proc.apvts, "mod2_src", 13); set (proc.apvts, "mod2_dst", 9); set (proc.apvts, "mod2_amt", -0.6f); set (proc.apvts, "mod2_via", 9);
        set (proc.apvts, "lfo2_shape", 5); set (proc.apvts, "lfo3_sync", 1);
        set (proc.apvts, "fx_eq_on", 1); set (proc.apvts, "fx_eq_lowcut", 80.0f); set (proc.apvts, "fx_eq_mid", -6.0f);
        set (proc.apvts, "fx_eq_high", 4.0f); set (proc.apvts, "fx_eq_highcut", 12000.0f); set (proc.apvts, "fx_reverb_on", 1);
        set (proc.apvts, "fx_eq_mid2", 5.0f); set (proc.apvts, "fx_eq_mid2freq", 3500.0f); set (proc.apvts, "fx_eq_mid2q", 2.5f);
        set (proc.apvts, "fx_delay_on", 1);
        set (proc.apvts, "gate_on", 1); set (proc.apvts, "arp_on", 1); set (proc.apvts, "pump_on", 1);
        set (proc.apvts, "stutter_on", 1); set (proc.apvts, "stutter_start", 2); set (proc.apvts, "stutter_pitch", 7.0f);
        set (proc.apvts, "stutter_decay", 0.12f); set (proc.apvts, "stutter_gate", 0.7f); set (proc.apvts, "stutter_slice", 2);
        proc.apvts.state.setProperty ("macroNames", "Tone|Motion|Width|Attack|Release|Vibrato|Drive|Detune", nullptr);
        {
            // Some signal for the oscilloscope
            proc.prepareToPlay (48000.0, 512);
            juce::AudioBuffer<float> b (2, 512);
            juce::MidiBuffer m;
            m.addEvent (juce::MidiMessage::noteOn (1, 48, (juce::uint8) 100), 0);
            for (int k = 0; k < 40; ++k) { proc.processBlock (b, m); m.clear(); }
        }
        for (bool dark : { false, true })
        {
            fjord::ui::Theme::get().setDark (dark);
            std::unique_ptr<juce::AudioProcessorEditor> editorWindow (proc.createEditor());
            editorWindow->setSize (FjordView::baseWidth, FjordView::baseHeight);
            juce::Component* editor = editorWindow->getChildComponent (0);   // FjordView
            const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);
            auto snap = [&] (const juce::String& name)
            {
                const auto img = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);
                auto file = outDir.getChildFile (name + (dark ? "_dark.png" : "_light.png"));
                file.deleteFile();
                if (auto stream = file.createOutputStream())
                    juce::PNGImageFormat().writeImageToStream (img, *stream);
            };
            juce::ignoreUnused (image);
            snap ("ui");

            auto showOnly = [&] (juce::Component* target)
            {
                for (auto* child : editor->getChildren())
                    if (dynamic_cast<fjord::ui::SynthPage*> (child) || dynamic_cast<fjord::ui::ModPage*> (child)
                        || dynamic_cast<fjord::ui::PlayPage*> (child) || dynamic_cast<fjord::ui::FxPage*> (child)
                        || dynamic_cast<fjord::ui::RhythmPage*> (child) || dynamic_cast<fjord::ui::ArpPage*> (child))
                        child->setVisible (child == target);
            };
            for (auto* child : editor->getChildren())
            {
                if (dynamic_cast<fjord::ui::ModPage*> (child))  { showOnly (child); snap ("ui_mod"); }
                if (dynamic_cast<fjord::ui::PlayPage*> (child)) { showOnly (child); snap ("ui_play"); }
                if (dynamic_cast<fjord::ui::RhythmPage*> (child)) { showOnly (child); snap ("ui_rhythm"); }
                if (dynamic_cast<fjord::ui::ArpPage*> (child)) { showOnly (child); snap ("ui_arp"); }
                if (auto* fxp = dynamic_cast<fjord::ui::FxPage*> (child))
                {
                    showOnly (child);
                    snap ("ui_fx_reverb");
                    fxp->selectEffect (2);
                    snap ("ui_fx_eq");
                    fxp->selectEffect (7);
                    snap ("ui_fx_delay");
                    for (auto* c : fxp->getChildren())
                        if (auto* picker = dynamic_cast<fjord::ui::FxPresetPicker*> (c))
                        {
                            picker->setVisible (true);
                            snap ("ui_fx_picker");
                            picker->setVisible (false);
                        }
                }
            }
            for (auto* child : editor->getChildren())
                if (dynamic_cast<fjord::ui::SynthPage*> (child)) showOnly (child);

            for (auto* child : editor->getChildren())
                if (auto* b = dynamic_cast<fjord::ui::PresetBrowser*> (child))
                {
                    b->open();
                    snap ("ui_browser");
                    for (auto* c2 : editor->getChildren())
                        if (auto* d = dynamic_cast<fjord::ui::SaveDialog*> (c2))
                        {
                            d->open();
                            snap ("ui_save");
                            d->close();
                        }
                }
        }
        fjord::ui::Theme::get().setDark (false);
    }

    {
        FjordProcessor proc;
        const auto& list = proc.presetManager.getPresets();
        printf ("%d factory presets found\n", (int) list.size());
        for (size_t i = 0; i < list.size(); ++i)
        {
            // Drums sound the same on every key – a chord would stack identical hits
            const std::vector<int> notes = list[i].category == "Drums" ? std::vector<int> { 36 } : std::vector<int> { 48, 55, 60, 64 };
            Case c { "preset_" + list[i].category + "_" + list[i].name.replaceCharacter (' ', '_'), [] (S&) {}, notes };
            c.presetIndex = (int) i;
            cases.push_back (c);
        }
    }

    int failures = stuckNoteTests() + arpSwitchTest() + fxPresetTests() + lockTests() + oversizedBlockTest() + modulePresetTests();
    for (auto& c : cases)
        failures += render (c, outDir) ? 0 : 1;

    printf ("%s\n", failures == 0 ? "ALL PASSED" : "FAILURES");
    return failures == 0 ? 0 : 1;
}
