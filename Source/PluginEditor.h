#pragma once

#include "PluginProcessor.h"

class HostedPluginWindow;
class SettingsWindow;

// Main UI: toolbar, searchable plugin list, status line, Surface (one knob),
// settings window, right-click menu and VST3 drag and drop.
class UltrapatcherEditor : public juce::AudioProcessorEditor,
                           public juce::FileDragAndDropTarget,
                           private juce::ChangeListener,
                           private juce::ListBoxModel
{
public:
    explicit UltrapatcherEditor (UltrapatcherProcessor&);
    ~UltrapatcherEditor() override;

    // Opens a file chooser and loads the chosen .vst3. Also reachable from
    // the right-click menu.
    void chooseAndLoadPluginFile();

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

    // FileDragAndDropTarget
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray& files, int x, int y) override;
    void fileDragExit (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

private:
    static constexpr int kSurfaceSlot = 0; // the knob is bound to pool slot 1

    // ListBoxModel
    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void selectedRowsChanged (int lastRowSelected) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    void backgroundClicked (const juce::MouseEvent&) override;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void refresh();
    void applyFilter();
    void refreshMapBox();
    void updateStatus();
    juce::String getSelectedIdentifier() const;
    void loadSelected();
    void showContextMenu();
    void openSettings();
    void openHostedEditor();

    UltrapatcherProcessor& proc;

    // Toolbar
    juce::TextButton scanButton     { "Scan" };
    juce::TextButton loadButton     { "Load" };
    juce::TextButton unloadButton   { "Unload" };
    juce::TextButton openButton     { "Open" };
    juce::TextButton pinButton      { "Pin" };
    juce::TextButton settingsButton { "Settings" };

    // Plugin list
    juce::TextEditor searchBox;
    juce::ListBox    listBox { "plugins", this };
    juce::Label      statusLabel;

    // Surface (milestone 2: a single knob)
    juce::Label      surfaceTitle;
    juce::Slider     surfaceKnob;
    juce::Label      knobLabel;
    juce::ComboBox   mapBox;
    std::unique_ptr<juce::SliderParameterAttachment> knobAttachment;

    juce::Array<juce::PluginDescription> allTypes, shownTypes;
    std::unique_ptr<HostedPluginWindow> pluginWindow;
    std::unique_ptr<SettingsWindow> settingsWindow;
    std::unique_ptr<juce::FileChooser> fileChooser;
    int mapBoxGeneration = -1; // which hosted plugin mapBox was built for
    bool dragHover = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UltrapatcherEditor)
};
