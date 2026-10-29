#include "PluginProcessor.h"
#include "PluginEditor.h"
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
// A generic automatable parameter whose display name follows the hosted
// parameter it is mapped to ("Param 3" becomes "Cutoff").
class UltrapatcherProcessor::PoolParameter : public juce::AudioParameterFloat
{
public:
    explicit PoolParameter (int slotIndex)
        : juce::AudioParameterFloat (juce::ParameterID ("pool" + juce::String (slotIndex + 1).paddedLeft ('0', 3), 1),
                                     "Param " + juce::String (slotIndex + 1),
                                     juce::NormalisableRange<float> (0.0f, 1.0f),
                                     0.0f),
          defaultName ("Param " + juce::String (slotIndex + 1))
    {
    }

    juce::String getName (int maximumStringLength) const override
    {
        const juce::ScopedLock sl (nameLock);
        return (mappedName.isEmpty() ? defaultName : mappedName).substring (0, maximumStringLength);
    }

    void setMappedName (const juce::String& newName)
    {
        const juce::ScopedLock sl (nameLock);
        mappedName = newName;
    }

    juce::String getMappedName() const
    {
        const juce::ScopedLock sl (nameLock);
        return mappedName;
    }

private:
    mutable juce::CriticalSection nameLock;
    juce::String defaultName, mappedName;
};

static int resolveHostedIndex (const juce::Array<juce::AudioProcessorParameter*>& params,
                               const juce::String& name, int index)
{
    if (juce::isPositiveAndBelow (index, params.size()) && params[index]->getName (64) == name)
        return index;

    for (int i = 0; i < params.size(); ++i)
        if (params[i]->getName (64) == name)
            return i;

    return -1;
}

constexpr float kParamEpsilon = 1.0e-6f;

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

    rebuildConnections();

    for (int i = 0; i < kPoolSize; ++i)
    {
        pool[(size_t) i] = new PoolParameter (i);
        addParameter (pool[(size_t) i]); // the AudioProcessor owns it
    }

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

void UltrapatcherProcessor::loadPlugin (const juce::PluginDescription& desc,
                                        const juce::MemoryBlock& state,
                                        const std::vector<SlotMap>& maps)
{
    juce::WeakReference<UltrapatcherProcessor> weak (this);

    juce::MessageManager::callAsync ([weak, desc, state, maps]
    {
        if (weak != nullptr)
            weak->doLoad (desc, state, maps);
    });
}

void UltrapatcherProcessor::doLoad (const juce::PluginDescription& desc,
                                    const juce::MemoryBlock& state,
                                    const std::vector<SlotMap>& maps)
{
    lastError.clear();

    const double sr = getSampleRate() > 0 ? getSampleRate() : 44100.0;
    const int    bs = getBlockSize()  > 0 ? getBlockSize()  : 512;

    juce::WeakReference<UltrapatcherProcessor> weak (this);

    formatManager.createPluginInstanceAsync (desc, sr, bs,
        [weak, desc, state, maps] (std::unique_ptr<juce::AudioPluginInstance> instance, const juce::String& error)
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

            weak->detachHostedNode();

            auto node = weak->graph.addNode (std::move (instance));

            {
                const juce::SpinLock::ScopedLockType sl (weak->mappingLock);
                weak->hostedNode = node;
            }

            weak->hostedDescription = desc;
            ++weak->hostedGeneration;
            weak->rebuildConnections();
            weak->setLatencySamples (node->getProcessor()->getLatencySamples());
            weak->updateHostDisplay (ChangeDetails().withLatencyChanged (true));

            // Restore saved mappings, matching by parameter name first.
            const auto& params = node->getProcessor()->getParameters();

            for (const auto& m : maps)
            {
                const int idx = resolveHostedIndex (params, m.hostedName, m.hostedIndex);

                if (idx >= 0)
                    weak->mapSlot (m.slot, idx);
            }

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

    detachHostedNode();
    rebuildConnections();
    setLatencySamples (0);
    updateHostDisplay (ChangeDetails().withLatencyChanged (true).withParameterInfoChanged (true));

    hostedChanging = false;
    sendChangeMessage();
}

// Message thread. Takes the hosted node out of the audio thread's reach
// (under the lock) before it is removed from the graph and destroyed.
void UltrapatcherProcessor::detachHostedNode()
{
    Node::Ptr old;

    {
        const juce::SpinLock::ScopedLockType sl (mappingLock);
        old = std::move (hostedNode);
        hostedNode = nullptr;

        for (auto& s : slots)
            s = {};
    }

    for (auto* p : pool)
        p->setMappedName ({});

    if (old != nullptr)
        graph.removeNode (old->nodeID);

    hostedDescription = {};
    ++hostedGeneration;
}

//==============================================================================
// Parameter pool.
//
// Pool slot values are the source of truth for the DAW and the Surface.
//  - Audio thread (applyPoolToHosted): a changed pool value is pushed to the
//    hosted parameter at block rate.
//  - Message thread (timerCallback): a changed hosted value, for example from
//    turning a knob in the plugin's own GUI, is pushed back into the pool so
//    the DAW records it and the Surface follows.
// lastPoolApplied / lastHostedSeen stop the two directions from echoing.
juce::RangedAudioParameter* UltrapatcherProcessor::getPoolParameter (int slot) const
{
    return juce::isPositiveAndBelow (slot, kPoolSize) ? pool[(size_t) slot] : nullptr;
}

juce::Array<UltrapatcherProcessor::HostedParamInfo> UltrapatcherProcessor::getHostedParameterList() const
{
    juce::Array<HostedParamInfo> list;

    if (hostedNode == nullptr)
        return list;

    const auto& params = hostedNode->getProcessor()->getParameters();

    for (int i = 0; i < params.size(); ++i)
        if (params[i]->isAutomatable())
            list.add ({ i, params[i]->getName (64) });

    return list;
}

bool UltrapatcherProcessor::mapSlot (int slot, int hostedParamIndex)
{
    if (! juce::isPositiveAndBelow (slot, kPoolSize) || hostedNode == nullptr)
        return false;

    const auto& params = hostedNode->getProcessor()->getParameters();

    if (! juce::isPositiveAndBelow (hostedParamIndex, params.size()))
        return false;

    auto* hostedParam = params[hostedParamIndex];
    const float hostedValue = hostedParam->getValue();

    // Move the pool value to the hosted value before the mapping goes live,
    // so mapping never makes the hosted parameter jump.
    pool[(size_t) slot]->setMappedName (hostedParam->getName (64));
    pool[(size_t) slot]->setValueNotifyingHost (hostedValue);

    {
        const juce::SpinLock::ScopedLockType sl (mappingLock);
        slots[(size_t) slot] = { hostedParamIndex, pool[(size_t) slot]->get(), hostedValue };
    }

    updateHostDisplay (ChangeDetails().withParameterInfoChanged (true));
    sendChangeMessage();
    return true;
}

void UltrapatcherProcessor::unmapSlot (int slot)
{
    if (! juce::isPositiveAndBelow (slot, kPoolSize))
        return;

    {
        const juce::SpinLock::ScopedLockType sl (mappingLock);
        slots[(size_t) slot] = {};
    }

    pool[(size_t) slot]->setMappedName ({});
    updateHostDisplay (ChangeDetails().withParameterInfoChanged (true));
    sendChangeMessage();
}

int UltrapatcherProcessor::getSlotMapping (int slot) const
{
    return juce::isPositiveAndBelow (slot, kPoolSize) ? slots[(size_t) slot].hostedIndex : -1;
}

juce::String UltrapatcherProcessor::getSlotName (int slot) const
{
    return juce::isPositiveAndBelow (slot, kPoolSize) ? pool[(size_t) slot]->getMappedName() : juce::String();
}

// Audio thread. Never blocks: if the message thread holds the lock this
// block simply skips the update and the next block catches up.
void UltrapatcherProcessor::applyPoolToHosted()
{
    const juce::SpinLock::ScopedTryLockType lock (mappingLock);

    if (! lock.isLocked() || hostedNode == nullptr)
        return;

    const auto& params = hostedNode->getProcessor()->getParameters();

    for (size_t i = 0; i < (size_t) kPoolSize; ++i)
    {
        auto& s = slots[i];

        if (s.hostedIndex < 0 || s.hostedIndex >= params.size())
            continue;

        const float poolValue = pool[i]->get();

        if (std::abs (poolValue - s.lastPoolApplied) > kParamEpsilon)
        {
            s.lastPoolApplied = poolValue;

            auto* hostedParam = params[s.hostedIndex];
            hostedParam->setValue (poolValue);
            s.lastHostedSeen = hostedParam->getValue(); // read back: stepped params may snap
        }
    }
}

// Message thread, 30 Hz. Hosted -> pool direction.
void UltrapatcherProcessor::timerCallback()
{
    std::vector<std::pair<int, float>> updates;

    {
        const juce::SpinLock::ScopedLockType sl (mappingLock);

        if (hostedNode == nullptr)
            return;

        const auto& params = hostedNode->getProcessor()->getParameters();

        for (size_t i = 0; i < (size_t) kPoolSize; ++i)
        {
            auto& s = slots[i];

            if (s.hostedIndex < 0 || s.hostedIndex >= params.size())
                continue;

            const float hostedValue = params[s.hostedIndex]->getValue();

            if (std::abs (hostedValue - s.lastHostedSeen) > kParamEpsilon)
            {
                s.lastHostedSeen = hostedValue;

                if (std::abs (hostedValue - pool[i]->get()) > kParamEpsilon)
                {
                    s.lastPoolApplied = hostedValue; // the pool is about to take this value
                    updates.emplace_back ((int) i, hostedValue);
                }
            }
        }
    }

    // Call into the host outside the lock.
    for (const auto& u : updates)
    {
        auto* p = pool[(size_t) u.first];
        p->beginChangeGesture();
        p->setValueNotifyingHost (u.second);
        p->endChangeGesture();
    }
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
// scanner the folder scan uses, so it also lands in the known-plugin list.
void UltrapatcherProcessor::loadPluginFromFile (const juce::File& file)
{
    juce::WeakReference<UltrapatcherProcessor> weak (this);

    juce::MessageManager::callAsync ([weak, file]
    {
        if (weak != nullptr)
            weak->doLoadFromFile (file);
    });
}

void UltrapatcherProcessor::doLoadFromFile (const juce::File& file)
{
    lastError.clear();

    const auto path = file.getFullPathName();
    juce::OwnedArray<juce::PluginDescription> found;

    if (file.exists())
    {
        for (int i = 0; i < formatManager.getNumFormats() && found.isEmpty(); ++i)
        {
            auto* format = formatManager.getFormat (i);

            if (format->fileMightContainThisPluginType (path))
                knownPlugins.scanAndAddFile (path, true, found, *format);
        }
    }

    if (found.isEmpty())
    {
        lastError = "Could not load " + file.getFileName() + " as a plugin";
        sendChangeMessage();
        return;
    }

    savePluginList();
    loadPlugin (*found.getFirst());
}

//==============================================================================
// State: the hosted plugin's description and opaque state blob, plus the
// pool-slot mappings. Version 2 added <MAPS>; version 1 projects still load.
void UltrapatcherProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::XmlElement xml ("Ultrapatcher");
    xml.setAttribute ("version", 2);

    Node::Ptr node;
    std::vector<int> mappedSlots;

    {
        const juce::SpinLock::ScopedLockType sl (mappingLock);
        node = hostedNode;

        for (int i = 0; i < kPoolSize; ++i)
            if (slots[(size_t) i].hostedIndex >= 0)
                mappedSlots.push_back (i);
    }

    if (node != nullptr)
    {
        xml.addChildElement (hostedDescription.createXml().release());

        juce::MemoryBlock pluginState;
        node->getProcessor()->getStateInformation (pluginState);
        xml.setAttribute ("pluginState", pluginState.toBase64Encoding());

        auto* mapsXml = xml.createNewChildElement ("MAPS");

        for (int slot : mappedSlots)
        {
            auto* m = mapsXml->createNewChildElement ("MAP");
            m->setAttribute ("slot", slot);
            m->setAttribute ("index", slots[(size_t) slot].hostedIndex);
            m->setAttribute ("name", pool[(size_t) slot]->getMappedName());
        }
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

            std::vector<SlotMap> maps;

            if (auto* mapsXml = xml->getChildByName ("MAPS"))
                for (auto* m : mapsXml->getChildWithTagNameIterator ("MAP"))
                    maps.push_back ({ m->getIntAttribute ("slot", -1),
                                      m->getIntAttribute ("index", -1),
                                      m->getStringAttribute ("name") });

            loadPlugin (desc, pluginState, maps);
        }
    }
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new UltrapatcherProcessor();
}
