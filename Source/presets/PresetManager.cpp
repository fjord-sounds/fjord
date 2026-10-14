#include "PresetManager.h"
#include "Locks.h"

namespace fjord
{
    PresetManager::PresetManager (juce::AudioProcessorValueTreeState& s, juce::UndoManager* um)
        : PresetManagerBase (s, um, { ".fjord", "FjordPreset", false, {}, { "master_gain" } })
    {
        setLocks (locks::set());   // locked parts (Locks.h) keep their values when a preset is loaded
        initialise();
    }
}
