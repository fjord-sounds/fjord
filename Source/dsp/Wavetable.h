#pragma once
#include <juce_core/juce_core.h>
#include <vector>
#include "DspCommon.h"

namespace fjord::dsp
{
    // Algorithmically generated wavetables (no samples): each table has 8 frames that are
    // morphed seamlessly, and 11 band-limited mip levels (one per octave) against aliasing.
    class WavetableBank
    {
    public:
        static constexpr int kFrames = 8;
        static constexpr int kLevels = 11;          // level L contains at most 1024 >> L harmonics

        struct Level
        {
            int size = 0, mask = 0;
            std::vector<float> data;                // kFrames * size
            const float* frame (int f) const { return data.data() + (size_t) f * (size_t) size; }
        };

        static const juce::StringArray& names();
        static const WavetableBank& get();

        int numTables() const { return (int) tables.size(); }
        const Level& level (int table, int lvl) const { return tables[(size_t) table][(size_t) lvl]; }

        // Picks the mip level for the phase increment (cycles per sample).
        static int levelForIncrement (float inc)
        {
            const float x = 1024.0f * inc * 1.43f;
            if (x <= 1.0f) return 0;
            return std::min (kLevels - 1, (int) std::ceil (std::log2 (x)));
        }

        // Single value for preview graphics (level 0)
        float sampleForDisplay (int table, float position, float phase) const;

    private:
        WavetableBank();
        std::vector<std::array<Level, kLevels>> tables;
    };
}
