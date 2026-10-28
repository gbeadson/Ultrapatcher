#pragma once

#include "PluginProcessor.h"

class HostedPluginWindow;

// Milestone 1 UI: scan, pick a plugin, load it, open its GUI in a floating window.
class UltrapatcherEditor : public juce::AudioProcessorEditor,
                           private juce::ChangeListener
{
public:
    explicit UltrapatcherEditor (UltrapatcherProcessor&);
    ~UltrapatcherEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void refresh();
    void openHostedEditor();

    UltrapatcherProcessor& proc;

    juce::TextButton scanButton   { "Scan plugins" };
    juce::TextButton loadButton   { "Load" };
    juce::TextButton unloadButton { "Unload" };
    juce::TextButton openButton   { "Open plugin window" };
    juce::ComboBox   pluginBox;
    juce::Label      statusLabel;

    juce::Array<juce::PluginDescription> types;
    std::unique_ptr<HostedPluginWindow> pluginWindow;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UltrapatcherEditor)
};
