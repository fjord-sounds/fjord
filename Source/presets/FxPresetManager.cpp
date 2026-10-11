#include "FxPresetManager.h"
#include "BinaryData.h"

namespace fjord
{
    FxPresetManager::FxPresetManager (juce::AudioProcessorValueTreeState& s) : state (s)
    {
        rescan();
    }

    juce::File FxPresetManager::getUserFolder()
    {
        return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Fjord/FX Presets");
    }

    bool FxPresetManager::parseHeader (const juce::String& text, Preset& out)
    {
        const int start = text.indexOf ("<FjordFxPreset");
        const int end = start >= 0 ? text.indexOfChar (start, '>') : -1;
        if (start < 0 || end < 0)
            return false;
        auto header = text.substring (start, end);
        if (header.endsWithChar ('/')) header = header.dropLastCharacters (1);
        auto xml = juce::parseXML (header + "/>");
        if (xml == nullptr)
            return false;
        out.name = xml->getStringAttribute ("name", "Untitled");
        out.category = xml->getStringAttribute ("category", "User");
        out.xml = text;
        return true;
    }

    void FxPresetManager::rescan()
    {
        presets.clear();
        for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
        {
            if (! juce::String (BinaryData::originalFilenames[i]).endsWithIgnoreCase (".fjordfx"))
                continue;
            int size = 0;
            if (auto* data = BinaryData::getNamedResource (BinaryData::namedResourceList[i], size))
            {
                Preset p;
                if (parseHeader (juce::String::fromUTF8 (data, size), p))
                    presets.push_back (std::move (p));
            }
        }

        for (const auto& f : getUserFolder().findChildFiles (juce::File::findFiles, false, "*.fjordfx"))
        {
            Preset p;
            if (parseHeader (f.loadFileAsString(), p))
            {
                p.file = f;
                p.isFactory = false;
                p.category = "User";
                presets.push_back (std::move (p));
            }
        }

        auto rank = [] (const juce::String& c) { const int i = categoryOrder.indexOf (c); return i < 0 ? 99 : i; };
        std::stable_sort (presets.begin(), presets.end(), [&] (const Preset& a, const Preset& b)
        {
            if (rank (a.category) != rank (b.category)) return rank (a.category) < rank (b.category);
            return a.name.compareNatural (b.name) < 0;
        });
        sendChangeMessage();
    }

    int FxPresetManager::getCurrentIndex() const
    {
        const auto name = getCurrentName();
        for (size_t i = 0; i < presets.size(); ++i)
            if (presets[i].name == name) return (int) i;
        return -1;
    }

    void FxPresetManager::load (int index)
    {
        if (! juce::isPositiveAndBelow (index, (int) presets.size()))
            return;
        auto xml = juce::parseXML (presets[(size_t) index].xml);
        if (xml == nullptr)
            return;

        std::map<juce::String, float> values;
        for (auto* e : xml->getChildWithTagNameIterator ("Param"))
            values[e->getStringAttribute ("id")] = (float) e->getDoubleAttribute ("value");

        for (auto* p : state.processor.getParameters())
            if (auto* param = dynamic_cast<juce::RangedAudioParameter*> (p))
                if (isFxParameter (param->getParameterID()))
                {
                    const auto it = values.find (param->getParameterID());
                    param->beginChangeGesture();
                    param->setValueNotifyingHost (it != values.end() ? param->convertTo0to1 (it->second) : param->getDefaultValue());
                    param->endChangeGesture();
                }

        state.state.setProperty ("fxOrder", xml->getStringAttribute ("fxOrder"), nullptr);
        state.state.setProperty (nameProp, presets[(size_t) index].name, nullptr);
        sendChangeMessage();
    }

    void FxPresetManager::step (int delta)
    {
        if (presets.empty()) return;
        const int n = (int) presets.size();
        const int cur = getCurrentIndex();
        load (((cur < 0 ? (delta > 0 ? -1 : 0) : cur) + delta + n) % n);
    }

    bool FxPresetManager::saveUserPreset (const juce::String& name)
    {
        const auto clean = name.trim().isEmpty() ? juce::String ("My FX") : name.trim();
        juce::XmlElement xml ("FjordFxPreset");
        xml.setAttribute ("name", clean);
        xml.setAttribute ("category", "User");
        xml.setAttribute ("fxOrder", state.state.getProperty ("fxOrder").toString());
        for (auto* p : state.processor.getParameters())
            if (auto* param = dynamic_cast<juce::RangedAudioParameter*> (p))
                if (isFxParameter (param->getParameterID()))
                {
                    auto* e = xml.createNewChildElement ("Param");
                    e->setAttribute ("id", param->getParameterID());
                    e->setAttribute ("value", param->convertFrom0to1 (param->getValue()));
                }

        auto folder = getUserFolder();
        if (! folder.createDirectory()) return false;
        if (! xml.writeTo (folder.getChildFile (juce::File::createLegalFileName (clean) + ".fjordfx"))) return false;
        state.state.setProperty (nameProp, clean, nullptr);
        rescan();
        return true;
    }

    void FxPresetManager::setLocked (bool locked)
    {
        state.state.setProperty (lockProp, locked, nullptr);
        sendChangeMessage();
    }
}
