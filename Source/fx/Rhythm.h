#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>
#include "../dsp/DspCommon.h"
#include "../dsp/Modulation.h"

namespace fjord::rhythm
{
    constexpr int kMaxSteps = 32;

    // Shared time base for a block: position in quarter notes at block start + quarters per sample
    struct Clock
    {
        double ppq = 0.0;
        double beatsPerSample = 0.0;
        bool running = false;           // false: no note active and host stopped → rhythm effects rest
        double at (int sample) const { return ppq + beatsPerSample * sample; }
    };

    // ---------------------------------------------------------------------------------------------
    // Step gate patterns (for the picker): x = 1, o = 0.6, . = 0.3, - = 0
    struct GatePattern { const char* name; const char* steps; };
    inline constexpr GatePattern gatePatterns[] = {
        { "Trance A",     "x-xx-xx-x-xx-xx-" },
        { "Trance B",     "xx-xx-x-xx-xx-x-" },
        { "Shutter",      "x-x-x-xxx-x-x-xx" },
        { "Straight 16",  "xxxxxxxxxxxxxxxx" },
        { "Offbeat",      "--x---x---x---x-" },
        { "Dotted",       "x--x--x--x--x-x-" },
        { "Gallop",       "xx-xxx-xxx-xxx-x" },
        { "Stutter",      "xxxx-x-xxxxx-x-x" },
        { "Half Time",    "xxxxxxxx--------" },
        { "Accents",      "xo.ox.o.xo.ox.o." },
        { "Breathing",    "x-o-.-o-x-o-.-o-" },
        { "Syncopated",   "x--x--x---x--x--" },
    };

    inline float patternValue (const char* steps, int i)
    {
        const int len = (int) std::strlen (steps);
        switch (steps[i % len])
        {
            case 'x': return 1.0f;
            case 'o': return 0.6f;
            case '.': return 0.3f;
            default:  return 0.0f;
        }
    }

    // ---------------------------------------------------------------------------------------------
    constexpr int kArpSteps = 16;

    // Arp modes. New modes are appended so stored indices keep their meaning.
    inline const juce::StringArray arpModeNames { "Up", "Down", "Up/Down", "Random", "Played", "Chord",
                                                  "Down/Up", "Converge", "Diverge", "Thumb", "Pinky" };

    struct ArpParams
    {
        bool on = false, latch = false, retrigger = false;
        int mode = 0;
        double stepBeats = 0.25;
        int octaves = 1, repeats = 1, steps = kArpSteps;
        float gate = 0.6f, swing = 0.0f, chance = 1.0f;
        // Step pattern: velocity (0 = rest), transposition in semitones, tie into the next step
        std::array<float, kArpSteps> velocity {};
        std::array<int, kArpSteps> pitch {};
        std::array<bool, kArpSteps> tie {};
    };

    // Arp pattern presets: velocity x = 1, o = 0.7, . = 0.4, - = rest; t = tie into the next step
    struct ArpPattern
    {
        const char* name;
        const char* velocity;
        std::array<int, kArpSteps> pitch;
        const char* ties;
        int steps;
    };

    inline constexpr ArpPattern arpPatterns[] = {
        { "Straight",      "xxxxxxxxxxxxxxxx", {},                                                  "----------------", 16 },
        { "Accents",       "xo.ox.o.xo.ox.o.", {},                                                  "----------------", 16 },
        { "Offbeat Push",  ".x.x.x.x.x.x.x.x", {},                                                  "----------------", 16 },
        { "Octave Jump",   "xoxoxoxoxoxoxoxo", { 0, 12, 0, 12, 0, 12, 0, 12, 0, 12, 0, 12, 0, 12, 0, 12 }, "----------------", 16 },
        { "Fifths",        "xoooxoooxoooxooo", { 0, 7, 12, 7, 0, 7, 12, 7, 0, 7, 12, 7, 0, 7, 12, 7 },    "----------------", 16 },
        { "Disco Octaves", "x.x.x.x.x.x.x.x.", { 0, 12, 0, 12, 0, 12, 0, 12, 0, 12, 0, 12, 0, 12, 0, 12 }, "----------------", 16 },
        { "Gallop",        "x-xx-xx-x-xx-xx-", {},                                                  "----------------", 16 },
        { "Trance Rests",  "xx-xx-x-xx-xx-x-", {},                                                  "----------------", 16 },
        { "Skippy",        "x.x-xx.x-x.xx-x.", {},                                                  "----------------", 16 },
        { "Slide Bass",    "xoxoxoxoxoxoxoxo", { 0, 0, 12, 0, 0, 0, 12, 7, 0, 0, 12, 0, 0, 5, 7, 12 },  "--t---t---t---t-", 16 },
        { "Rising Thirds", "xooxooxooxooxoox", { 0, 4, 7, 0, 4, 7, 12, 4, 7, 12, 16, 7, 12, 16, 19, 12 }, "----------------", 16 },
        { "Three Step",    "xoo",              {},                                                  "---",              3 },
        { "Five Step",     "xo.ox",            { 0, 0, 12, 0, 7 },                                  "-----",            5 },
        { "Long Notes",    "x-x-x-x-x-x-x-x-", {},                                                  "t-t-t-t-t-t-t-t-", 16 },
    };

    inline float arpVelocityValue (char c)
    {
        switch (c)
        {
            case 'x': return 1.0f;
            case 'o': return 0.7f;
            case '.': return 0.4f;
            default:  return 0.0f;
        }
    }

    // Arpeggiator: turns held notes into tempo-synced steps (sample accurate).
    class Arpeggiator
    {
    public:
        void reset()
        {
            held.clear();
            latched.clear();
            pendingOff.clear();
            sounding.clear();   // after a reset the engine is silent too
            stepIndex = 0;
            patternIndex = 0;
            lastStep = -1;
            physicalDown = 0;
            shownStep = -1;
        }

        bool hasHeldNotes() const { return ! source().empty(); }

        // Pattern step that played last (-1 = arp idle), for the step display
        int currentPatternStep() const { return shownStep; }

        // Ends everything the arp is currently playing (e.g. on All Notes Off or host time jumps).
        void flush (juce::MidiBuffer& out, int pos)
        {
            for (int n : sounding) out.addEvent (juce::MidiMessage::noteOff (1, n), pos);
            sounding.clear();
            pendingOff.clear();
        }

        // Processes MIDI in place: notes become arp notes, everything else passes through.
        void process (juce::MidiBuffer& midi, int numSamples, const Clock& clock, const ArpParams& p)
        {
            if (! p.on)
            {
                if (! sounding.empty())
                {
                    // Arp was switched off: end its notes first, then pass the incoming events – a key pressed
                    // in the same block must not be cut by these note-offs.
                    juce::MidiBuffer out;
                    for (int n : sounding) out.addEvent (juce::MidiMessage::noteOff (1, n), 0);
                    sounding.clear();
                    for (const auto meta : midi) out.addEvent (meta.getMessage(), meta.samplePosition);
                    midi.swapWith (out);
                }
                held.clear();
                latched.clear();
                lastStep = -1;
                lastStepPpq = -1.0e9;
                shownStep = -1;
                return;
            }

            juce::MidiBuffer out;
            for (const auto meta : midi)
            {
                const auto m = meta.getMessage();
                if (m.isNoteOn())
                {
                    // Retrigger: every new key starts the sequence and the pattern from the beginning
                    if (p.retrigger) { stepIndex = 0; patternIndex = 0; }
                    noteOn (m.getNoteNumber(), m.getFloatVelocity(), p.latch);
                }
                else if (m.isNoteOff()) noteOff (m.getNoteNumber(), p.latch);
                else if (m.isAllNotesOff() || m.isAllSoundOff())
                {
                    held.clear();
                    latched.clear();
                    physicalDown = 0;
                    flush (out, meta.samplePosition);
                    out.addEvent (m, meta.samplePosition);
                }
                else                    out.addEvent (m, meta.samplePosition);
            }

            // Due note-offs from earlier steps. If a note-off lies outside the plausible window
            // (host stopped, loop jump, switch between host and free clock), it is sent immediately.
            for (auto it = pendingOff.begin(); it != pendingOff.end();)
            {
                const bool implausible = it->ppq < clock.ppq - 1.0e-6 || it->ppq > clock.ppq + 2.5 * p.stepBeats + 0.01;
                const int pos = implausible ? 0 : (int) std::floor ((it->ppq - clock.ppq) / std::max (1.0e-12, clock.beatsPerSample));
                if (pos < numSamples)
                {
                    out.addEvent (juce::MidiMessage::noteOff (1, it->note), std::clamp (pos, 0, numSamples - 1));
                    std::erase (sounding, it->note);
                    it = pendingOff.erase (it);
                }
                else ++it;
            }

            if (source().empty())
            {
                lastStep = -1;
                lastStepPpq = -1.0e9;
                stepIndex = 0;
                patternIndex = 0;
                shownStep = -1;
                midi.swapWith (out);
                return;
            }

            // Find the steps in this block
            const double startPpq = clock.ppq, endPpq = clock.at (numSamples);
            if (endPpq < startPpq || startPpq < lastStepPpq - p.stepBeats - 1.0e-6)   // Loop / Sprung im Host
            {
                lastStep = -1;
                lastStepPpq = -1.0e9;
            }
            const int64_t first = (int64_t) std::floor (startPpq / p.stepBeats) - 1;
            const int64_t last = (int64_t) std::floor (endPpq / p.stepBeats);

            for (int64_t k = first; k <= last; ++k)
            {
                if (k <= lastStep) continue;
                double t = (double) k * p.stepBeats;
                if (k % 2 != 0) t += p.swing * p.stepBeats * 0.5;   // swing shifts every second step
                if (t < startPpq - 1.0e-9 || t >= endPpq) continue;

                const int pos = std::clamp ((int) ((t - startPpq) / clock.beatsPerSample), 0, numSamples - 1);
                lastStep = k;
                lastStepPpq = t;
                playStep (out, pos, t, p);
            }

            midi.swapWith (out);
        }

    private:
        struct Held { int note; float velocity; };
        struct Pending { int note; double ppq; };

        const std::vector<Held>& source() const { return latched.empty() ? held : latched; }

        void noteOn (int note, float vel, bool latch)
        {
            if (latch && physicalDown == 0)
                latched.clear();   // a new chord replaces the held one
            ++physicalDown;
            std::erase_if (held, [note] (const Held& h) { return h.note == note; });
            held.push_back ({ note, vel });
            if (latch)
            {
                std::erase_if (latched, [note] (const Held& h) { return h.note == note; });
                latched.push_back ({ note, vel });
            }
        }

        void noteOff (int note, bool latch)
        {
            physicalDown = std::max (0, physicalDown - 1);
            std::erase_if (held, [note] (const Held& h) { return h.note == note; });
            if (! latch) latched.clear();
        }

        // Index into the note sequence for the given mode (n = number of notes, i = running step)
        int sequenceIndex (int mode, int n, int i)
        {
            if (n <= 1) return 0;
            switch (mode)
            {
                case 1: return n - 1 - i % n;                                            // Down
                case 2: { const int per = 2 * n - 2, k = i % per; return k < n ? k : per - k; }          // Up/Down
                case 3: return rng.nextInt (n);                                          // Random
                case 6: { const int per = 2 * n - 2, k = i % per; return k < n ? n - 1 - k : k - n + 1; } // Down/Up
                case 7: { const int k = i % n; return k % 2 == 0 ? k / 2 : n - 1 - k / 2; }               // Converge
                case 8: { const int k = n - 1 - i % n; return k % 2 == 0 ? k / 2 : n - 1 - k / 2; }       // Diverge
                case 9:  { const int k = i % (2 * (n - 1)); return k % 2 == 0 ? 0 : 1 + k / 2; }          // Thumb
                case 10: { const int k = i % (2 * (n - 1)); return k % 2 == 0 ? k / 2 : n - 1; }          // Pinky
                default: return i % n;                                                   // Up, Played
            }
        }

        void playStep (juce::MidiBuffer& out, int pos, double ppq, const ArpParams& p)
        {
            // Pattern lane of this step
            const int steps = std::clamp (p.steps, 1, kArpSteps);
            const int lane = patternIndex % steps;
            ++patternIndex;
            shownStep = lane;
            const float stepVelocity = p.velocity[(size_t) lane];
            if (stepVelocity < 0.01f)
                return;                                   // rest: the note sequence waits
            const bool skip = p.chance < 1.0f && rng.nextFloat() >= p.chance;

            // Build the sequence: sorted (or in played order) across the octaves
            std::vector<Held> base = source();
            if (p.mode != 4)
                std::sort (base.begin(), base.end(), [] (const Held& a, const Held& b) { return a.note < b.note; });

            std::vector<Held> seq;
            for (int o = 0; o < std::max (1, p.octaves); ++o)
                for (const auto& h : base)
                    if (h.note + 12 * o < 128) seq.push_back ({ h.note + 12 * o, h.velocity });
            if (seq.empty()) return;

            std::vector<Held> notes;
            const int n = (int) seq.size();
            const int noteStep = stepIndex / std::max (1, p.repeats);
            if (p.mode == 5) notes = seq;                 // Chord
            else             notes.push_back (seq[(size_t) sequenceIndex (p.mode, n, noteStep)]);
            ++stepIndex;
            if (skip)
                return;                                   // chance: the step stays silent, the melody moves on

            const double length = p.tie[(size_t) lane] ? 1.04 + p.swing * 0.5 : std::clamp ((double) p.gate, 0.05, 1.0) * 0.98;
            for (auto h : notes)
            {
                h.note = std::clamp (h.note + p.pitch[(size_t) lane], 0, 127);
                h.velocity = std::clamp (h.velocity * stepVelocity, 0.05f, 1.0f);

                // Same note still active? End it first so it is struck again.
                if (std::find (sounding.begin(), sounding.end(), h.note) != sounding.end())
                {
                    out.addEvent (juce::MidiMessage::noteOff (1, h.note), pos);
                    std::erase_if (pendingOff, [&h] (const Pending& x) { return x.note == h.note; });
                    std::erase (sounding, h.note);
                }
                out.addEvent (juce::MidiMessage::noteOn (1, h.note, h.velocity), pos);
                sounding.push_back (h.note);
                pendingOff.push_back ({ h.note, ppq + p.stepBeats * length });
            }
        }

        std::vector<Held> held, latched;
        std::vector<Pending> pendingOff;
        std::vector<int> sounding;
        int stepIndex = 0, patternIndex = 0, physicalDown = 0, shownStep = -1;
        int64_t lastStep = -1;
        double lastStepPpq = -1.0e9;
        juce::Random rng;
    };

    // ---------------------------------------------------------------------------------------------
    struct GateParams
    {
        bool on = false;
        int steps = 16;
        double stepBeats = 0.25;
        float length = 1.0f;      // fraction of the step that stays open
        float smooth = 3.0f;      // ms
        float depth = 1.0f;
        float swing = 0.0f;
        std::array<float, kMaxSteps> values {};
    };

    // Step gate (trance/shutter gate): volume follows a step pattern.
    class StepGate
    {
    public:
        void prepare (double sr) { sampleRate = (float) sr; level = 1.0f; }
        int currentStep() const { return current; }

        void process (juce::AudioBuffer<float>& buffer, const Clock& clock, const GateParams& p)
        {
            if (! p.on) { current = -1; level = 1.0f; return; }

            const float coef = std::exp (-1.0f / (std::max (0.2f, p.smooth) * 0.001f * sampleRate));
            auto* l = buffer.getWritePointer (0);
            auto* r = buffer.getWritePointer (1);

            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                float target = 1.0f;
                if (clock.running)
                {
                    const double ppq = clock.at (i);
                    // Swing: pairs of steps, the second step starts later
                    const double pair = 2.0 * p.stepBeats;
                    const double inPair = ppq - std::floor (ppq / pair) * pair;
                    const double split = p.stepBeats * (1.0 + p.swing * 0.5);
                    const bool second = inPair >= split;
                    const double stepStart = second ? split : 0.0;
                    const double stepLen = second ? pair - split : split;
                    const double frac = (inPair - stepStart) / stepLen;

                    const int64_t pairIndex = (int64_t) std::floor (ppq / pair);
                    const int step = (int) ((pairIndex * 2 + (second ? 1 : 0)) % p.steps);
                    current = step;
                    const float v = frac < p.length ? p.values[(size_t) step] : 0.0f;
                    target = 1.0f - p.depth * (1.0f - v);
                }
                level = target + (level - target) * coef;
                l[i] *= level;
                r[i] *= level;
            }
            if (! clock.running) current = -1;
        }

    private:
        float sampleRate = 44100.0f, level = 1.0f;
        int current = -1;
    };

    // ---------------------------------------------------------------------------------------------
    // Stutter choice lists (names + length in quarter notes)
    inline const juce::StringArray stutterEveryNames { "1/4", "1/2", "1 Bar", "2 Bars" };
    inline constexpr double stutterEveryBeats[] = { 1.0, 2.0, 4.0, 8.0 };
    inline const juce::StringArray stutterSliceNames { "1/4", "1/8", "1/16", "1/32", "1/64" };
    inline constexpr double stutterSliceBeats[] = { 1.0, 0.5, 0.25, 0.125, 0.0625 };
    inline const juce::StringArray stutterStartNames { "Start", "Half", "3/4", "7/8" };
    inline constexpr double stutterStartFraction[] = { 0.0, 0.5, 0.75, 0.875 };

    // Stutter presets: plain values (indices into the choice lists above)
    struct StutterPreset
    {
        const char* name;
        int trigger, every, slice, start;      // trigger 0 = Auto, 1 = Mod Wheel
        float chance, length, gate, pitch, decay;
        bool reverse;
        float mix;
    };

    inline constexpr StutterPreset stutterPresets[] = {
        //  name                 trig every slice start chance length gate  pitch  decay  rev   mix
        { "End Of Bar Fill",      0,   2,    2,    2,   1.0f,  1.0f, 1.0f,   0.0f, 0.0f,  false, 1.0f },
        { "Last Beat Roll",       0,   2,    3,    2,   0.8f,  1.0f, 0.85f,  0.0f, 0.0f,  false, 1.0f },
        { "Rising Roll",          0,   3,    3,    1,   1.0f,  1.0f, 0.8f,  12.0f, 0.0f,  false, 1.0f },
        { "Tape Slowdown",        0,   2,    1,    3,   0.7f,  1.0f, 1.0f, -12.0f, 0.05f, false, 1.0f },
        { "Reverse Tail",         0,   2,    1,    2,   0.6f,  1.0f, 1.0f,   0.0f, 0.1f,  true,  1.0f },
        { "Half Bar Echo",        0,   1,    1,    1,   0.5f,  1.0f, 0.9f,   0.0f, 0.25f, false, 0.7f },
        { "Glitch Sparkle",       0,   1,    4,    3,   0.6f,  1.0f, 0.6f,   7.0f, 0.0f,  false, 1.0f },
        { "Choppy Eighths",       0,   2,    1,    0,   0.4f,  0.5f, 0.45f,  0.0f, 0.0f,  false, 1.0f },
        { "Dub Repeats",          0,   2,    1,    1,   0.5f,  1.0f, 1.0f,   0.0f, 0.3f,  false, 0.6f },
        { "Two Bar Breakdown",    0,   3,    2,    1,   1.0f,  1.0f, 0.7f,  -5.0f, 0.04f, false, 1.0f },
        { "Soft Stammer",         0,   2,    3,    3,   0.5f,  1.0f, 0.6f,   0.0f, 0.15f, false, 0.5f },
        { "Wheel Roll 1/16",      1,   2,    2,    0,   1.0f,  1.0f, 0.9f,   0.0f, 0.0f,  false, 1.0f },
        { "Wheel Roll 1/32",      1,   2,    3,    0,   1.0f,  1.0f, 0.8f,   5.0f, 0.0f,  false, 1.0f },
        { "Wheel Reverse",        1,   2,    1,    0,   1.0f,  1.0f, 1.0f,   0.0f, 0.0f,  true,  1.0f },
    };

    struct StutterParams
    {
        bool on = false, reverse = false;
        bool modWheelTrigger = false;  // true: stutters while the mod wheel is above the centre
        double intervalBeats = 4.0;    // grid ("Every") on which a stutter can start
        double sliceBeats = 0.25;      // length of the repeated piece
        double startFraction = 0.0;    // where in the grid the stutter starts (e.g. 3/4 = last quarter)
        float chance = 0.5f;           // probability per grid cell (auto)
        float length = 1.0f;           // fraction of the remaining grid that stutters
        float gate = 1.0f;             // audible part of each repeat
        float pitch = 0.0f;            // semitones of total glide across the repeats
        float decay = 0.0f;            // level loss per repeat
        float mix = 1.0f;
    };

    // Stutter / beat repeat: records a short piece and repeats it (optionally reversed, pitched, gated).
    class Stutter
    {
    public:
        void prepare (double sr)
        {
            sampleRate = (float) sr;
            for (auto& b : slice) b.assign ((size_t) (sr * 4.1), 0.0f);
            reset();
        }

        void reset() { active = false; lastInterval = -1; wheelWasUp = false; }

        // For the display: is a repeat currently playing?
        bool isRepeating() const { return active && recorded >= sliceLen; }

        void process (juce::AudioBuffer<float>& buffer, const Clock& clock, const StutterParams& p, float modWheel)
        {
            if (! p.on || ! clock.running) { active = false; return; }

            auto* io0 = buffer.getWritePointer (0);
            auto* io1 = buffer.getWritePointer (1);
            const double samplesPerBeat = 1.0 / std::max (1.0e-9, clock.beatsPerSample);
            sliceLen = std::clamp ((int) (p.sliceBeats * samplesPerBeat), 64, (int) slice[0].size() - 1);
            const bool wheelUp = modWheel > 0.5f;

            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                const double ppq = clock.at (i);

                if (p.modWheelTrigger)
                {
                    // Live mode: stuttering starts when the mod wheel goes up, on the next slice boundary
                    if (wheelUp && ! wheelWasUp)
                        begin (std::ceil (ppq / p.sliceBeats - 1.0e-9) * p.sliceBeats, 1.0e12);
                    if (! wheelUp) active = false;
                }
                else
                {
                    const int64_t interval = (int64_t) std::floor (ppq / p.intervalBeats);
                    if (interval != lastInterval)
                    {
                        // New grid cell: roll the dice (deterministic per cell) whether to stutter
                        lastInterval = interval;
                        const double s = ((double) interval + p.startFraction) * p.intervalBeats;
                        const double e = s + (1.0 - p.startFraction) * p.intervalBeats * p.length;
                        if ((dsp::hashBipolar (interval, 4242u) * 0.5f + 0.5f) < p.chance) begin (s, e);
                        else active = false;
                    }
                }
                wheelWasUp = wheelUp;

                if (! active || ppq < startPpq || ppq >= endPpq)
                    continue;

                const float inL = io0[i], inR = io1[i];
                float wetL, wetR;
                if (recorded < sliceLen)
                {
                    // First pass: record and let the signal through
                    slice[0][(size_t) recorded] = inL;
                    slice[1][(size_t) recorded] = inR;
                    ++recorded;
                    wetL = inL;
                    wetR = inR;
                }
                else
                {
                    const double span = std::min (endPpq, startPpq + 64.0) - startPpq;
                    const double progress = std::clamp ((ppq - startPpq) / std::max (1.0e-6, span), 0.0, 1.0);
                    const double rate = std::exp2 (p.pitch * progress / 12.0);
                    const double pos = p.reverse ? (double) sliceLen - 1.0 - playPos : playPos;
                    const int i0 = std::clamp ((int) pos, 0, sliceLen - 1);
                    const int i1 = std::min (i0 + 1, sliceLen - 1);
                    const float fr = (float) (pos - std::floor (pos));

                    // Gate and short fades at the edges against clicks
                    const float gateLen = (float) sliceLen * std::clamp (p.gate, 0.05f, 1.0f);
                    const float edge = std::min ({ 1.0f, (float) playPos / 48.0f, (gateLen - (float) playPos) / 48.0f });
                    const float g = gain * std::max (0.0f, edge);
                    wetL = (slice[0][(size_t) i0] * (1.0f - fr) + slice[0][(size_t) i1] * fr) * g;
                    wetR = (slice[1][(size_t) i0] * (1.0f - fr) + slice[1][(size_t) i1] * fr) * g;

                    playPos += rate;
                    if (playPos >= sliceLen)
                    {
                        playPos -= sliceLen;
                        gain *= 1.0f - p.decay;
                    }
                }

                io0[i] = inL + (wetL - inL) * p.mix;
                io1[i] = inR + (wetR - inR) * p.mix;
            }
        }

    private:
        void begin (double from, double to)
        {
            active = true;
            startPpq = from;
            endPpq = to;
            recorded = 0;
            playPos = 0.0;
            gain = 1.0f;
        }

        float sampleRate = 44100.0f, gain = 1.0f;
        std::array<std::vector<float>, 2> slice;
        bool active = false, wheelWasUp = false;
        int recorded = 0, sliceLen = 64;
        double playPos = 0.0, startPpq = 0.0, endPpq = 0.0;
        int64_t lastInterval = -1;
    };

    // ---------------------------------------------------------------------------------------------
    struct PumpParams
    {
        bool on = false;
        double beats = 1.0;
        float depth = 0.6f, release = 0.6f, curve = 1.0f;
    };

    // Sidechain pump: tempo-synced ducking as if triggered by a kick.
    inline float pumpGain (double phase, const PumpParams& p)
    {
        constexpr double attack = 0.015;
        if (phase < attack) return 1.0f - p.depth * (float) (phase / attack);
        const float rec = std::clamp ((float) ((phase - attack) / std::max (0.05f, p.release)), 0.0f, 1.0f);
        return 1.0f - p.depth * (1.0f - std::pow (rec, p.curve));
    }

    class Pump
    {
    public:
        void process (juce::AudioBuffer<float>& buffer, const Clock& clock, const PumpParams& p)
        {
            if (! p.on || ! clock.running) return;
            auto* l = buffer.getWritePointer (0);
            auto* r = buffer.getWritePointer (1);
            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                const double ppq = clock.at (i) / p.beats;
                const float g = pumpGain (ppq - std::floor (ppq), p);
                l[i] *= g;
                r[i] *= g;
            }
        }
    };
}
