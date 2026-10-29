#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// True if every whitespace-separated term appears in the plugin's name,
// manufacturer or format (case-insensitive). An empty term list matches all.
inline bool pluginMatchesSearch (const juce::PluginDescription& d, const juce::StringArray& terms)
{
    for (const auto& term : terms)
    {
        const bool hit = d.name.containsIgnoreCase (term)
                      || d.manufacturerName.containsIgnoreCase (term)
                      || d.pluginFormatName.containsIgnoreCase (term);

        if (! hit)
            return false;
    }

    return true;
}

inline juce::StringArray splitSearchTerms (const juce::String& text)
{
    return juce::StringArray::fromTokens (text.trim(), " \t", "");
}
