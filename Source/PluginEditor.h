#pragma once

#include "PluginProcessor.h"

class HostedPluginWindow;

// Milestone 2 UI: scan, pick a plugin, load it, open its GUI in a floating
// window, and drive one hosted parameter from a Surface knob.
class UltrapatcherEditor : public juce::AudioProcessorEditor,
                           private juce::ChangeListener
{
public:
    explicit UltrapatcherEditor (UltrapatcherProcessor&);
    ~UltrapatcherEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    static constexpr int kSurfaceSlot = 0; // the knob is bound to pool slot 1

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void refresh();
    void refreshMapBox();
    void openHostedEditor();

    UltrapatcherProcessor& proc;

    juce::TextButton scanButton   { "Scan plugins" };
    juce::TextButton loadButton   { "Load" };
    juce::TextButton unloadButton { "Unload" };
    juce::TextButton openButton   { "Open plugin window" };
    juce::ComboBox   pluginBox;
    juce::Label      statusLabel;

    // Surface (milestone 2: a single knob)
    juce::Label      surfaceTitle;
    juce::Slider     surfaceKnob;
    juce::Label      knobLabel;
    juce::ComboBox   mapBox;
    std::unique_ptr<juce::SliderParameterAttachment> knobAttachment;

    juce::Array<juce::PluginDescription> types;
    std::unique_ptr<HostedPluginWindow> pluginWindow;
    int mapBoxGeneration = -1; // which hosted plugin mapBox was built for

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UltrapatcherEditor)
};
