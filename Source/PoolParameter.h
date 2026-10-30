#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

// A generic automatable parameter whose display name follows the mapping it
// carries ("Param 3" becomes "Cutoff"). Internal to the processor.
class PoolParameter : public juce::AudioParameterFloat
{
public:
    explicit PoolParameter (int slotIndex)
        : juce::AudioParameterFloat (juce::ParameterID ("pool" + juce::String (slotIndex + 1).paddedLeft ('0', 3), 1),
                                     "Param " + juce::String (slotIndex + 1),
                                     juce::NormalisableRange<float> (0.0f, 1.0f),
                                     0.0f,
                                     juce::AudioParameterFloatAttributes()
                                         .withStringFromValueFunction ([] (float v, int) { return juce::String (v, 2); })),
          defaultName ("Param " + juce::String (slotIndex + 1))
    {
    }

    juce::String getName (int maximumStringLength) const override
    {
        const juce::ScopedLock sl (nameLock);
        return (mappedName.isEmpty() ? defaultName : mappedName).substring (0, maximumStringLength);
    }

    void setMappedName (const juce::String& newName)
    {
        const juce::ScopedLock sl (nameLock);
        mappedName = newName;
    }

private:
    mutable juce::CriticalSection nameLock;
    juce::String defaultName, mappedName;
};
