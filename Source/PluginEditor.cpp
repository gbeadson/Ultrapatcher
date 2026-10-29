#include "PluginEditor.h"
#include "PluginSearch.h"

static juce::Font makeFont (float height, int styleFlags = juce::Font::plain)
{
    return juce::Font (juce::FontOptions (height, styleFlags));
}

//==============================================================================
// Floating window that owns a hosted plugin's editor component.
class HostedPluginWindow : public juce::DocumentWindow
{
public:
    HostedPluginWindow (juce::AudioProcessorEditor* editor, const juce::String& name,
                        bool alwaysOnTop, std::function<void()> onClose)
        : juce::DocumentWindow (name, juce::Colours::darkgrey, juce::DocumentWindow::closeButton),
          onCloseFn (std::move (onClose))
    {
        setUsingNativeTitleBar (true);
        setContentOwned (editor, true);
        setResizable (editor->isResizable(), false);
        centreWithSize (getWidth(), getHeight());
        setAlwaysOnTop (alwaysOnTop);
        setVisible (true);
    }

    void closeButtonPressed() override
    {
        if (onCloseFn)
            onCloseFn();
    }

private:
    std::function<void()> onCloseFn;
};

//==============================================================================
// Settings: custom scan folders, Scan Now, and the pin-window option.
class SettingsComponent : public juce::Component,
                          private juce::ListBoxModel,
                          private juce::ChangeListener
{
public:
    explicit SettingsComponent (UltrapatcherProcessor& p) : proc (p)
    {
        for (auto* c : std::initializer_list<juce::Component*> { &foldersTitle, &folderList, &addButton,
                                                                  &removeButton, &scanNowButton, &hint,
                                                                  &windowTitle, &pinToggle })
            addAndMakeVisible (c);

        foldersTitle.setText ("Custom scan folders", juce::dontSendNotification);
        foldersTitle.setFont (makeFont (15.0f, juce::Font::bold));
        windowTitle.setText ("Plugin window", juce::dontSendNotification);
        windowTitle.setFont (makeFont (15.0f, juce::Font::bold));

        hint.setText ("Scan in the main window covers the default plugin folders and these folders. "
                      "Scan Now covers only these folders.", juce::dontSendNotification);
        hint.setFont (makeFont (12.5f));
        hint.setJustificationType (juce::Justification::topLeft);

        folderList.setModel (this);
        folderList.setRowHeight (24);
        folderList.setOutlineThickness (1);

        addButton.onClick = [this] { chooseFolder(); };

        removeButton.onClick = [this]
        {
            const int row = folderList.getSelectedRow();

            if (row >= 0)
                proc.removeScanFolder (row);
        };

        scanNowButton.onClick = [this] { proc.startScan (true); };
        pinToggle.onClick = [this] { proc.setWindowAlwaysOnTop (pinToggle.getToggleState()); };

        proc.addChangeListener (this);
        refresh();
        setSize (500, 380);
    }

    ~SettingsComponent() override { proc.removeChangeListener (this); }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (14);

        foldersTitle.setBounds (area.removeFromTop (22));
        area.removeFromTop (4);

        auto bottom = area.removeFromBottom (134);
        folderList.setBounds (area.withTrimmedBottom (8));

        auto row = bottom.removeFromTop (28);
        addButton.setBounds (row.removeFromLeft (110));
        row.removeFromLeft (8);
        removeButton.setBounds (row.removeFromLeft (90));
        row.removeFromLeft (8);
        scanNowButton.setBounds (row.removeFromLeft (100));

        bottom.removeFromTop (8);
        hint.setBounds (bottom.removeFromTop (36));
        bottom.removeFromTop (10);
        windowTitle.setBounds (bottom.removeFromTop (22));
        bottom.removeFromTop (4);
        pinToggle.setBounds (bottom.removeFromTop (26));
    }

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }

    void refresh()
    {
        folders = proc.getScanFolders();
        folderList.updateContent();
        folderList.repaint();
        removeButton.setEnabled (folderList.getSelectedRow() >= 0);
        scanNowButton.setEnabled (! proc.isScanning() && folders.size() > 0);
        pinToggle.setToggleState (proc.isWindowAlwaysOnTop(), juce::dontSendNotification);
    }

    void chooseFolder()
    {
        chooser = std::make_unique<juce::FileChooser> ("Choose a folder to scan for plugins", juce::File());

        juce::Component::SafePointer<SettingsComponent> safeThis (this);

        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [safeThis] (const juce::FileChooser& fc)
                              {
                                  const auto result = fc.getResult();

                                  if (safeThis != nullptr && result.isDirectory())
                                      safeThis->proc.addScanFolder (result);
                              });
    }

    // ListBoxModel
    int getNumRows() override { return folders.size(); }

    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override
    {
        if (selected)
            g.fillAll (getLookAndFeel().findColour (juce::TextEditor::highlightColourId).withAlpha (0.6f));

        if (juce::isPositiveAndBelow (row, folders.size()))
        {
            g.setColour (getLookAndFeel().findColour (juce::ListBox::textColourId));
            g.setFont (makeFont (14.0f));
            g.drawText (folders[row], 8, 0, width - 16, height, juce::Justification::centredLeft, true);
        }
    }

    void selectedRowsChanged (int) override { removeButton.setEnabled (folderList.getSelectedRow() >= 0); }

    UltrapatcherProcessor& proc;
    juce::StringArray folders;

    juce::Label foldersTitle, hint, windowTitle;
    juce::ListBox folderList { "folders", nullptr };
    juce::TextButton addButton { "Add Folder..." };
    juce::TextButton removeButton { "Remove" };
    juce::TextButton scanNowButton { "Scan Now" };
    juce::ToggleButton pinToggle { "Keep plugin window on top (pinned)" };
    std::unique_ptr<juce::FileChooser> chooser;
};

class SettingsWindow : public juce::DocumentWindow
{
public:
    SettingsWindow (UltrapatcherProcessor& p, std::function<void()> onClose)
        : juce::DocumentWindow ("Ultrapatcher Settings", juce::Colours::darkgrey, juce::DocumentWindow::closeButton),
          onCloseFn (std::move (onClose))
    {
        setUsingNativeTitleBar (true);
        setContentOwned (new SettingsComponent (p), true);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
    }

    void closeButtonPressed() override
    {
        if (onCloseFn)
            onCloseFn();
    }

private:
    std::function<void()> onCloseFn;
};

//==============================================================================
static bool isVst3Path (const juce::String& path)
{
    return path.trimCharactersAtEnd ("/\\").endsWithIgnoreCase (".vst3");
}

UltrapatcherEditor::UltrapatcherEditor (UltrapatcherProcessor& p)
    : juce::AudioProcessorEditor (&p), proc (p)
{
    for (auto* c : std::initializer_list<juce::Component*> { &scanButton, &loadButton, &unloadButton,
                                                              &openButton, &pinButton, &settingsButton,
                                                              &searchBox, &listBox, &statusLabel,
                                                              &surfaceTitle, &surfaceKnob, &knobLabel, &mapBox })
        addAndMakeVisible (c);

    // Toolbar
    pinButton.setClickingTogglesState (true);

    scanButton.onClick     = [this] { proc.startScan(); };
    loadButton.onClick     = [this] { loadSelected(); };
    unloadButton.onClick   = [this] { proc.unloadPlugin(); };
    openButton.onClick     = [this] { openHostedEditor(); };
    pinButton.onClick      = [this] { proc.setWindowAlwaysOnTop (pinButton.getToggleState()); };
    settingsButton.onClick = [this] { openSettings(); };

    // Search and list
    searchBox.setTextToShowWhenEmpty ("Search plugins...", juce::Colours::grey);
    searchBox.setFont (makeFont (15.0f));
    searchBox.onTextChange = [this] { applyFilter(); };
    searchBox.onEscapeKey  = [this] { searchBox.clear(); };

    listBox.setRowHeight (26);
    listBox.setOutlineThickness (1);

    statusLabel.setJustificationType (juce::Justification::centredLeft);

    // Surface (a single knob for now)
    surfaceTitle.setText ("Surface", juce::dontSendNotification);
    surfaceTitle.setFont (makeFont (15.0f, juce::Font::bold));

    surfaceKnob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    surfaceKnob.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 18);
    surfaceKnob.setNumDecimalPlacesToDisplay (2);

    // The knob binds to a pool slot, never to the hosted parameter directly.
    knobAttachment = std::make_unique<juce::SliderParameterAttachment> (*proc.getPoolParameter (kSurfaceSlot),
                                                                         surfaceKnob, nullptr);

    knobLabel.setJustificationType (juce::Justification::centredLeft);

    mapBox.onChange = [this]
    {
        const int id = mapBox.getSelectedId();

        if (id == 1)
            proc.unmapSlot (kSurfaceSlot);
        else if (id > 1)
            proc.mapSlot (kSurfaceSlot, id - 2);
    };

    proc.addChangeListener (this);
    proc.knownPlugins.addChangeListener (this);

    refresh();
    setSize (560, 450);
}

UltrapatcherEditor::~UltrapatcherEditor()
{
    proc.removeChangeListener (this);
    proc.knownPlugins.removeChangeListener (this);
    pluginWindow.reset();   // hosted editor must die before anything else
    settingsWindow.reset();
}

//==============================================================================
void UltrapatcherEditor::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
}

void UltrapatcherEditor::paintOverChildren (juce::Graphics& g)
{
    if (! dragHover)
        return;

    auto r = getLocalBounds().toFloat().reduced (3.0f);
    g.setColour (juce::Colours::dodgerblue.withAlpha (0.18f));
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (juce::Colours::dodgerblue);
    g.drawRoundedRectangle (r, 8.0f, 2.5f);
    g.setColour (juce::Colours::white);
    g.setFont (makeFont (20.0f, juce::Font::bold));
    g.drawText ("Drop a VST3 plugin to load it", getLocalBounds(), juce::Justification::centred);
}

void UltrapatcherEditor::resized()
{
    auto area = getLocalBounds().reduced (12);

    auto bar = area.removeFromTop (30);
    settingsButton.setBounds (bar.removeFromRight (90));
    scanButton.setBounds   (bar.removeFromLeft (80));  bar.removeFromLeft (6);
    loadButton.setBounds   (bar.removeFromLeft (70));  bar.removeFromLeft (6);
    unloadButton.setBounds (bar.removeFromLeft (70));  bar.removeFromLeft (6);
    openButton.setBounds   (bar.removeFromLeft (70));  bar.removeFromLeft (6);
    pinButton.setBounds    (bar.removeFromLeft (60));

    area.removeFromTop (8);
    searchBox.setBounds (area.removeFromTop (28));

    area.removeFromTop (8);
    auto surfaceArea = area.removeFromBottom (110 + 4 + 22 + 14 + 22 + 6);
    listBox.setBounds (area.withTrimmedBottom (6));

    statusLabel.setBounds (surfaceArea.removeFromTop (22));
    surfaceArea.removeFromTop (14);
    surfaceTitle.setBounds (surfaceArea.removeFromTop (22));
    surfaceArea.removeFromTop (4);

    auto surfaceRow = surfaceArea.removeFromTop (110);
    surfaceKnob.setBounds (surfaceRow.removeFromLeft (100));
    surfaceRow.removeFromLeft (12);
    surfaceRow.removeFromTop (14);
    knobLabel.setBounds (surfaceRow.removeFromTop (24));
    surfaceRow.removeFromTop (6);
    mapBox.setBounds (surfaceRow.removeFromTop (28));
}

void UltrapatcherEditor::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        showContextMenu();
}

//==============================================================================
// Drag and drop: any .vst3 dropped on the window is loaded.
bool UltrapatcherEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (isVst3Path (f))
            return true;

    return false;
}

void UltrapatcherEditor::fileDragEnter (const juce::StringArray& files, int, int)
{
    dragHover = isInterestedInFileDrag (files);
    repaint();
}

void UltrapatcherEditor::fileDragExit (const juce::StringArray&)
{
    dragHover = false;
    repaint();
}

void UltrapatcherEditor::filesDropped (const juce::StringArray& files, int, int)
{
    dragHover = false;
    repaint();

    for (const auto& f : files)
    {
        if (isVst3Path (f))
        {
            proc.loadPluginFromFile (juce::File (f));
            return;
        }
    }
}

//==============================================================================
// Plugin list model.
int UltrapatcherEditor::getNumRows()
{
    return shownTypes.size();
}

void UltrapatcherEditor::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (selected)
        g.fillAll (getLookAndFeel().findColour (juce::TextEditor::highlightColourId).withAlpha (0.6f));

    if (! juce::isPositiveAndBelow (row, shownTypes.size()))
        return;

    const auto& d = shownTypes.getReference (row);
    const auto textColour = getLookAndFeel().findColour (juce::ListBox::textColourId);

    auto area = juce::Rectangle<int> (0, 0, width, height).reduced (8, 0);
    auto right = area.removeFromRight (width / 2 - 8);

    g.setColour (textColour);
    g.setFont (makeFont (15.0f));
    g.drawText (d.name, area, juce::Justification::centredLeft, true);

    g.setColour (textColour.withAlpha (0.6f));
    g.setFont (makeFont (13.0f));
    g.drawText (d.manufacturerName + "   |   " + d.pluginFormatName, right,
                juce::Justification::centredRight, true);
}

void UltrapatcherEditor::selectedRowsChanged (int)
{
    loadButton.setEnabled (listBox.getSelectedRow() >= 0);
}

void UltrapatcherEditor::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    listBox.selectRow (row);
    loadSelected();
}

void UltrapatcherEditor::listBoxItemClicked (int, const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        showContextMenu();
}

void UltrapatcherEditor::backgroundClicked (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        showContextMenu();
}

//==============================================================================
void UltrapatcherEditor::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    // The hosted plugin is about to be replaced: destroy its GUI first.
    if (source == &proc && proc.isHostedChanging())
    {
        pluginWindow.reset();
        return;
    }

    refresh();
}

juce::String UltrapatcherEditor::getSelectedIdentifier() const
{
    const int row = listBox.getSelectedRow();

    return juce::isPositiveAndBelow (row, shownTypes.size()) ? shownTypes.getReference (row).createIdentifierString()
                                                             : juce::String();
}

void UltrapatcherEditor::refresh()
{
    allTypes = proc.knownPlugins.getTypes();

    std::sort (allTypes.begin(), allTypes.end(),
               [] (const juce::PluginDescription& a, const juce::PluginDescription& b)
               {
                   return a.name.compareIgnoreCase (b.name) < 0;
               });

    applyFilter();

    const bool hosting = proc.getHostedPlugin() != nullptr;

    scanButton.setEnabled (! proc.isScanning());
    unloadButton.setEnabled (hosting);
    openButton.setEnabled (hosting);
    pinButton.setToggleState (proc.isWindowAlwaysOnTop(), juce::dontSendNotification);

    if (pluginWindow != nullptr)
        pluginWindow->setAlwaysOnTop (proc.isWindowAlwaysOnTop());

    refreshMapBox();
    mapBox.setEnabled (hosting);

    const auto mappedName = proc.getSlotName (kSurfaceSlot);
    knobLabel.setText (mappedName.isNotEmpty() ? mappedName : "Not mapped", juce::dontSendNotification);

    updateStatus();
}

// Live filter: every search term must hit the name, manufacturer or format.
void UltrapatcherEditor::applyFilter()
{
    const auto selectedId = getSelectedIdentifier();
    const auto terms = splitSearchTerms (searchBox.getText());

    shownTypes.clear();

    for (const auto& d : allTypes)
        if (pluginMatchesSearch (d, terms))
            shownTypes.add (d);

    listBox.updateContent();

    int row = -1;

    if (selectedId.isNotEmpty())
        for (int i = 0; i < shownTypes.size(); ++i)
            if (shownTypes.getReference (i).createIdentifierString() == selectedId)
            {
                row = i;
                break;
            }

    if (row >= 0)
        listBox.selectRow (row, true);
    else
        listBox.deselectAllRows();

    listBox.repaint();
    loadButton.setEnabled (listBox.getSelectedRow() >= 0);
    updateStatus();
}

void UltrapatcherEditor::updateStatus()
{
    const int total = allTypes.size();
    const int shown = shownTypes.size();
    const bool filtering = searchBox.getText().trim().isNotEmpty();

    auto count = [] (int n) { return juce::String (n) + (n == 1 ? " plugin" : " plugins"); };

    juce::String text;
    bool isError = false;

    if (proc.isScanning())
    {
        text = "Scanning... " + count (total) + " found so far";
    }
    else if (proc.getLastError().isNotEmpty())
    {
        text = proc.getLastError();
        isError = true;
    }
    else
    {
        const bool hosting = proc.getHostedPlugin() != nullptr;

        text = (hosting ? "Hosting: " + proc.getHostedName() : juce::String ("No plugin loaded (passthrough)"))
             + "   |   "
             + (filtering ? juce::String (shown) + " of " + count (total) : count (total));
    }

    statusLabel.setText (text, juce::dontSendNotification);
    statusLabel.setColour (juce::Label::textColourId,
                           isError ? juce::Colours::orange
                                   : getLookAndFeel().findColour (juce::Label::textColourId));
}

// Rebuilds the parameter list only when the hosted plugin changes: some plugins
// expose thousands of parameters and refresh() runs on every state change.
void UltrapatcherEditor::refreshMapBox()
{
    const int generation = proc.getHostedGeneration();

    if (generation != mapBoxGeneration)
    {
        mapBoxGeneration = generation;
        mapBox.clear (juce::dontSendNotification);
        mapBox.addItem ("(not mapped)", 1);

        for (const auto& info : proc.getHostedParameterList())
            mapBox.addItem (info.name, info.index + 2);
    }

    const int mapped = proc.getSlotMapping (kSurfaceSlot);
    mapBox.setSelectedId (mapped >= 0 ? mapped + 2 : 1, juce::dontSendNotification);
}

//==============================================================================
void UltrapatcherEditor::loadSelected()
{
    const int row = listBox.getSelectedRow();

    if (juce::isPositiveAndBelow (row, shownTypes.size()))
        proc.loadPlugin (shownTypes.getReference (row));
}

void UltrapatcherEditor::chooseAndLoadPluginFile()
{
    fileChooser = std::make_unique<juce::FileChooser> ("Select a VST3 plugin", juce::File(), "*.vst3");

    juce::Component::SafePointer<UltrapatcherEditor> safeThis (this);

    // .vst3 is a single file on some platforms and a folder bundle on others.
    fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                  | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::canSelectDirectories,
                              [safeThis] (const juce::FileChooser& fc)
                              {
                                  const auto result = fc.getResult();

                                  if (safeThis != nullptr && result != juce::File())
                                      safeThis->proc.loadPluginFromFile (result);
                              });
}

void UltrapatcherEditor::showContextMenu()
{
    juce::PopupMenu menu;
    menu.addItem (1, "Load Plugin from File...");
    menu.addItem (2, "Scan plugins", ! proc.isScanning());
    menu.addItem (3, "Settings...");

    juce::Component::SafePointer<UltrapatcherEditor> safeThis (this);

    menu.showMenuAsync (juce::PopupMenu::Options(), [safeThis] (int result)
    {
        if (safeThis == nullptr)
            return;

        if (result == 1)      safeThis->chooseAndLoadPluginFile();
        else if (result == 2) safeThis->proc.startScan();
        else if (result == 3) safeThis->openSettings();
    });
}

void UltrapatcherEditor::openSettings()
{
    if (settingsWindow != nullptr)
    {
        settingsWindow->toFront (true);
        return;
    }

    juce::Component::SafePointer<UltrapatcherEditor> safeThis (this);

    settingsWindow = std::make_unique<SettingsWindow> (proc, [safeThis]
    {
        // Defer: this runs inside the window's own button handler.
        juce::MessageManager::callAsync ([safeThis]
        {
            if (safeThis != nullptr)
                safeThis->settingsWindow.reset();
        });
    });
}

void UltrapatcherEditor::openHostedEditor()
{
    if (pluginWindow != nullptr)
    {
        pluginWindow->toFront (true);
        return;
    }

    auto* plugin = proc.getHostedPlugin();

    if (plugin == nullptr || ! plugin->hasEditor())
        return;

    auto* hostedEditor = plugin->createEditorAndMakeActive();

    if (hostedEditor == nullptr)
        return;

    juce::Component::SafePointer<UltrapatcherEditor> safeThis (this);

    pluginWindow = std::make_unique<HostedPluginWindow> (hostedEditor, proc.getHostedName(),
                                                         proc.isWindowAlwaysOnTop(), [safeThis]
    {
        // Defer: this runs inside the window's own button handler.
        juce::MessageManager::callAsync ([safeThis]
        {
            if (safeThis != nullptr)
                safeThis->pluginWindow.reset();
        });
    });
}
