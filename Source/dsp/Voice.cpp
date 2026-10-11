#include "Voice.h"

namespace fjord::dsp
{
    static float hzToSemis (float hz) { return 12.0f * std::log2 (hz / 440.0f) + 69.0f; }

    void Voice::prepare (float sr, int, uint32_t seed)
    {
        sampleRate = sr;
        noiseScale = std::sqrt (sr / 48000.0f);   // same audible noise energy at every sample rate
        rng.seed (seed);
        rngLate.seed (seed ^ 0x5bd1e995u);
        filter1.prepare (sr);
        filter2.prepare (sr);
        ampEnv.prepare (sr);
        filterEnv.prepare (sr);
        modEnv.prepare (sr);

        // Short smoothing: prevents zipper noise but lets fast transients (drums, clicks) through
        for (auto& s : oscLevel) s.setTime (0.0008f, sr);
        subLevel.setTime (0.0008f, sr);
        noiseLevel.setTime (0.0008f, sr);
        for (auto* s : { &cutoff1, &cutoff2 }) s->setTime (0.004f, sr);
        for (auto* s : { &reso1, &reso2, &drive1, &drive2 }) s->setTime (0.01f, sr);

        ampEnv.reset();
        filterEnv.reset();
        modEnv.reset();
    }

    void Voice::start (int newNote, float vel, float startNote, bool retrigger, const EngineParams& p)
    {
        const bool wasActive = isActive();
        note = newNote;
        velocity = vel;
        targetPitch = (float) newNote;
        currentPitch = startNote;
        age = ++ageCounter;
        noteRandom = rng.nextBipolar();
        polyAftertouch = 0.0f;

        if (! wasActive)
        {
            filter1.reset();
            filter2.reset();
            for (int i = 0; i < kNumOsc; ++i)
            {
                osc[(size_t) i].reset (! p.osc[(size_t) i].retrig, rngFor (i));
                oscLevel[(size_t) i].reset (p.osc[(size_t) i].on ? p.osc[(size_t) i].level : 0.0f);
            }
            subPhase = 0.0f;
            subLevel.reset (p.subLevel);
            noiseLevel.reset (p.noiseLevel);
            cutoff1.reset (hzToSemis (p.filter1.cutoff));
            cutoff2.reset (hzToSemis (p.filter2.cutoff));
            reso1.reset (p.filter1.reso);
            reso2.reset (p.filter2.reso);
            drive1.reset (p.filter1.drive);
            drive2.reset (p.filter2.drive);
            volume = 1.0f;
            panL = panR = 1.0f;
            driftCents.fill (0.0f);
        }
        else
        {
            for (int i = 0; i < kNumOsc; ++i)
                if (p.osc[(size_t) i].retrig)
                    osc[(size_t) i].reset (false, rngFor (i));
        }

        // Component tolerances: every note is tuned slightly differently (scaled by "Analog")
        for (int i = 0; i < kNumOsc; ++i)
            staticCents[(size_t) i] = rngFor (i).nextBipolar() * 4.0f;
        staticCutoff = rng.nextBipolar() * 1.5f;

        if (retrigger || ! wasActive)
        {
            ampEnv.noteOn();
            filterEnv.noteOn();
            modEnv.noteOn();
            samplesSinceStart = 0.0;
            for (int i = 0; i < 3; ++i)
                if (p.lfo[(size_t) i].retrig)
                    lfoPhase[(size_t) i] = 0.0;
        }
    }

    void Voice::glideTo (int newNote)
    {
        note = newNote;
        targetPitch = (float) newNote;
    }

    void Voice::release()
    {
        ampEnv.noteOff();
        filterEnv.noteOff();
        modEnv.noteOff();
    }

    void Voice::kill()
    {
        ampEnv.reset();
        filterEnv.reset();
        modEnv.reset();
    }

    void Voice::updateDrift (float analog, int samples)
    {
        if (analog <= 0.0f)
            return;

        driftCountdown -= samples;
        if (driftCountdown <= 0)
        {
            driftCountdown = (int) (sampleRate * (0.25f + 0.5f * rng.next01()));
            for (int i = 0; i < kNumOsc; ++i)
                driftTarget[(size_t) i] = rngFor (i).nextBipolar();
        }

        const float c = 1.0f - std::exp (-(float) samples / (0.35f * sampleRate));
        for (int i = 0; i < kNumOsc; ++i)
            driftCents[(size_t) i] += (driftTarget[(size_t) i] - driftCents[(size_t) i]) * c;
    }

    void Voice::computeModulation (const EngineParams& p, const ModContext& ctx, int offset, int samples, Mods& mods)
    {
        // The mod envelope runs at control rate (it only modulates, it makes no sound)
        float modEnvLevel = 0.0f;
        for (int i = 0; i < samples; ++i)
            modEnvLevel = modEnv.next();

        // LFOs
        const float fadeTime = (float) (samplesSinceStart / sampleRate);
        for (int i = 0; i < 3; ++i)
        {
            const auto& lp = p.lfo[(size_t) i];
            double phase;
            if (lp.retrig)
            {
                const double inc = lp.sync ? (ctx.bpm / 60.0) / lp.beats / sampleRate : lp.rateHz / sampleRate;
                const float rateMod = mods[(size_t) ModDest::Lfo1Rate + (size_t) i];
                phase = lfoPhase[(size_t) i];
                lfoPhase[(size_t) i] += inc * std::exp2 (rateMod * 4.0f) * samples;
            }
            else
            {
                phase = ctx.lfoPhase[(size_t) i] + ctx.lfoInc[(size_t) i] * offset;
            }

            const float fade = lp.fade > 0.001f ? std::min (1.0f, fadeTime / lp.fade) : 1.0f;
            lfoOut[(size_t) i] = lfoValue (lp.shape, phase, 1000u + (uint32_t) i) * fade;
        }

        std::array<float, (size_t) ModSource::Count> src {};
        src[(size_t) ModSource::Lfo1] = lfoOut[0];
        src[(size_t) ModSource::Lfo2] = lfoOut[1];
        src[(size_t) ModSource::Lfo3] = lfoOut[2];
        src[(size_t) ModSource::AmpEnv] = ampEnv.getLevel();
        src[(size_t) ModSource::FilterEnv] = filterEnv.getLevel();
        src[(size_t) ModSource::ModEnv] = modEnvLevel;
        src[(size_t) ModSource::Velocity] = velocity;
        src[(size_t) ModSource::Note] = ((float) note - 60.0f) / 60.0f;
        src[(size_t) ModSource::ModWheel] = ctx.modWheel;
        src[(size_t) ModSource::Aftertouch] = std::max (ctx.aftertouch, polyAftertouch);
        src[(size_t) ModSource::PitchBend] = ctx.pitchBend;
        src[(size_t) ModSource::Random] = noteRandom;
        for (int m = 0; m < kNumMacros; ++m)
            src[(size_t) ModSource::Macro1 + (size_t) m] = p.macros[(size_t) m];

        mods.fill (0.0f);
        for (const auto& slot : p.matrix)
        {
            if (slot.source == ModSource::None || slot.dest == ModDest::None || slot.amount == 0.0f)
                continue;
            float v = src[(size_t) slot.source] * slot.amount;
            if (slot.via != ModSource::None)
                v *= src[(size_t) slot.via];
            mods[(size_t) slot.dest] += v;
        }

        samplesSinceStart += samples;
    }

    void Voice::updateControl (const EngineParams& p, const ModContext& ctx, const Mods& m, int samples)
    {
        auto mod = [&m] (ModDest d) { return m[(size_t) d]; };

        // Portamento: exponential approach in semitones (like analog glide circuits).
        if (p.glide > 0.0005f)
        {
            const float c = std::exp (-(float) samples / (p.glide * 0.35f * sampleRate));
            currentPitch = targetPitch + (currentPitch - targetPitch) * c;
        }
        else
        {
            currentPitch = targetPitch;
        }

        updateDrift (p.analog, samples);
        // Key follow: at 0 every key plays at the pitch of C3 (drums)
        const float keyed = 60.0f + (currentPitch - 60.0f) * p.keyFollow;
        const float pitch = keyed + ctx.pitchBend * p.bendRange + mod (ModDest::Pitch) * 24.0f;

        for (int i = 0; i < kNumOsc; ++i)
        {
            const auto& op = p.osc[(size_t) i];
            const auto oi = (size_t) i;
            if (! op.on && oscLevel[oi].value == 0.0f)
                continue;   // silent oscillator: process() only advances its phase
            OscSettings s;
            s.wave = op.wave;
            const float analogCents = p.analog * (staticCents[oi] + driftCents[oi] * 7.0f);
            s.pitchNote = pitch + (float) (op.octave * 12 + op.semi) + (op.fine + analogCents) * 0.01f
                        + m[(size_t) oscDest (OscMod::Pitch, i)] * 24.0f;
            s.pw = std::clamp (op.pw + m[(size_t) oscDest (OscMod::Pw, i)] * 0.45f, 0.05f, 0.95f);
            s.unison = op.unison;
            s.detune = std::clamp (op.detune + mod (ModDest::Detune), 0.0f, 1.0f);
            s.spread = std::clamp (op.spread + mod (ModDest::Spread), 0.0f, 1.0f);
            s.blend = op.blend;
            s.pan = op.pan;
            s.table = op.table;
            s.wtPos = std::clamp (op.wtPos + m[(size_t) oscDest (OscMod::WtPos, i)], 0.0f, 1.0f);
            osc[oi].update (s, sampleRate);
        }

        subInc = std::min (0.45f, noteToHz (pitch + 12.0f * (float) p.subOctave) / sampleRate);

        // Envelope times (modulatable via Env Attack / Env Release)
        const float atk = std::exp2 (mod (ModDest::EnvAttack) * 5.0f);
        const float rel = std::exp2 (mod (ModDest::EnvRelease) * 5.0f);
        ampEnv.setCurve (p.ampCurve);
        ampEnv.setParameters (p.ampEnv.attack * atk, p.ampEnv.decay, p.ampEnv.sustain, p.ampEnv.release * rel);
        filterEnv.setParameters (p.filterEnv.attack * atk, p.filterEnv.decay, p.filterEnv.sustain, p.filterEnv.release * rel);
        modEnv.setParameters (p.modEnv.attack * atk, p.modEnv.decay, p.modEnv.sustain, p.modEnv.release * rel);

        filter1.setType (p.filter1.type);
        filter2.setType (p.filter2.type);

        const float velCutoff = p.filterVelocity * (velocity - 0.5f) * 48.0f;
        const float analogCut = p.analog * (staticCutoff + driftCents[0] * 0.6f);
        const float key = (float) note - 60.0f;
        cutoff1Target = hzToSemis (p.filter1.cutoff) + p.filter1.keyTrack * key + velCutoff + analogCut + mod (ModDest::Cutoff1) * 96.0f;
        cutoff2Target = hzToSemis (p.filter2.cutoff) + p.filter2.keyTrack * key + velCutoff + analogCut + mod (ModDest::Cutoff2) * 96.0f;
        envRange1 = (p.filter1.envAmount + mod (ModDest::FilterEnvAmount)) * 96.0f;
        envRange2 = (p.filter2.envAmount + mod (ModDest::FilterEnvAmount)) * 96.0f;
        reso1Target = std::clamp (p.filter1.reso + mod (ModDest::Reso1), 0.0f, 1.0f);
        reso2Target = std::clamp (p.filter2.reso + mod (ModDest::Reso2), 0.0f, 1.0f);
        drive1Target = std::clamp (p.filter1.drive + mod (ModDest::Drive1), 0.0f, 1.0f);
        drive2Target = std::clamp (p.filter2.drive, 0.0f, 1.0f);
    }

    float Voice::nextNoise (bool pink)
    {
        const float white = rng.nextBipolar() * noiseScale;
        if (! pink)
            return white * 0.5f;

        // Paul Kellet "economy" pink noise filter
        pinkState[0] = 0.99765f * pinkState[0] + white * 0.0990460f;
        pinkState[1] = 0.96300f * pinkState[1] + white * 0.2965164f;
        pinkState[2] = 0.57000f * pinkState[2] + white * 1.0526913f;
        return (pinkState[0] + pinkState[1] + pinkState[2] + white * 0.1848f) * 0.18f;
    }

    void Voice::render (float* outL, float* outR, int numSamples, const EngineParams& p, const ModContext& ctx)
    {
        const float ampVelGain = 1.0f - p.ampVelocity * (1.0f - velocity);
        const bool f1 = p.filter1.on, f2 = p.filter2.on;
        Mods mods {};

        for (int start = 0; start < numSamples; start += kControlBlock)
        {
            const int n = std::min (kControlBlock, numSamples - start);
            computeModulation (p, ctx, start, n, mods);
            updateControl (p, ctx, mods, n);

            float* L = bufL.data();
            float* R = bufR.data();
            std::fill (L, L + n, 0.0f);
            std::fill (R, R + n, 0.0f);

            // Oscillators: level smoothed per control block
            for (int i = 0; i < kNumOsc; ++i)
            {
                const auto& op = p.osc[(size_t) i];
                const float target = op.on ? std::clamp (op.level + mods[(size_t) oscDest (OscMod::Level, i)], 0.0f, 1.0f) : 0.0f;
                auto& sm = oscLevel[(size_t) i];
                float lvl = sm.value;
                for (int k = 0; k < n; ++k) lvl = sm.process (target);
                if (target == 0.0f && lvl < 1.0e-5f)
                    sm.reset (lvl = 0.0f);   // fully faded out: skip the oscillator from now on
                osc[(size_t) i].process (L, R, n, lvl);
            }

            // Sub oscillator and noise
            const float subTarget = std::clamp (p.subLevel + mods[(size_t) ModDest::SubLevel], 0.0f, 1.0f);
            const float noiseTarget = std::clamp (p.noiseLevel + mods[(size_t) ModDest::NoiseLevel], 0.0f, 1.0f);
            for (int k = 0; k < n; ++k)
            {
                const float sl = subLevel.process (subTarget);
                const float nl = noiseLevel.process (noiseTarget);

                if (sl > 0.0001f)
                {
                    const float s = p.subSquare ? renderVA (Wave::Square, subPhase, subInc, 0.5f)
                                                : SineTable::get().lookup (subPhase);
                    L[k] += s * sl;
                    R[k] += s * sl;
                }
                subPhase += subInc;
                if (subPhase >= 1.0f) subPhase -= 1.0f;

                if (nl > 0.0001f)
                {
                    L[k] += nextNoise (p.noisePink) * nl;
                    R[k] += nextNoise (p.noisePink) * nl;
                }
            }

            // Volume and pan from the matrix – linearly interpolated across the block
            const float volTarget = std::clamp (1.0f + mods[(size_t) ModDest::Volume], 0.0f, 2.0f);
            const float pan = std::clamp (mods[(size_t) ModDest::Pan], -1.0f, 1.0f);
            const float angle = (pan + 1.0f) * kPi * 0.25f;
            const float panLTarget = std::cos (angle) * 1.41421356f, panRTarget = std::sin (angle) * 1.41421356f;
            const float inv = 1.0f / (float) n;
            const float dVol = (volTarget - volume) * inv, dL = (panLTarget - panL) * inv, dR = (panRTarget - panR) * inv;

            // Filter + amplifier
            for (int k = 0; k < n; ++k)
            {
                const float fenv = filterEnv.next();
                const float amp  = ampEnv.next();

                float l = L[k] * 0.32f, r = R[k] * 0.32f;
                const float c1 = cutoff1.process (cutoff1Target), c2 = cutoff2.process (cutoff2Target);
                const float rs1 = reso1.process (reso1Target), rs2 = reso2.process (reso2Target);
                const float dr1 = drive1.process (drive1Target), dr2 = drive2.process (drive2Target);

                if (f1 && f2 && p.filterParallel)
                {
                    float l2 = l, r2 = r;
                    filter1.process (l, r, noteToHz (std::clamp (c1 + fenv * envRange1, 0.0f, 140.0f)), rs1, dr1);
                    filter2.process (l2, r2, noteToHz (std::clamp (c2 + fenv * envRange2, 0.0f, 140.0f)), rs2, dr2);
                    l = (l + l2) * 0.6f;
                    r = (r + r2) * 0.6f;
                }
                else
                {
                    if (f1) filter1.process (l, r, noteToHz (std::clamp (c1 + fenv * envRange1, 0.0f, 140.0f)), rs1, dr1);
                    if (f2) filter2.process (l, r, noteToHz (std::clamp (c2 + fenv * envRange2, 0.0f, 140.0f)), rs2, dr2);
                }

                volume += dVol;
                panL += dL;
                panR += dR;
                const float g = amp * ampVelGain * volume;
                outL[start + k] += l * g * panL;
                outR[start + k] += r * g * panR;
            }

            if (! ampEnv.isActive())
                break;
        }
    }
}
