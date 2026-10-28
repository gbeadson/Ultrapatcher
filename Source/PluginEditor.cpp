#include "PluginEditor.h"

//==============================================================================
// Floating window that owns a hosted plugin's editor component.
class HostedPluginWindow : public juce::DocumentWindow
{
public:
    HostedPluginWindow (juce::AudioProcessorEditor* editor, const juce::String& name,
                        std::function<void()> onClose)
        : juce::DocumentWindow (name, juce::Colours::darkgrey, juce::DocumentWindow::closeButton),
          onCloseFn (std::move (onClose))
    {
        setUsingNativeTitleBar (true);
        setContentOwned (editor, true);
        setResizable (editor->isResizable(), false);
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
UltrapatcherEditor::UltrapatcherEditor (UltrapatcherProcessor& p)
    : juce::AudioProcessorEditor (&p), proc (p)
{
    for (auto* c : std::initializer_list<juce::Component*> { &scanButton, &loadButton, &unloadButton,
                                                              &openButton, &pluginBox, &statusLabel })
        addAndMakeVisible (c);

    statusLabel.setJustificationType (juce::Justification::centredLeft);

    scanButton.onClick = [this] { proc.startScan(); };

    loadButton.onClick = [this]
    {
        const int id = pluginBox.getSelectedId();
        if (id > 0 && id <= types.size())
            proc.loadPlugin (types.getReference (id - 1));
    };

    unloadButton.onClick = [this] { proc.unloadPlugin(); };
    openButton.onClick   = [this] { openHostedEditor(); };

    proc.addChangeListener (this);
    proc.knownPlugins.addChangeListener (this);

    refresh();
    setSize (480, 170);
}

UltrapatcherEditor::~UltrapatcherEditor()
{
    proc.removeChangeListener (this);
    proc.knownPlugins.removeChangeListener (this);
    pluginWindow.reset(); // hosted editor must die before anything else
}

void UltrapatcherEditor::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
}

void UltrapatcherEditor::resized()
{
    auto area = getLocalBounds().reduced (12);

    auto row = area.removeFromTop (28);
    scanButton.setBounds (row.removeFromLeft (110));
    row.removeFromLeft (8);
    statusLabel.setBounds (row);

    area.removeFromTop (10);
    pluginBox.setBounds (area.removeFromTop (28));

    area.removeFromTop (10);
    row = area.removeFromTop (28);
    loadButton.setBounds   (row.removeFromLeft (80));
    row.removeFromLeft (8);
    unloadButton.setBounds (row.removeFromLeft (80));
    row.removeFromLeft (8);
    openButton.setBounds   (row.removeFromLeft (170));
}

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

void UltrapatcherEditor::refresh()
{
    const int previous = pluginBox.getSelectedItemIndex();

    types = proc.knownPlugins.getTypes();
    pluginBox.clear (juce::dontSendNotification);

    for (int i = 0; i < types.size(); ++i)
        pluginBox.addItem (types[i].name + "  (" + types[i].pluginFormatName + ")", i + 1);

    if (previous >= 0 && previous < types.size())
        pluginBox.setSelectedItemIndex (previous, juce::dontSendNotification);

    const bool hosting = proc.getHostedPlugin() != nullptr;

    scanButton.setEnabled (! proc.isScanning());
    unloadButton.setEnabled (hosting);
    openButton.setEnabled (hosting);

    if (proc.isScanning())
        statusLabel.setText ("Scanning...", juce::dontSendNotification);
    else if (proc.getLastError().isNotEmpty())
        statusLabel.setText (proc.getLastError(), juce::dontSendNotification);
    else if (hosting)
        statusLabel.setText ("Hosting: " + proc.getHostedName(), juce::dontSendNotification);
    else
        statusLabel.setText ("No plugin loaded (passthrough)", juce::dontSendNotification);
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

    auto* hostedEditor = plugin->createEditorIfNeeded();
    if (hostedEditor == nullptr)
        return;

    juce::Component::SafePointer<UltrapatcherEditor> safeThis (this);

    pluginWindow = std::make_unique<HostedPluginWindow> (hostedEditor, proc.getHostedName(), [safeThis]
    {
        // Defer: this runs inside the window's own button handler.
        juce::MessageManager::callAsync ([safeThis]
        {
            if (safeThis != nullptr)
                safeThis->pluginWindow.reset();
        });
    });
}
