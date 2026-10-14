#pragma once
#include <fjord_base/presets/PresetManagerBase.h>

namespace fjord
{
    // Presets are XML files (.fjord) with plain values:
    //   <FjordPreset name="Soft Glass" category="Pad" author="Fjord" tags="..." macros="..." fxOrder="..."><Param id="filter_cutoff" value="2400"/>…
    // Missing parameters get their default value – this keeps old presets compatible with new versions.
    // Locked parts (Locks.h: oscillators, effects, rhythm …) keep their values when switching presets.
    class PresetManager : public fjord::base::presets::PresetManagerBase
    {
    public:
        PresetManager (juce::AudioProcessorValueTreeState&, juce::UndoManager* = nullptr);
    };
}
