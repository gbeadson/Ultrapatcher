#pragma once

#include "PluginProcessor.h"
#include "PatchCanvas.h"
#include "SurfacePanel.h"

class HostedPluginWindow;
class SettingsWindow;

// Patcher mode UI: toolbar, searchable plugin list on the left, the patch
// canvas in the middle, the Surface along the bottom.
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
    void updateStatus();
    void updateButtons();
    juce::String getSelectedIdentifier() const;
    void addSelectedPlugin();
    void removeSelectedNode();
    void showContextMenu();
    void openSettings();
    void openNodeWindow (NodeUid);

    UltrapatcherProcessor& proc;

    // Toolbar
    juce::TextButton scanButton     { "Scan" };
    juce::TextButton addButton      { "Add" };
    juce::TextButton removeButton   { "Remove" };
    juce::TextButton openButton     { "Open" };
    juce::TextButton bypassButton   { "Bypass" };
    juce::TextButton pinButton      { "Pin" };
    juce::TextButton settingsButton { "Settings" };

    // Plugin list
    juce::TextEditor searchBox;
    juce::ListBox    listBox { "plugins", this };
    juce::Label      statusLabel;

    PatchCanvas  canvas;
    SurfacePanel surface;

    juce::Array<juce::PluginDescription> allTypes, shownTypes;
    std::map<NodeUid, std::unique_ptr<HostedPluginWindow>> pluginWindows;
    std::unique_ptr<SettingsWindow> settingsWindow;
    std::unique_ptr<juce::FileChooser> fileChooser;
    int lastPatchGeneration = -1;
    bool dragHover = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UltrapatcherEditor)
};
