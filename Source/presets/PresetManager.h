#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace fjord
{
    // Presets are XML files (.fjord) with plain values:
    //   <FjordPreset name="Soft Glass" category="Pad" author="Fjord"><Param id="filter_cutoff" value="2400"/>…
    // Missing parameters get their default value – this keeps old presets compatible with new versions.
    class PresetManager : public juce::ChangeBroadcaster
    {
    public:
        struct Preset
        {
            juce::String name, category, author;
            juce::StringArray tags;
            juce::String xml;          // file contents
            juce::File file;           // empty for factory presets
            bool isFactory = true;
        };

        // Tag vocabulary in display order (must match tools/preset-gen/tags.py)
        static inline const juce::StringArray tagOrder {
            "Warm", "Bright", "Dark", "Soft", "Airy", "Glassy", "Vocal", "Bell", "Gritty", "Acid", "Supersaw",
            "Evolving", "Pulsing", "Arpeggiated", "Melody", "Gated", "Pumping", "Stutter",
            "Wide", "Spacey", "Echo", "Dry",
            "Short", "Slow Attack", "Long Tail", "Mono", "Glide", "Sub", "Layered", "Chord", "Velocity",
            "Disco", "House", "Bedroom", "Retro", "Lo-Fi", "Ambient" };

        static inline const juce::StringArray categoryOrder { "Pad", "Atmos", "Lead", "Pluck", "Keys", "Bass", "Arp",
                                                               "Gated", "Drums", "FX", "User" };

        explicit PresetManager (juce::AudioProcessorValueTreeState&);

        void rescan();
        const std::vector<Preset>& getPresets() const { return presets; }

        int getCurrentIndex() const { return currentIndex; }
        juce::String getCurrentName() const;
        juce::String getCurrentCategory() const;

        void load (int index);
        void loadInit();
        void next()     { step (1); }
        void previous() { step (-1); }

        // Saves the current sound as a user preset and selects it.
        bool saveUserPreset (const juce::String& name, const juce::String& category, const juce::StringArray& tags = {});

        // Tags of the active preset (empty for Init)
        juce::StringArray getCurrentTags() const;

        static juce::File getUserPresetFolder();

        // Favorites (★), stored in favorites.txt in the user folder
        bool isFavorite (int index) const;
        void toggleFavorite (int index);

        // Only user presets can be deleted.
        bool deleteUserPreset (int index);

        juce::ValueTree& getState() { return state.state; }

        // For host recall: keep the name of the active preset in the state.
        void restoreFromState();

    private:
        void step (int delta);
        void applyXml (const juce::XmlElement&);
        void setCurrent (int index, const juce::String& name, const juce::String& category);
        static bool parse (const juce::String& xmlText, Preset& out);
        static juce::String favoriteKey (const Preset& p) { return (p.isFactory ? "factory/" : "user/") + p.category + "/" + p.name; }
        void loadFavorites();
        void saveFavorites() const;

        juce::AudioProcessorValueTreeState& state;
        std::vector<Preset> presets;
        int currentIndex = -1;
        juce::StringArray favorites;
    };
}
