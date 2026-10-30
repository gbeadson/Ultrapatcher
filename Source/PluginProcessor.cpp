#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "PoolParameter.h"
#include <juce_audio_processors/format/juce_AudioPluginFormatManagerHelpers.h>

using IOProcessor = juce::AudioProcessorGraph::AudioGraphIOProcessor;

//==============================================================================
// Scans the default plugin folders on a background thread.
class UltrapatcherProcessor::ScanThread : public juce::Thread
{
public:
    ScanThread (UltrapatcherProcessor& o, bool customOnly, juce::StringArray extraFolders)
        : juce::Thread ("Ultrapatcher plugin scan"), owner (o),
          customFoldersOnly (customOnly), folders (std::move (extraFolders)) {}
    ~ScanThread() override { stopThread (15000); }

    void run() override
    {
        const auto deadMans = owner.getSupportDirectory().getChildFile ("scan-deadmans.txt");

        for (int i = 0; i < owner.formatManager.getNumFormats() && ! threadShouldExit(); ++i)
        {
            auto* format = owner.formatManager.getFormat (i);

            juce::FileSearchPath searchPath;

            if (! customFoldersOnly)
                searchPath = format->getDefaultLocationsToSearch();

            for (const auto& folder : folders)
                searchPath.add (juce::File (folder));

            juce::PluginDirectoryScanner scanner (owner.knownPlugins, *format, searchPath,
                                                  true, deadMans, false);
            juce::String name;
            while (! threadShouldExit() && scanner.scanNextFile (true, name)) {}
        }

        juce::WeakReference<UltrapatcherProcessor> weak (&owner);
        juce::MessageManager::callAsync ([weak] { if (weak != nullptr) weak->scanFinished(); });
    }

private:
    UltrapatcherProcessor& owner;
    const bool customFoldersOnly;
    const juce::StringArray folders;
};

//==============================================================================
UltrapatcherProcessor::UltrapatcherProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    juce::addDefaultFormatsToManager (formatManager);

    getSupportDirectory().createDirectory();
    settings.load (getSupportDirectory().getChildFile ("settings.xml"));

    if (auto xml = juce::parseXML (getSupportDirectory().getChildFile ("plugins.xml")))
        knownPlugins.recreateFromXml (*xml);

    // The graph needs a channel configuration before connections are legal.
    graph.setPlayConfigDetails (2, 2, 44100.0, 512);
    graph.clear();

    audioInNode  = graph.addNode (std::make_unique<IOProcessor> (IOProcessor::audioInputNode));
    audioOutNode = graph.addNode (std::make_unique<IOProcessor> (IOProcessor::audioOutputNode));
    midiInNode   = graph.addNode (std::make_unique<IOProcessor> (IOProcessor::midiInputNode));
    midiOutNode  = graph.addNode (std::make_unique<IOProcessor> (IOProcessor::midiOutputNode));

    for (int i = 0; i < kPoolSize; ++i)
    {
        pool[(size_t) i] = new PoolParameter (i);
        addParameter (pool[(size_t) i]); // the AudioProcessor owns it
    }

    createDefaultCords();
    startTimerHz (30);
}

UltrapatcherProcessor::~UltrapatcherProcessor()
{
    stopTimer();
    scanThread.reset();
    masterReference.clear();
}

juce::File UltrapatcherProcessor::getSupportDirectory() const
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("Ultrapatcher");
}

//==============================================================================
void UltrapatcherProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    graph.setPlayConfigDetails (getMainBusNumInputChannels(), getMainBusNumOutputChannels(),
                                sampleRate, samplesPerBlock);
    graph.prepareToPlay (sampleRate, samplesPerBlock);
    setLatencySamples (graph.getLatencySamples());
}

void UltrapatcherProcessor::releaseResources()
{
    graph.releaseResources();
}

bool UltrapatcherProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    const auto in  = layouts.getMainInputChannelSet();

    return out == juce::AudioChannelSet::stereo()
        && (in == out || in.isDisabled());
}

void UltrapatcherProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    applyPoolToHosted();
    graph.processBlock (buffer, midi);
}

juce::AudioProcessorEditor* UltrapatcherProcessor::createEditor()
{
    return new UltrapatcherEditor (*this);
}

//==============================================================================
void UltrapatcherProcessor::startScan (bool customFoldersOnly)
{
    if (scanning.load())
        return;

    if (customFoldersOnly && settings.scanFolders.isEmpty())
    {
        lastError = "Add a scan folder in Settings first";
        sendChangeMessage();
        return;
    }

    lastError.clear();
    scanning = true;

    scanThread = std::make_unique<ScanThread> (*this, customFoldersOnly, settings.scanFolders);
    scanThread->startThread();
    sendChangeMessage();
}

void UltrapatcherProcessor::scanFinished()
{
    savePluginList();
    scanning = false;
    sendChangeMessage();
}

void UltrapatcherProcessor::savePluginList()
{
    if (auto xml = knownPlugins.createXml())
        xml->writeTo (getSupportDirectory().getChildFile ("plugins.xml"));
}

void UltrapatcherProcessor::saveSettings()
{
    settings.save (getSupportDirectory().getChildFile ("settings.xml"));
}

void UltrapatcherProcessor::addScanFolder (const juce::File& folder)
{
    if (! folder.isDirectory())
        return;

    settings.scanFolders.addIfNotAlreadyThere (folder.getFullPathName());
    saveSettings();
    sendChangeMessage();
}

void UltrapatcherProcessor::removeScanFolder (int index)
{
    if (! juce::isPositiveAndBelow (index, settings.scanFolders.size()))
        return;

    settings.scanFolders.remove (index);
    saveSettings();
    sendChangeMessage();
}

void UltrapatcherProcessor::setWindowAlwaysOnTop (bool shouldBeOnTop)
{
    if (settings.windowAlwaysOnTop == shouldBeOnTop)
        return;

    settings.windowAlwaysOnTop = shouldBeOnTop;
    saveSettings();
    sendChangeMessage();
}

// Loads a plugin from a file path. The file is probed through the same JUCE

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new UltrapatcherProcessor();
}
