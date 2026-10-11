#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace fjord
{
    // Effect presets (.fjordfx): only the effect chain (all "fx_" parameters + order).
    // They can be put on any sound. With "Lock FX" active the chain is kept when switching
    // sound presets.
    class FxPresetManager : public juce::ChangeBroadcaster
    {
    public:
        struct Preset
        {
            juce::String name, category, xml;
            juce::File file;
            bool isFactory = true;
        };

        static inline const juce::StringArray categoryOrder { "Clean", "Space", "Echo", "Modulation", "Character", "Lo-Fi", "User" };
        static inline const juce::Identifier nameProp { "fxPresetName" }, lockProp { "fxLock" };

        explicit FxPresetManager (juce::AudioProcessorValueTreeState&);

        void rescan();
        const std::vector<Preset>& getPresets() const { return presets; }

        void load (int index);
        void next()     { step (1); }
        void previous() { step (-1); }
        bool saveUserPreset (const juce::String& name);

        // Empty = the effects come from the current sound preset
        juce::String getCurrentName() const { return state.state.getProperty (nameProp).toString(); }
        int getCurrentIndex() const;

        bool isLocked() const { return state.state.getProperty (lockProp, false); }
        void setLocked (bool);

        static juce::File getUserFolder();
        static bool isFxParameter (const juce::String& id) { return id.startsWith ("fx_"); }

    private:
        void step (int delta);
        static bool parseHeader (const juce::String& text, Preset& out);

        juce::AudioProcessorValueTreeState& state;
        std::vector<Preset> presets;
    };
}
