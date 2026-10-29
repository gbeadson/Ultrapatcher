#pragma once

#include <juce_core/juce_core.h>

// User settings, persisted as settings.xml in the support directory.
// Message thread only.
struct UltrapatcherSettings
{
    juce::StringArray scanFolders;     // extra folders scanned for plugins
    bool windowAlwaysOnTop = false;    // keep the hosted plugin's window pinned on top

    void load (const juce::File& file)
    {
        auto xml = juce::parseXML (file);

        if (xml == nullptr || ! xml->hasTagName ("UltrapatcherSettings"))
            return;

        scanFolders.clear();

        if (auto* folders = xml->getChildByName ("SCAN_FOLDERS"))
            for (auto* e : folders->getChildWithTagNameIterator ("FOLDER"))
                if (auto path = e->getStringAttribute ("path"); path.isNotEmpty())
                    scanFolders.addIfNotAlreadyThere (path);

        windowAlwaysOnTop = xml->getBoolAttribute ("windowAlwaysOnTop", false);
    }

    bool save (const juce::File& file) const
    {
        juce::XmlElement xml ("UltrapatcherSettings");
        xml.setAttribute ("version", 1);
        xml.setAttribute ("windowAlwaysOnTop", windowAlwaysOnTop);

        auto* folders = xml.createNewChildElement ("SCAN_FOLDERS");

        for (const auto& path : scanFolders)
            folders->createNewChildElement ("FOLDER")->setAttribute ("path", path);

        return xml.writeTo (file);
    }
};
