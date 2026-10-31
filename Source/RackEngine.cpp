#include "PluginProcessor.h"

//==============================================================================
// Rack mode.
//
// A rack is a list of chains. Each chain is a serial list of devices ending in
// a mixer strip, and chains run in parallel and sum at the Output. The rack is
// only a model: rebuildRackWiring() turns it into the same nodes and cords the
// Patcher uses, so mapping, learn, windows, latency compensation and state all
// work unchanged. Converting to Patcher mode just stops regenerating the cords.
UltrapatcherProcessor::ChainRecord* UltrapatcherProcessor::findChain (int chainId)
{
    for (auto& c : chains)
        if (c.id == chainId)
            return &c;

    return nullptr;
}

UltrapatcherProcessor::ChainRecord* UltrapatcherProcessor::chainOfDevice (NodeUid uid)
{
    for (auto& c : chains)
        if (std::find (c.devices.begin(), c.devices.end(), uid) != c.devices.end())
            return &c;

    return nullptr;
}

std::vector<ChainInfo> UltrapatcherProcessor::getChains() const
{
    std::vector<ChainInfo> list;

    for (const auto& c : chains)
        list.push_back ({ c.id, c.name, c.stripUid, c.devices });

    return list;
}

ChainStripProcessor* UltrapatcherProcessor::getChainStrip (int chainId) const
{
    for (const auto& c : chains)
        if (c.id == chainId)
            return dynamic_cast<ChainStripProcessor*> (getNodeProcessor (c.stripUid));

    return nullptr;
}

NodeUid UltrapatcherProcessor::installStrip (NodeUid fixedUid, const juce::MemoryBlock& state)
{
    auto strip = std::make_unique<ChainStripProcessor>();

    if (state.getSize() > 0)
        strip->setStateInformation (state.getData(), (int) state.getSize());

    juce::PluginDescription desc;
    desc.name = "Chain mixer";

    const auto uid = installNode (std::move (strip), desc, fixedUid, { -1.0f, -1.0f }, false, false);

    if (auto* rec = findNode (uid))
        rec->isStrip = true;

    return uid;
}

int UltrapatcherProcessor::addChain()
{
    if (mode != RackMode::rack)
        return 0;

    ChainRecord chain;
    chain.id = nextChainId++;
    chain.name = "Chain " + juce::String (chain.id);
    chain.stripUid = installStrip();

    chains.push_back (chain);
    activeChainId = chain.id;

    rebuildRackWiring();
    patchChanged();
    return chain.id;
}

void UltrapatcherProcessor::removeChain (int chainId)
{
    auto* chain = findChain (chainId);

    if (chain == nullptr)
        return;

    const auto devices = chain->devices;
    const auto strip = chain->stripUid;

    beginStructureChange (0);

    for (auto uid : devices)
        removeNodeCore (uid);

    removeNodeCore (strip);

    chains.erase (std::remove_if (chains.begin(), chains.end(),
                                  [chainId] (const ChainRecord& c) { return c.id == chainId; }),
                  chains.end());

    if (activeChainId == chainId)
        activeChainId = chains.empty() ? 0 : chains.back().id;

    rebuildRackWiring();
    endStructureChange();
    patchChanged();
}

void UltrapatcherProcessor::renameChain (int chainId, const juce::String& name)
{
    if (auto* chain = findChain (chainId))
    {
        chain->name = name.trim().isNotEmpty() ? name.trim() : "Chain " + juce::String (chainId);
        sendChangeMessage();
    }
}

void UltrapatcherProcessor::moveDevice (NodeUid uid, int chainId, int index)
{
    if (mode != RackMode::rack)
        return;

    auto* from = chainOfDevice (uid);
    auto* to = findChain (chainId);

    if (from == nullptr || to == nullptr)
        return;

    from->devices.erase (std::remove (from->devices.begin(), from->devices.end(), uid), from->devices.end());
    to->devices.insert (to->devices.begin() + juce::jlimit (0, (int) to->devices.size(), index), uid);
    activeChainId = chainId;

    rebuildRackWiring();
    patchChanged();
}

void UltrapatcherProcessor::installIntoChain (std::unique_ptr<juce::AudioProcessor> processor,
                                              const juce::PluginDescription& desc, int chainId)
{
    if (findChain (chainId) == nullptr)
        chainId = chains.empty() ? addChain() : chains.back().id;

    const auto uid = installNode (std::move (processor), desc, 0, { -1.0f, -1.0f }, false, false);

    if (uid == 0)
        return;

    if (auto* chain = findChain (chainId))
        chain->devices.push_back (uid);

    activeChainId = chainId;
    rebuildRackWiring();
    patchChanged();
}

// Generates the cords for the whole rack.
//  - Audio runs Input, device, device, ..., mixer strip, Output, per chain.
//    An instrument (no audio input) replaces the audio before it, like an
//    Instrument Rack in Ableton or Bitwig.
//  - MIDI reaches every device that takes it and passes through MIDI effects.
//    The Input to Output MIDI thru is always kept.
void UltrapatcherProcessor::rebuildRackWiring()
{
    for (const auto& c : std::vector<Cord> (cords))
        removeCordInternal (c);

    addCordInternal ({ kInputUid, kOutputUid, true });

    for (const auto& chain : chains)
    {
        NodeUid audioSrc = kInputUid, midiSrc = kInputUid;

        for (auto uid : chain.devices)
        {
            const bool audioIn = audioInputsOf (uid) > 0, audioOut = audioOutputsOf (uid) > 0;

            if (audioIn && audioOut)
            {
                addCordInternal ({ audioSrc, uid, false });
                audioSrc = uid;
            }
            else if (! audioIn && audioOut)
            {
                audioSrc = uid;
            }

            const bool midiIn = midiInOf (uid), midiOut = midiOutOf (uid);

            if (midiIn)
                addCordInternal ({ midiSrc, uid, true });

            if (midiIn && midiOut)
                midiSrc = uid;
        }

        addCordInternal ({ audioSrc, chain.stripUid, false });
        addCordInternal ({ chain.stripUid, kOutputUid, false });

        if (midiSrc != kInputUid)
            addCordInternal ({ midiSrc, kOutputUid, true });
    }

    lastError.clear();
}

//==============================================================================
void UltrapatcherProcessor::setMode (RackMode newMode)
{
    if (newMode == mode)
        return;

    if (newMode == RackMode::rack)
    {
        // A free-form graph has no rack form, so this starts a fresh rack.
        clearPatch (false);
        mode = RackMode::rack;
        addChain();
    }
    else
    {
        layoutRackAsPatch();
        mode = RackMode::patcher;
        chains.clear();
        activeChainId = 0;
        patchChanged();
    }
}

// Rack to Patcher is lossless: the nodes and cords are already there, so they
// only need positions, laid out one chain per row.
void UltrapatcherProcessor::layoutRackAsPatch()
{
    inputPos = { 24.0f, 40.0f };
    outputPos = { -1.0f, 40.0f };

    for (size_t i = 0; i < chains.size(); ++i)
    {
        const float y = 24.0f + 110.0f * (float) i;
        float x = 200.0f;

        for (auto uid : chains[i].devices)
        {
            setNodePosition (uid, { x, y });
            x += 170.0f;
        }

        setNodePosition (chains[i].stripUid, { x, y });

        if (auto* rec = findNode (chains[i].stripUid))
            rec->name = chains[i].name + " mixer";
    }
}

// Solo is rack-wide: while any strip is soloed, every other strip is muted.
void UltrapatcherProcessor::updateSoloSuppression()
{
    bool anySolo = false;

    for (const auto& n : nodes)
        if (n.isStrip)
            if (auto* strip = dynamic_cast<ChainStripProcessor*> (n.graphNode->getProcessor()))
                anySolo = anySolo || strip->solo->get();

    for (const auto& n : nodes)
        if (n.isStrip)
            if (auto* strip = dynamic_cast<ChainStripProcessor*> (n.graphNode->getProcessor()))
                strip->suppressed = anySolo && ! strip->solo->get();
}
