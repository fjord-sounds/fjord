#pragma once
#include "Effects.h"

namespace fjord::fx
{
    class FxChain
    {
    public:
        void prepare (double sr, int maxBlock)
        {
            drive.prepare (sr, maxBlock);
            crush.prepare (sr);
            eq.prepare (sr);
            comp.prepare (sr);
            chorus.prepare (sr);
            phaser.prepare (sr);
            flanger.prepare (sr);
            delay.prepare (sr);
            reverb.prepare (sr);
            width.prepare (sr);
            wasOn.fill (false);
        }

        void process (juce::AudioBuffer<float>& buffer, const FxParams& p, const Order& order, double bpm)
        {
            const StereoBuffer b { buffer.getWritePointer (0), buffer.getWritePointer (1), buffer.getNumSamples() };

            for (int index : order)
            {
                const auto type = (FxType) index;
                const bool on = p.on[(size_t) index];
                if (on && ! wasOn[(size_t) index])
                    reset (type);
                wasOn[(size_t) index] = on;
                if (! on)
                    continue;

                switch (type)
                {
                    case FxType::Drive:   drive.process (b, p); break;
                    case FxType::Crush:   crush.process (b, p); break;
                    case FxType::Eq:      eq.process (b, p); break;
                    case FxType::Comp:    comp.process (b, p); break;
                    case FxType::Chorus:  chorus.process (b, p); break;
                    case FxType::Phaser:  phaser.process (b, p); break;
                    case FxType::Flanger: flanger.process (b, p); break;
                    case FxType::Delay:   delay.process (b, p, bpm); break;
                    case FxType::Reverb:  reverb.process (b, p); break;
                    case FxType::Width:   width.process (b, p); break;
                    case FxType::Count:   break;
                }
            }
        }

        float getCompressorReduction() const { return comp.getGainReduction(); }
        float getCompressorInput() const     { return comp.getInputPeak(); }
        float getCompressorOutput() const    { return comp.getOutputPeak(); }

    private:
        void reset (FxType t)
        {
            switch (t)
            {
                case FxType::Drive:   drive.reset(); break;
                case FxType::Crush:   crush.reset(); break;
                case FxType::Eq:      eq.reset(); break;
                case FxType::Comp:    comp.reset(); break;
                case FxType::Chorus:  chorus.reset(); break;
                case FxType::Phaser:  phaser.reset(); break;
                case FxType::Flanger: flanger.reset(); break;
                case FxType::Delay:   delay.reset(); break;
                case FxType::Reverb:  reverb.reset(); break;
                case FxType::Width:   width.reset(); break;
                case FxType::Count:   break;
            }
        }

        Drive drive;
        Crush crush;
        Eq eq;
        Comp comp;
        Chorus chorus;
        Phaser phaser;
        Flanger flanger;
        Delay delay;
        Reverb reverb;
        Width width;
        std::array<bool, kNumFx> wasOn {};
    };
}
