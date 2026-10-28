#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <juce_audio_processors/format/juce_AudioPluginFormatManagerHelpers.h>

using IOProcessor = juce::AudioProcessorGraph::AudioGraphIOProcessor;

//==============================================================================
// Scans the default plugin folders on a background thread.
class UltrapatcherProcessor::ScanThread : public juce::Thread
{
public:
    explicit ScanThread (UltrapatcherProcessor& o) : juce::Thread ("Ultrapatcher plugin scan"), owner (o) {}
    ~ScanThread() override { stopThread (15000); }

    void run() override
    {
        const auto deadMans = owner.getSupportDirectory().getChildFile ("scan-deadmans.txt");

        for (int i = 0; i < owner.formatManager.getNumFormats() && ! threadShouldExit(); ++i)
        {
            auto* format = owner.formatManager.getFormat (i);

            juce::PluginDirectoryScanner scanner (owner.knownPlugins, *format,
                                                  format->getDefaultLocationsToSearch(),
                                                  true, deadMans, false);
            juce::String name;
            while (! threadShouldExit() && scanner.scanNextFile (true, name)) {}
        }

        juce::WeakReference<UltrapatcherProcessor> weak (&owner);
        juce::MessageManager::callAsync ([weak] { if (weak != nullptr) weak->scanFinished(); });
    }

private:
    UltrapatcherProcessor& owner;
};

//==============================================================================
UltrapatcherProcessor::UltrapatcherProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    juce::addDefaultFormatsToManager (formatManager);

    getSupportDirectory().createDirectory();
    if (auto xml = juce::parseXML (getSupportDirectory().getChildFile ("plugins.xml")))
        knownPlugins.recreateFromXml (*xml);

    // The graph needs a channel configuration before connections are legal.
    graph.setPlayConfigDetails (2, 2, 44100.0, 512);
    graph.clear();

    audioInNode  = graph.addNode (std::make_unique<IOProcessor> (IOProcessor::audioInputNode));
    audioOutNode = graph.addNode (std::make_unique<IOProcessor> (IOProcessor::audioOutputNode));
    midiInNode   = graph.addNode (std::make_unique<IOProcessor> (IOProcessor::midiInputNode));
    midiOutNode  = graph.addNode (std::make_unique<IOProcessor> (IOProcessor::midiOutputNode));

    rebuildConnections();
}

UltrapatcherProcessor::~UltrapatcherProcessor()
{
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
    graph.processBlock (buffer, midi);
}

juce::AudioProcessorEditor* UltrapatcherProcessor::createEditor()
{
    return new UltrapatcherEditor (*this);
}

//==============================================================================
// Routing. Milestone 1: one hosted plugin, or straight passthrough.
void UltrapatcherProcessor::rebuildConnections()
{
    const auto existing = graph.getConnections();
    for (const auto& c : existing)
        graph.removeConnection (c);

    constexpr int midiCh = juce::AudioProcessorGraph::midiChannelIndex;

    if (hostedNode != nullptr)
    {
        auto* p = hostedNode->getProcessor();

        for (int ch = 0; ch < juce::jmin (2, p->getTotalNumInputChannels()); ++ch)
            graph.addConnection ({ { audioInNode->nodeID, ch }, { hostedNode->nodeID, ch } });

        for (int ch = 0; ch < juce::jmin (2, p->getTotalNumOutputChannels()); ++ch)
            graph.addConnection ({ { hostedNode->nodeID, ch }, { audioOutNode->nodeID, ch } });

        graph.addConnection ({ { midiInNode->nodeID, midiCh }, { hostedNode->nodeID, midiCh } });
        graph.addConnection ({ { hostedNode->nodeID, midiCh }, { midiOutNode->nodeID, midiCh } });
    }
    else
    {
        for (int ch = 0; ch < 2; ++ch)
            graph.addConnection ({ { audioInNode->nodeID, ch }, { audioOutNode->nodeID, ch } });

        graph.addConnection ({ { midiInNode->nodeID, midiCh }, { midiOutNode->nodeID, midiCh } });
    }
}

//==============================================================================
juce::AudioPluginInstance* UltrapatcherProcessor::getHostedPlugin() const
{
    if (hostedNode == nullptr)
        return nullptr;

    return dynamic_cast<juce::AudioPluginInstance*> (hostedNode->getProcessor());
}

void UltrapatcherProcessor::loadPlugin (const juce::PluginDescription& desc, const juce::MemoryBlock& state)
{
    juce::WeakReference<UltrapatcherProcessor> weak (this);

    juce::MessageManager::callAsync ([weak, desc, state]
    {
        if (weak != nullptr)
            weak->doLoad (desc, state);
    });
}

void UltrapatcherProcessor::doLoad (const juce::PluginDescription& desc, const juce::MemoryBlock& state)
{
    lastError.clear();

    const double sr = getSampleRate() > 0 ? getSampleRate() : 44100.0;
    const int    bs = getBlockSize()  > 0 ? getBlockSize()  : 512;

    juce::WeakReference<UltrapatcherProcessor> weak (this);

    formatManager.createPluginInstanceAsync (desc, sr, bs,
        [weak, desc, state] (std::unique_ptr<juce::AudioPluginInstance> instance, const juce::String& error)
        {
            if (weak == nullptr)
                return;

            if (instance == nullptr)
            {
                weak->lastError = error.isNotEmpty() ? error : "Could not load " + desc.name;
                weak->sendChangeMessage();
                return;
            }

            if (state.getSize() > 0)
                instance->setStateInformation (state.getData(), (int) state.getSize());

            // Editors must drop the old plugin's GUI before the plugin goes away.
            weak->hostedChanging = true;
            weak->sendSynchronousChangeMessage();

            if (weak->hostedNode != nullptr)
                weak->graph.removeNode (weak->hostedNode->nodeID);

            weak->hostedDescription = desc;
            weak->hostedNode = weak->graph.addNode (std::move (instance));
            weak->rebuildConnections();
            weak->setLatencySamples (weak->hostedNode->getProcessor()->getLatencySamples());
            weak->updateHostDisplay (ChangeDetails().withLatencyChanged (true));

            weak->hostedChanging = false;
            weak->sendChangeMessage();
        });
}

void UltrapatcherProcessor::unloadPlugin()
{
    juce::WeakReference<UltrapatcherProcessor> weak (this);

    juce::MessageManager::callAsync ([weak]
    {
        if (weak != nullptr)
            weak->doUnload();
    });
}

void UltrapatcherProcessor::doUnload()
{
    if (hostedNode == nullptr)
        return;

    hostedChanging = true;
    sendSynchronousChangeMessage();

    graph.removeNode (hostedNode->nodeID);
    hostedNode = nullptr;
    hostedDescription = {};
    rebuildConnections();
    setLatencySamples (0);
    updateHostDisplay (ChangeDetails().withLatencyChanged (true));

    hostedChanging = false;
    sendChangeMessage();
}

//==============================================================================
void UltrapatcherProcessor::startScan()
{
    if (scanning.exchange (true))
        return;

    scanThread = std::make_unique<ScanThread> (*this);
    scanThread->startThread();
    sendChangeMessage();
}

void UltrapatcherProcessor::scanFinished()
{
    if (auto xml = knownPlugins.createXml())
        xml->writeTo (getSupportDirectory().getChildFile ("plugins.xml"));

    scanning = false;
    sendChangeMessage();
}

//==============================================================================
// State: the hosted plugin's description plus its opaque state blob.
// Format is versioned from day one.
void UltrapatcherProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::XmlElement xml ("Ultrapatcher");
    xml.setAttribute ("version", 1);

    if (auto* plugin = getHostedPlugin())
    {
        xml.addChildElement (hostedDescription.createXml().release());

        juce::MemoryBlock pluginState;
        plugin->getStateInformation (pluginState);
        xml.setAttribute ("pluginState", pluginState.toBase64Encoding());
    }

    copyXmlToBinary (xml, destData);
}

void UltrapatcherProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr || ! xml->hasTagName ("Ultrapatcher"))
        return;

    if (auto* pluginXml = xml->getChildByName ("PLUGIN"))
    {
        juce::PluginDescription desc;

        if (desc.loadFromXml (*pluginXml))
        {
            juce::MemoryBlock pluginState;
            pluginState.fromBase64Encoding (xml->getStringAttribute ("pluginState"));
            loadPlugin (desc, pluginState);
        }
    }
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new UltrapatcherProcessor();
}
