#include "Engine.h"

namespace fjord::dsp
{
    void Engine::prepare (double sr, int maxBlockSize)
    {
        sampleRate = sr;
        for (size_t i = 0; i < voices.size(); ++i)
            voices[i].prepare ((float) sr, maxBlockSize, 0x9e3779b9u * (uint32_t) (i + 1));

        noteStack.reserve (128);
        allNotesOff();
    }

    void Engine::allNotesOff()
    {
        for (auto& v : voices) v.kill();
        keyDown.fill (false);
        sustainedNote.fill (false);
        noteStack.clear();
        sustainPedal = false;
    }

    void Engine::updateLfoClock (const EngineParams& p, const TimeInfo& time, int numSamples)
    {
        ctx.bpm = time.bpm > 1.0 ? time.bpm : 120.0;
        for (size_t i = 0; i < 3; ++i)
        {
            const auto& lp = p.lfo[i];
            const double inc = lp.sync ? (ctx.bpm / 60.0) / lp.beats / sampleRate : lp.rateHz / sampleRate;
            ctx.lfoInc[i] = inc;

            // With the host playing, a synced LFO locks exactly to the song position.
            ctx.lfoPhase[i] = (lp.sync && time.playing) ? time.ppq / lp.beats : freePhase[i];
            freePhase[i] = ctx.lfoPhase[i] + inc * numSamples;
            freePhase[i] -= std::floor (freePhase[i] / 4096.0) * 4096.0;   // keep precision
        }
    }

    void Engine::releaseAll()
    {
        for (auto& v : voices)
            if (v.isActive()) v.release();
        keyDown.fill (false);
        sustainedNote.fill (false);
        noteStack.clear();
    }

    void Engine::process (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi, const EngineParams& p,
                          const TimeInfo& time)
    {
        int pos = 0;
        const int total = buffer.getNumSamples();
        updateLfoClock (p, time, total);
        blockPhase = ctx.lfoPhase;

        // Sample-accurate MIDI: render up to the next event, then apply the event.
        for (const auto meta : midi)
        {
            const int eventPos = std::clamp (meta.samplePosition, 0, total);
            if (eventPos > pos)
            {
                for (size_t i = 0; i < 3; ++i) ctx.lfoPhase[i] = blockPhase[i] + ctx.lfoInc[i] * pos;
                renderVoices (buffer, pos, eventPos - pos, p);
                pos = eventPos;
            }
            handleMidi (meta.getMessage(), p);
        }

        if (pos < total)
        {
            for (size_t i = 0; i < 3; ++i) ctx.lfoPhase[i] = blockPhase[i] + ctx.lfoInc[i] * pos;
            renderVoices (buffer, pos, total - pos, p);
        }
    }

    float Engine::globalSource (ModSource s, const EngineParams& p) const
    {
        switch (s)
        {
            case ModSource::Lfo1: case ModSource::Lfo2: case ModSource::Lfo3:
            {
                const auto i = (size_t) ((int) s - (int) ModSource::Lfo1);
                return lfoValue (p.lfo[i].shape, blockPhase[i], 1000u + (uint32_t) i);
            }
            case ModSource::ModWheel:   return ctx.modWheel;
            case ModSource::Aftertouch: return ctx.aftertouch;
            case ModSource::PitchBend:  return ctx.pitchBend;
            default: break;
        }
        if (s >= ModSource::Macro1 && s <= ModSource::Macro8)
            return p.macros[(size_t) ((int) s - (int) ModSource::Macro1)];
        return 0.0f;
    }

    void Engine::renderVoices (juce::AudioBuffer<float>& buffer, int start, int numSamples, const EngineParams& p)
    {
        auto* L = buffer.getWritePointer (0) + start;
        auto* R = buffer.getWritePointer (1) + start;

        for (auto& v : voices)
            if (v.isActive())
                v.render (L, R, numSamples, p, ctx);
    }

    void Engine::handleMidi (const juce::MidiMessage& m, const EngineParams& p)
    {
        if (m.isNoteOn())
            noteOn (m.getNoteNumber(), m.getFloatVelocity(), p);
        else if (m.isNoteOff())
            noteOff (m.getNoteNumber(), p);
        else if (m.isPitchWheel())
            ctx.pitchBend = (float) (m.getPitchWheelValue() - 8192) / 8192.0f;
        else if (m.isControllerOfType (1))
            ctx.modWheel = (float) m.getControllerValue() / 127.0f;
        else if (m.isChannelPressure())
            ctx.aftertouch = (float) m.getChannelPressureValue() / 127.0f;
        else if (m.isAftertouch())
        {
            for (auto& v : voices)
                if (v.isActive() && v.getNote() == m.getNoteNumber())
                    v.setPolyAftertouch ((float) m.getAfterTouchValue() / 127.0f);
        }
        else if (m.isSustainPedalOn())
            sustainPedal = true;
        else if (m.isSustainPedalOff())
        {
            sustainPedal = false;
            for (int n = 0; n < 128; ++n)
                if (sustainedNote[(size_t) n])
                {
                    sustainedNote[(size_t) n] = false;
                    noteOff (n, p);
                }
        }
        else if (m.isAllNotesOff() || m.isAllSoundOff())
            allNotesOff();
    }

    Voice* Engine::findVoiceToUse (const EngineParams& p)
    {
        const int limit = std::clamp (p.polyphony, 1, kMaxVoices);

        for (int i = 0; i < limit; ++i)
            if (! voices[(size_t) i].isActive())
                return &voices[(size_t) i];

        // Stealing: prefer the oldest released voice, otherwise the oldest one overall.
        Voice* best = nullptr;
        for (int i = 0; i < limit; ++i)
        {
            auto& v = voices[(size_t) i];
            if (best == nullptr
                || (v.isReleasing() && ! best->isReleasing())
                || (v.isReleasing() == best->isReleasing() && v.getAge() < best->getAge()))
                best = &v;
        }

        // Let voices above the limit (after a polyphony change) fade out.
        for (int i = limit; i < kMaxVoices; ++i)
            voices[(size_t) i].release();

        return best;
    }

    void Engine::noteOn (int note, float velocity, const EngineParams& p)
    {
        keyDown[(size_t) note] = true;
        sustainedNote[(size_t) note] = false;
        const float startNote = (p.glide > 0.0005f && lastNote >= 0.0f) ? lastNote : (float) note;

        if (p.voiceMode == VoiceMode::Poly)
        {
            if (auto* v = findVoiceToUse (p))
                v->start (note, velocity, startNote, true, p);
        }
        else
        {
            std::erase (noteStack, note);
            noteStack.push_back (note);

            auto& v = voices[0];
            const bool legato = p.voiceMode == VoiceMode::Legato && v.isActive() && ! v.isReleasing();
            if (legato)
                v.glideTo (note);
            else
                v.start (note, velocity, startNote, true, p);
        }

        lastNote = (float) note;
    }

    void Engine::noteOff (int note, const EngineParams& p)
    {
        keyDown[(size_t) note] = false;

        if (sustainPedal)
        {
            sustainedNote[(size_t) note] = true;
            return;
        }

        if (p.voiceMode == VoiceMode::Poly)
        {
            for (auto& v : voices)
                if (v.isActive() && ! v.isReleasing() && v.getNote() == note)
                    v.release();
            return;
        }

        const bool wasTop = ! noteStack.empty() && noteStack.back() == note;
        std::erase (noteStack, note);
        auto& v = voices[0];

        if (noteStack.empty())
        {
            if (v.getNote() == note)
                v.release();
        }
        else if (wasTop)
        {
            // Back to the previously held key (last-note priority)
            const int prev = noteStack.back();
            if (p.voiceMode == VoiceMode::Legato)
                v.glideTo (prev);
            else
                v.start (prev, 0.8f, (float) note, true, p);
            lastNote = (float) prev;
        }
    }
}
