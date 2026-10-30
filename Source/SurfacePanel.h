#pragma once

#include "PluginProcessor.h"

// The Surface: eight knobs, colour-coded in rainbow order (after Bitwig's soft
// controls). Each knob is a DAW-automatable pool parameter that can drive many
// plugin parameters at once.
//  - Learn, then move a parameter in a plugin window to map it.
//  - Double-click the name to rename. The "..." button edits mappings.
class SurfacePanel : public juce::Component
{
public:
    explicit SurfacePanel (UltrapatcherProcessor&);
    ~SurfacePanel() override;

    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class KnobCell;

    UltrapatcherProcessor& proc;
    juce::OwnedArray<KnobCell> cells;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SurfacePanel)
};
