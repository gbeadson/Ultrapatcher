#include "PluginEditor.h"
#include "PluginSearch.h"
#include "PoolParameter.h"

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
    : juce::AudioProcessorEditor (&p), proc (p), canvas (p), rackView (p), surface (p)
{
    for (auto* c : std::initializer_list<juce::Component*> { &patcherButton, &rackButton, &scanButton, &addButton,
                                                              &removeButton, &openButton, &bypassButton, &pinButton,
                                                              &settingsButton, &searchBox, &listBox, &statusLabel,
                                                              &canvas, &rackView, &surface })
        addAndMakeVisible (c);

    patcherButton.onClick = [this] { requestMode (RackMode::patcher); };
    rackButton.onClick    = [this] { requestMode (RackMode::rack); };

    // Toolbar
    bypassButton.setClickingTogglesState (false);
    pinButton.setClickingTogglesState (true);

    scanButton.onClick     = [this] { proc.startScan(); };
    addButton.onClick      = [this] { addSelectedPlugin(); };
    removeButton.onClick   = [this] { removeSelection(); };
    openButton.onClick     = [this] { for (auto uid : selectedPlugins()) openNodeWindow (uid); };
    pinButton.onClick      = [this] { proc.setWindowAlwaysOnTop (pinButton.getToggleState()); };
    settingsButton.onClick = [this] { openSettings(); };

    bypassButton.onClick = [this] { toggleBypassOnSelection(); };

    // Search and list
    searchBox.setTextToShowWhenEmpty ("Search plugins...", juce::Colours::grey);
    searchBox.setFont (makeFont (15.0f));
    searchBox.onTextChange = [this] { applyFilter(); };
    searchBox.onEscapeKey  = [this] { searchBox.clear(); };

    listBox.setRowHeight (36);
    listBox.setOutlineThickness (1);

    statusLabel.setJustificationType (juce::Justification::centredLeft);
    statusLabel.setMinimumHorizontalScale (1.0f);

    canvas.onSelectionChanged = [this] { updateButtons(); };
    canvas.onOpenNode         = [this] (NodeUid uid) { openNodeWindow (uid); };
    canvas.onContextMenu      = [this] { showContextMenu(); };

    rackView.onSelectionChanged = [this] { updateButtons(); };
    rackView.onOpenDevice       = [this] (NodeUid uid) { openNodeWindow (uid); };
    rackView.onContextMenu      = [this] { showContextMenu(); };
    rackView.onAddToChain       = [this] (int chainId)
    {
        proc.setActiveChain (chainId);
        addSelectedPlugin();
    };

    proc.addChangeListener (this);
    proc.knownPlugins.addChangeListener (this);

    setResizable (true, true);
    setResizeLimits (820, 560, 2000, 1400);
    setSize (1040, 680);
    refresh();
}

UltrapatcherEditor::~UltrapatcherEditor()
{
    proc.removeChangeListener (this);
    proc.knownPlugins.removeChangeListener (this);
    pluginWindows.clear();   // hosted editors must die before anything else
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
    g.drawText ("Drop a VST3 plugin to add it", getLocalBounds(), juce::Justification::centred);
}

void UltrapatcherEditor::resized()
{
    auto area = getLocalBounds().reduced (10);

    auto bar = area.removeFromTop (30);
    settingsButton.setBounds (bar.removeFromRight (90));
    patcherButton.setBounds (bar.removeFromLeft (80)); bar.removeFromLeft (4);
    rackButton.setBounds    (bar.removeFromLeft (64)); bar.removeFromLeft (18);
    scanButton.setBounds   (bar.removeFromLeft (80));  bar.removeFromLeft (6);
    addButton.setBounds    (bar.removeFromLeft (70));  bar.removeFromLeft (6);
    removeButton.setBounds (bar.removeFromLeft (80));  bar.removeFromLeft (6);
    openButton.setBounds   (bar.removeFromLeft (70));  bar.removeFromLeft (6);
    bypassButton.setBounds (bar.removeFromLeft (80));  bar.removeFromLeft (6);
    pinButton.setBounds    (bar.removeFromLeft (60));

    area.removeFromTop (8);

    auto left = area.removeFromLeft (260);
    area.removeFromLeft (8);

    searchBox.setBounds (left.removeFromTop (28));
    left.removeFromTop (8);
    statusLabel.setBounds (left.removeFromBottom (44));
    left.removeFromBottom (6);
    listBox.setBounds (left);

    surface.setBounds (area.removeFromBottom (170));
    area.removeFromBottom (8);
    canvas.setBounds (area);
    rackView.setBounds (area);
}

void UltrapatcherEditor::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        showContextMenu();
}

//==============================================================================
// Drag and drop: any .vst3 dropped on the window is added to the patch.
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

    g.setColour (textColour);
    g.setFont (makeFont (14.5f));
    g.drawText (d.name, area.removeFromTop (height * 11 / 20), juce::Justification::bottomLeft, true);

    g.setColour (textColour.withAlpha (0.55f));
    g.setFont (makeFont (11.0f));
    g.drawText (d.manufacturerName + "  |  " + d.pluginFormatName, area, juce::Justification::topLeft, true);
}

void UltrapatcherEditor::selectedRowsChanged (int)
{
    updateButtons();
}

void UltrapatcherEditor::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    listBox.selectRow (row);
    addSelectedPlugin();
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
    // A node is about to be removed: destroy its plugin window first.
    if (source == &proc && proc.isStructureChanging())
    {
        const auto uid = proc.getChangingUid();

        rackView.releaseRows();   // rows hold parameter attachments into nodes about to be destroyed

        if (uid == 0)
            pluginWindows.clear();
        else
            pluginWindows.erase (uid);

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

    const bool rack = proc.getMode() == RackMode::rack;
    canvas.setVisible (! rack);
    rackView.setVisible (rack);
    patcherButton.setToggleState (! rack, juce::dontSendNotification);
    rackButton.setToggleState (rack, juce::dontSendNotification);

    canvas.refresh();
    rackView.refresh();
    surface.refresh();
    lastPatchGeneration = proc.getPatchGeneration();

    pinButton.setToggleState (proc.isWindowAlwaysOnTop(), juce::dontSendNotification);

    for (auto& entry : pluginWindows)
        entry.second->setAlwaysOnTop (proc.isWindowAlwaysOnTop());

    updateButtons();
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
    updateButtons();
    updateStatus();
}

std::vector<NodeUid> UltrapatcherEditor::selectedPlugins() const
{
    std::vector<NodeUid> list;

    if (proc.getMode() == RackMode::rack)
    {
        if (rackView.getSelectedDevice() != 0)
            list.push_back (rackView.getSelectedDevice());
    }
    else
    {
        for (auto uid : canvas.getSelectedNodes())
            if (uid != kInputUid && uid != kOutputUid)
                list.push_back (uid);
    }

    return list;
}

void UltrapatcherEditor::updateButtons()
{
    const auto selection = selectedPlugins();
    const bool any = ! selection.empty();

    scanButton.setEnabled (! proc.isScanning());
    addButton.setEnabled (listBox.getSelectedRow() >= 0);
    removeButton.setEnabled (any);
    openButton.setEnabled (any);
    bypassButton.setEnabled (any);

    bool allBypassed = any;

    for (const auto& info : proc.getPatchNodes())
        if (std::find (selection.begin(), selection.end(), info.uid) != selection.end())
            allBypassed = allBypassed && info.bypassed;

    bypassButton.setToggleState (allBypassed, juce::dontSendNotification);
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
    else if (proc.getLearnSlot() >= 0)
    {
        text = "Learning knob " + juce::String (proc.getLearnSlot() + 1) + ": move a parameter in a plugin window";
    }
    else
    {
        int inPatch = 0;

        for (const auto& info : proc.getPatchNodes())
            if (! info.isIO && info.subtitle != "Chain mixer")
                ++inPatch;

        const auto chainCount = (int) proc.getChains().size();
        const auto where = proc.getMode() == RackMode::rack
                             ? juce::String (chainCount) + (chainCount == 1 ? " chain, " : " chains, ")
                                 + juce::String (inPatch) + (inPatch == 1 ? " device" : " devices")
                             : (inPatch > 0 ? juce::String (inPatch) + (inPatch == 1 ? " plugin" : " plugins") + " in patch"
                                            : juce::String ("Patch is empty (passthrough)"));

        text = where
             + "\n"
             + (filtering ? juce::String (shown) + " of " + count (total) : count (total)) + " known";
    }

    statusLabel.setText (text, juce::dontSendNotification);
    statusLabel.setColour (juce::Label::textColourId,
                           isError ? juce::Colours::orange
                                   : getLookAndFeel().findColour (juce::Label::textColourId));
}

//==============================================================================
void UltrapatcherEditor::addSelectedPlugin()
{
    const int row = listBox.getSelectedRow();

    if (juce::isPositiveAndBelow (row, shownTypes.size()))
        proc.addPlugin (shownTypes.getReference (row));
}

void UltrapatcherEditor::removeSelection()
{
    if (proc.getMode() == RackMode::rack)
    {
        if (const auto uid = rackView.getSelectedDevice(); uid != 0)
            proc.removeNode (uid);
    }
    else
    {
        canvas.deleteSelection();
    }
}

void UltrapatcherEditor::toggleBypassOnSelection()
{
    const auto selection = selectedPlugins();
    bool allBypassed = ! selection.empty();

    for (const auto& info : proc.getPatchNodes())
        if (std::find (selection.begin(), selection.end(), info.uid) != selection.end())
            allBypassed = allBypassed && info.bypassed;

    for (auto uid : selection)
        proc.setNodeBypassed (uid, ! allBypassed);
}

// Patcher to Rack starts a fresh rack (a free-form graph has no rack form),
// so it asks first when there is something to lose. Rack to Patcher is lossless.
void UltrapatcherEditor::requestMode (RackMode newMode)
{
    if (newMode == proc.getMode())
    {
        refresh();
        return;
    }

    if (newMode == RackMode::rack && proc.getPatchNodes().size() > 2)
    {
        juce::Component::SafePointer<UltrapatcherEditor> safeThis (this);

        juce::AlertWindow::showOkCancelBox (
            juce::MessageBoxIconType::WarningIcon,
            "Switch to Rack mode?",
            "A free-form patch cannot be turned into a rack, so this starts a new, empty rack. "
            "The plugins, cords and knob mappings in the current patch will be removed.\n\n"
            "Switching back to Patcher mode later keeps whatever is in the rack.",
            "Switch to Rack", "Cancel", this,
            juce::ModalCallbackFunction::create ([safeThis] (int result)
            {
                if (safeThis != nullptr && result == 1)
                    safeThis->proc.setMode (RackMode::rack);
            }));

        refresh();   // snap the toggle back until the user confirms
        return;
    }

    proc.setMode (newMode);
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

void UltrapatcherEditor::openNodeWindow (NodeUid uid)
{
    if (auto it = pluginWindows.find (uid); it != pluginWindows.end())
    {
        it->second->toFront (true);
        return;
    }

    auto* hostedProcessor = proc.getNodeProcessor (uid);

    if (hostedProcessor == nullptr || ! hostedProcessor->hasEditor())
        return;

    auto* hostedEditor = hostedProcessor->createEditorAndMakeActive();

    if (hostedEditor == nullptr)
        return;

    juce::Component::SafePointer<UltrapatcherEditor> safeThis (this);

    pluginWindows[uid] = std::make_unique<HostedPluginWindow> (hostedEditor, proc.getNodeName (uid),
                                                               proc.isWindowAlwaysOnTop(), [safeThis, uid]
    {
        // Defer: this runs inside the window's own button handler.
        juce::MessageManager::callAsync ([safeThis, uid]
        {
            if (safeThis != nullptr)
                safeThis->pluginWindows.erase (uid);
        });
    });
}
