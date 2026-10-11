#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "BinaryData.h"
#include "../fx/FxParams.h"

namespace fjord
{
    // Presets for a single effect module (.fjordmod): only the parameters of that one effect.
    //   <FjordFxModulePresets effect="reverb"><Preset name="Small Room" category="Room"><Param id="size" value="0.25"/>…
    // Factory presets are embedded (presets/FxModules/<effect>.fjordmod), user presets live in
    // ~/Music/Fjord/FX Module Presets/<effect>/<name>.fjordmod. Parameters a preset leaves out get their default.
    class FxModulePresets
    {
    public:
        struct Preset
        {
            juce::String name, category;
            std::map<juce::String, float> values;   // short IDs ("size", "mix" …)
            juce::File file;                         // empty for factory presets
        };

        static FxModulePresets& get()
        {
            static FxModulePresets instance;
            return instance;
        }

        const std::vector<Preset>& getPresets (int fxType)
        {
            auto& list = presets[(size_t) fxType];
            if (! loaded[(size_t) fxType]) load (fxType);
            return list;
        }

        static juce::File getUserFolder (int fxType)
        {
            return juce::File::getSpecialLocation (juce::File::userMusicDirectory)
                .getChildFile ("Fjord/FX Module Presets").getChildFile (fx::fxIds()[fxType]);
        }

        // Applies a preset to one effect and switches the effect on.
        static void apply (juce::AudioProcessorValueTreeState& state, int fxType, const Preset& preset)
        {
            const auto prefix = "fx_" + fx::fxIds()[fxType] + "_";
            for (auto* p : state.processor.getParameters())
                if (auto* param = dynamic_cast<juce::RangedAudioParameter*> (p))
                {
                    const auto id = param->getParameterID();
                    if (! id.startsWith (prefix))
                        continue;
                    const auto shortId = id.substring (prefix.length());
                    const auto it = preset.values.find (shortId);
                    const float value = shortId == "on" ? 1.0f
                                      : (it != preset.values.end() ? param->convertTo0to1 (it->second) : param->getDefaultValue());
                    param->beginChangeGesture();
                    param->setValueNotifyingHost (value);
                    param->endChangeGesture();
                }
        }

        // Do the current settings of the effect equal this preset (the on switch is ignored)?
        static bool matches (juce::AudioProcessorValueTreeState& state, int fxType, const Preset& preset)
        {
            const auto prefix = "fx_" + fx::fxIds()[fxType] + "_";
            for (auto* p : state.processor.getParameters())
                if (auto* param = dynamic_cast<juce::RangedAudioParameter*> (p))
                {
                    const auto id = param->getParameterID();
                    if (! id.startsWith (prefix) || id.endsWith ("_on"))
                        continue;
                    const auto it = preset.values.find (id.substring (prefix.length()));
                    const float target = it != preset.values.end() ? param->convertTo0to1 (it->second) : param->getDefaultValue();
                    if (std::abs (param->getValue() - target) > 1.0e-3f)
                        return false;
                }
            return true;
        }

        // Saves the current settings of one effect as a user preset.
        bool saveUserPreset (juce::AudioProcessorValueTreeState& state, int fxType, const juce::String& name)
        {
            const auto clean = name.trim().isEmpty() ? juce::String ("My Preset") : name.trim();
            const auto effect = fx::fxIds()[fxType];
            const auto prefix = "fx_" + effect + "_";

            juce::XmlElement root ("FjordFxModulePresets");
            root.setAttribute ("effect", effect);
            auto* preset = root.createNewChildElement ("Preset");
            preset->setAttribute ("name", clean);
            preset->setAttribute ("category", "User");
            for (auto* p : state.processor.getParameters())
                if (auto* param = dynamic_cast<juce::RangedAudioParameter*> (p))
                    if (param->getParameterID().startsWith (prefix) && ! param->getParameterID().endsWith ("_on"))
                    {
                        auto* e = preset->createNewChildElement ("Param");
                        e->setAttribute ("id", param->getParameterID().substring (prefix.length()));
                        e->setAttribute ("value", param->convertFrom0to1 (param->getValue()));
                    }

            auto folder = getUserFolder (fxType);
            if (! folder.createDirectory() || ! root.writeTo (folder.getChildFile (juce::File::createLegalFileName (clean) + ".fjordmod")))
                return false;
            loaded[(size_t) fxType] = false;   // reload on next access
            return true;
        }

    private:
        void load (int fxType)
        {
            auto& list = presets[(size_t) fxType];
            list.clear();
            const auto fileName = fx::fxIds()[fxType] + ".fjordmod";
            for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
                if (juce::String (BinaryData::originalFilenames[i]) == fileName)
                {
                    int size = 0;
                    if (auto* data = BinaryData::getNamedResource (BinaryData::namedResourceList[i], size))
                        parse (juce::String::fromUTF8 (data, size), {}, list);
                }

            for (const auto& f : getUserFolder (fxType).findChildFiles (juce::File::findFiles, false, "*.fjordmod"))
                parse (f.loadFileAsString(), f, list);
            loaded[(size_t) fxType] = true;
        }

        static void parse (const juce::String& text, const juce::File& file, std::vector<Preset>& out)
        {
            auto xml = juce::parseXML (text);
            if (xml == nullptr) return;
            for (auto* e : xml->getChildWithTagNameIterator ("Preset"))
            {
                Preset p;
                p.name = e->getStringAttribute ("name", "Untitled");
                p.category = file == juce::File() ? e->getStringAttribute ("category", "Factory") : juce::String ("User");
                p.file = file;
                for (auto* v : e->getChildWithTagNameIterator ("Param"))
                    p.values[v->getStringAttribute ("id")] = (float) v->getDoubleAttribute ("value");
                out.push_back (std::move (p));
            }
        }

        std::array<std::vector<Preset>, fx::kNumFx> presets;
        std::array<bool, fx::kNumFx> loaded {};
    };
}
