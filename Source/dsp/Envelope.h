#pragma once
#include "DspCommon.h"

namespace fjord::dsp
{
    // Analog-style ADSR envelope (exponential RC curves).
    // Retriggering starts from the current level – no clicks on legato or voice stealing.
    class Envelope
    {
    public:
        enum class Stage { Idle, Attack, Decay, Sustain, Release };

        void prepare (float sr) { sampleRate = sr; }

        // curve 0 = strongly exponential (classic), 1 = nearly linear: the body holds longer
        void setCurve (float curve)
        {
            decayRatio = 0.0001f * std::pow (20000.0f, std::clamp (curve, 0.0f, 1.0f));
        }

        void setParameters (float attackS, float decayS, float sustain, float releaseS)
        {
            sustainLevel = sustain;
            attackCoef  = coef (attackS,  attackRatio);
            decayCoef   = coef (decayS,   decayRatio);
            releaseCoef = coef (releaseS, decayRatio);
            attackBase  = (1.0f + attackRatio) * (1.0f - attackCoef);
            decayBase   = (sustainLevel - decayRatio) * (1.0f - decayCoef);
            releaseBase = -decayRatio * (1.0f - releaseCoef);
        }

        void noteOn()  { stage = Stage::Attack; }
        void noteOff() { if (stage != Stage::Idle) stage = Stage::Release; }
        void reset()   { stage = Stage::Idle; level = 0.0f; }

        bool isActive() const   { return stage != Stage::Idle; }
        bool isReleasing() const { return stage == Stage::Release; }
        float getLevel() const  { return level; }

        float next()
        {
            switch (stage)
            {
                case Stage::Idle:
                    break;

                case Stage::Attack:
                    level = attackBase + level * attackCoef;
                    if (level >= 1.0f || attackCoef == 0.0f) { level = 1.0f; stage = Stage::Decay; }
                    break;

                case Stage::Decay:
                    level = decayBase + level * decayCoef;
                    if (level <= sustainLevel || decayCoef == 0.0f) { level = sustainLevel; stage = Stage::Sustain; }
                    break;

                case Stage::Sustain:
                    level += (sustainLevel - level) * 0.002f;   // follows sustain changes smoothly
                    break;

                case Stage::Release:
                    level = releaseBase + level * releaseCoef;
                    if (level <= 0.0f || releaseCoef == 0.0f) { level = 0.0f; stage = Stage::Idle; }
                    break;
            }
            return level;
        }

    private:
        float coef (float seconds, float ratio) const
        {
            const float samples = seconds * sampleRate;
            return samples <= 1.0f ? 0.0f : std::exp (-std::log ((1.0f + ratio) / ratio) / samples);
        }

        static constexpr float attackRatio = 0.3f;     // slightly convex, "analog" attack
        float decayRatio = 0.0001f;

        float sampleRate = 44100.0f;
        Stage stage = Stage::Idle;
        float level = 0.0f, sustainLevel = 1.0f;
        float attackCoef = 0.0f, decayCoef = 0.0f, releaseCoef = 0.0f;
        float attackBase = 0.0f, decayBase = 0.0f, releaseBase = 0.0f;
    };
}
