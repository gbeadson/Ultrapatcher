#include "PluginProcessor.h"
#include "PoolParameter.h"

using UpdateKind = juce::AudioProcessorGraph::UpdateKind;

//==============================================================================
// Lookup and port capabilities.
UltrapatcherProcessor::NodeRecord* UltrapatcherProcessor::findNode (NodeUid uid)
{
    for (auto& n : nodes)
        if (n.uid == uid)
            return &n;

    return nullptr;
}

const UltrapatcherProcessor::NodeRecord* UltrapatcherProcessor::findNode (NodeUid uid) const
{
    for (auto& n : nodes)
        if (n.uid == uid)
            return &n;

    return nullptr;
}

int UltrapatcherProcessor::audioInputsOf (NodeUid uid) const
{
    if (uid == kInputUid)  return 0;
    if (uid == kOutputUid) return 2;

    if (auto* n = findNode (uid))
        return n->graphNode->getProcessor()->getTotalNumInputChannels();

    return 0;
}

int UltrapatcherProcessor::audioOutputsOf (NodeUid uid) const
{
    if (uid == kInputUid)  return 2;
    if (uid == kOutputUid) return 0;

    if (auto* n = findNode (uid))
        return n->graphNode->getProcessor()->getTotalNumOutputChannels();

    return 0;
}

bool UltrapatcherProcessor::midiInOf (NodeUid uid) const
{
    if (uid == kInputUid)  return false;
    if (uid == kOutputUid) return true;

    auto* n = findNode (uid);
    return n != nullptr && n->graphNode->getProcessor()->acceptsMidi();
}

bool UltrapatcherProcessor::midiOutOf (NodeUid uid) const
{
    if (uid == kInputUid)  return true;
    if (uid == kOutputUid) return false;

    auto* n = findNode (uid);
    return n != nullptr && n->graphNode->getProcessor()->producesMidi();
}

juce::AudioProcessor* UltrapatcherProcessor::getNodeProcessor (NodeUid uid) const
{
    auto* n = findNode (uid);
    return n != nullptr ? n->graphNode->getProcessor() : nullptr;
}

juce::String UltrapatcherProcessor::getNodeName (NodeUid uid) const
{
    if (uid == kInputUid)  return "Input";
    if (uid == kOutputUid) return "Output";

    auto* n = findNode (uid);
    return n != nullptr ? n->name : juce::String();
}

juce::Array<PatchNodeInfo> UltrapatcherProcessor::getPatchNodes() const
{
    juce::Array<PatchNodeInfo> list;

    PatchNodeInfo in;
    in.uid = kInputUid; in.name = "Input"; in.isIO = true; in.pos = inputPos;
    in.audioOuts = 2; in.producesMidi = true;
    list.add (in);

    PatchNodeInfo out;
    out.uid = kOutputUid; out.name = "Output"; out.isIO = true; out.pos = outputPos;
    out.audioIns = 2; out.acceptsMidi = true;
    list.add (out);

    for (const auto& n : nodes)
    {
        PatchNodeInfo info;
        info.uid = n.uid;
        info.name = n.name;
        info.subtitle = n.isStrip ? juce::String ("Chain mixer") : n.desc.manufacturerName;
        info.bypassed = n.bypassed;
        info.pos = n.pos;

        auto* p = n.graphNode->getProcessor();
        info.audioIns = p->getTotalNumInputChannels();
        info.audioOuts = p->getTotalNumOutputChannels();
        info.acceptsMidi = p->acceptsMidi();
        info.producesMidi = p->producesMidi();
        list.add (info);
    }

    return list;
}

//==============================================================================
// Graph wiring.
juce::AudioProcessorGraph::NodeID UltrapatcherProcessor::graphIdFor (NodeUid uid, bool midi) const
{
    if (uid == kInputUid)  return midi ? midiInNode->nodeID  : audioInNode->nodeID;
    if (uid == kOutputUid) return midi ? midiOutNode->nodeID : audioOutNode->nodeID;

    return findNode (uid)->graphNode->nodeID;
}

// An audio cord is a stereo pair. Mono outputs feed both inputs, and stereo
// outputs sum into a mono input.
std::vector<UltrapatcherProcessor::Conn> UltrapatcherProcessor::connectionsFor (const Cord& c) const
{
    std::vector<Conn> out;
    const auto s = graphIdFor (c.src, c.midi);
    const auto d = graphIdFor (c.dst, c.midi);

    if (c.midi)
    {
        out.push_back ({ { s, juce::AudioProcessorGraph::midiChannelIndex },
                         { d, juce::AudioProcessorGraph::midiChannelIndex } });
        return out;
    }

    const int ns = juce::jmin (2, audioOutputsOf (c.src));
    const int nd = juce::jmin (2, audioInputsOf (c.dst));

    if (ns <= 0 || nd <= 0)
        return out;

    if (ns == 1)
        for (int ch = 0; ch < nd; ++ch) out.push_back ({ { s, 0 }, { d, ch } });
    else if (nd == 1)
        for (int ch = 0; ch < ns; ++ch) out.push_back ({ { s, ch }, { d, 0 } });
    else
        for (int ch = 0; ch < juce::jmin (ns, nd); ++ch) out.push_back ({ { s, ch }, { d, ch } });

    return out;
}

bool UltrapatcherProcessor::cordIsValid (const Cord& c, juce::String& why) const
{
    if (c.src == c.dst)
    {
        why = "A node cannot connect to itself";
        return false;
    }

    if (c.src == kOutputUid || c.dst == kInputUid)
    {
        why = "Cords run from an output port to an input port";
        return false;
    }

    if ((c.src != kInputUid && findNode (c.src) == nullptr) || (c.dst != kOutputUid && findNode (c.dst) == nullptr))
    {
        why = "That node no longer exists";
        return false;
    }

    if (std::find (cords.begin(), cords.end(), c) != cords.end())
    {
        why = "Those ports are already connected";
        return false;
    }

    // JUCE's graph does not reject loops itself, so direct feedback is refused
    // here, as the Grid does: if the destination already leads back to the
    // source, this cord would close a loop.
    {
        std::vector<NodeUid> pending { c.dst }, seen;

        while (! pending.empty())
        {
            const auto current = pending.back();
            pending.pop_back();

            if (current == c.src)
            {
                why = "That connection would create a feedback loop";
                return false;
            }

            if (std::find (seen.begin(), seen.end(), current) != seen.end())
                continue;

            seen.push_back (current);

            for (const auto& existing : cords)
                if (existing.src == current)
                    pending.push_back (existing.dst);
        }
    }

    if (c.midi)
    {
        if (! midiOutOf (c.src) || ! midiInOf (c.dst))
        {
            why = "MIDI cords need a MIDI output and a MIDI input";
            return false;
        }
    }
    else if (audioOutputsOf (c.src) <= 0 || audioInputsOf (c.dst) <= 0)
    {
        why = "Audio cords need an audio output and an audio input";
        return false;
    }

    return true;
}

bool UltrapatcherProcessor::addCordInternal (const Cord& c)
{
    juce::String why;

    if (! cordIsValid (c, why))
    {
        lastError = why;
        return false;
    }

    std::vector<Conn> added;

    for (const auto& conn : connectionsFor (c))
    {
        if (graph.addConnection (conn, UpdateKind::none))
        {
            added.push_back (conn);
        }
        else
        {
            for (const auto& a : added)
                graph.removeConnection (a, UpdateKind::none);

            lastError = "That connection would create a feedback loop";
            return false;
        }
    }

    if (added.empty())
    {
        lastError = "Nothing to connect";
        return false;
    }

    cords.push_back (c);
    return true;
}

void UltrapatcherProcessor::removeCordInternal (const Cord& c)
{
    auto it = std::find (cords.begin(), cords.end(), c);

    if (it == cords.end())
        return;

    for (const auto& conn : connectionsFor (c))
        graph.removeConnection (conn, UpdateKind::none);

    cords.erase (it);
}

void UltrapatcherProcessor::createDefaultCords()
{
    addCordInternal ({ kInputUid, kOutputUid, false });
    addCordInternal ({ kInputUid, kOutputUid, true });
    graph.rebuild();
}

void UltrapatcherProcessor::setError (const juce::String& message)
{
    lastError = message;
    sendChangeMessage();
}

// Rebuilds the render sequence (which also recomputes latency compensation
// between parallel branches) and tells the UI and the host.
void UltrapatcherProcessor::patchChanged()
{
    ++patchGeneration;
    graph.rebuild();
    setLatencySamples (graph.getLatencySamples());
    updateHostDisplay (ChangeDetails().withLatencyChanged (true));
    sendChangeMessage();
}

void UltrapatcherProcessor::beginStructureChange (NodeUid uid)
{
    cancelLearn();
    changingUid = uid;
    structureChanging = true;
    sendSynchronousChangeMessage();
}

void UltrapatcherProcessor::endStructureChange()
{
    structureChanging = false;
    changingUid = 0;
}

//==============================================================================
// Public cord editing.
bool UltrapatcherProcessor::addCord (const Cord& c)
{
    lastError.clear();

    if (mode == RackMode::rack)
    {
        setError ("The rack wires itself. Switch to Patcher mode to connect cords by hand.");
        return false;
    }

    if (addCordInternal (c))
    {
        patchChanged();
        return true;
    }

    sendChangeMessage();
    return false;
}

void UltrapatcherProcessor::removeCord (const Cord& c)
{
    if (mode == RackMode::rack)
        return;

    if (std::find (cords.begin(), cords.end(), c) == cords.end())
        return;

    removeCordInternal (c);
    patchChanged();
}

// The Grid's insert gesture: drop a module on a cord and it is spliced in.
bool UltrapatcherProcessor::insertNodeOnCord (NodeUid uid, const Cord& cord)
{
    lastError.clear();

    if (mode == RackMode::rack)
        return false;

    if (std::find (cords.begin(), cords.end(), cord) == cords.end() || findNode (uid) == nullptr)
        return false;

    const bool capable = cord.midi ? (midiInOf (uid) && midiOutOf (uid))
                                   : (audioInputsOf (uid) > 0 && audioOutputsOf (uid) > 0);

    if (! capable)
    {
        setError (cord.midi ? "That node has no MIDI input and output to splice into a MIDI cord"
                            : "That node has no audio input and output to splice into an audio cord");
        return false;
    }

    removeCordInternal (cord);

    const Cord a { cord.src, uid, cord.midi };
    const Cord b { uid, cord.dst, cord.midi };

    if (addCordInternal (a) && addCordInternal (b))
    {
        patchChanged();
        return true;
    }

    const auto reason = lastError;
    removeCordInternal (a);
    removeCordInternal (b);
    addCordInternal (cord);
    lastError = reason;
    patchChanged();
    return false;
}

//==============================================================================
// Nodes.
NodeUid UltrapatcherProcessor::installNode (std::unique_ptr<juce::AudioProcessor> processor,
                                            const juce::PluginDescription& desc,
                                            NodeUid fixedUid, juce::Point<float> pos,
                                            bool bypassed, bool autoWire)
{
    NodeRecord rec;
    rec.uid = fixedUid != 0 ? fixedUid : nextUid;
    nextUid = juce::jmax (nextUid, rec.uid + 1);
    rec.name = desc.name.isNotEmpty() ? desc.name : processor->getName();
    rec.desc = desc;
    rec.bypassed = bypassed;

    const int n = (int) nodes.size();
    rec.pos = pos.x < 0.0f ? juce::Point<float> (200.0f + 180.0f * (float) (n % 2), 24.0f + 110.0f * (float) (n / 2))
                           : pos;

    rec.graphNode = graph.addNode (std::move (processor), std::nullopt, UpdateKind::none);

    if (rec.graphNode == nullptr)
    {
        setError ("Could not add " + rec.name);
        return 0;
    }

    rec.graphNode->setBypassed (bypassed);
    nodes.push_back (rec);

    if (autoWire && mode == RackMode::patcher)
        autoWireNode (rec.uid);

    patchChanged();
    return rec.uid;
}

// New plugins slot in before the Output where that is unambiguous: if exactly
// one audio cord feeds the Output, the plugin is spliced into it.
void UltrapatcherProcessor::autoWireNode (NodeUid uid)
{
    const bool audioIn = audioInputsOf (uid) > 0, audioOut = audioOutputsOf (uid) > 0;

    std::vector<Cord> feeding;

    for (const auto& c : cords)
        if (! c.midi && c.dst == kOutputUid)
            feeding.push_back (c);

    if (audioIn && audioOut && feeding.size() == 1)
        insertNodeOnCord (uid, feeding.front());
    else if (! audioIn && audioOut)
        addCordInternal ({ uid, kOutputUid, false });

    const bool midiIn = midiInOf (uid), midiOut = midiOutOf (uid);
    const Cord thru { kInputUid, kOutputUid, true };

    if (midiIn && midiOut)
    {
        if (std::find (cords.begin(), cords.end(), thru) != cords.end())
        {
            insertNodeOnCord (uid, thru);
        }
        else
        {
            addCordInternal ({ kInputUid, uid, true });
            addCordInternal ({ uid, kOutputUid, true });
        }
    }
    else if (midiIn)  addCordInternal ({ kInputUid, uid, true });
    else if (midiOut) addCordInternal ({ uid, kOutputUid, true });

    lastError.clear();
}

void UltrapatcherProcessor::addPlugin (const juce::PluginDescription& desc, const juce::MemoryBlock& state)
{
    juce::WeakReference<UltrapatcherProcessor> weak (this);

    juce::MessageManager::callAsync ([weak, desc, state]
    {
        if (weak != nullptr)
            weak->doAddPlugin (desc, state);
    });
}

void UltrapatcherProcessor::doAddPlugin (const juce::PluginDescription& desc, const juce::MemoryBlock& state)
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
                weak->setError (error.isNotEmpty() ? error : "Could not load " + desc.name);
                return;
            }

            if (state.getSize() > 0)
                instance->setStateInformation (state.getData(), (int) state.getSize());

            if (weak->mode == RackMode::rack)
                weak->installIntoChain (std::move (instance), desc, weak->activeChainId);
            else
                weak->installNode (std::move (instance), desc, 0, { -1.0f, -1.0f }, false, true);
        });
}

// Removes a node and everything that points at it, with no signalling.
void UltrapatcherProcessor::removeNodeCore (NodeUid uid)
{
    auto* rec = findNode (uid);

    if (rec == nullptr)
        return;

    dropTargetsForNode (uid);

    for (const auto& c : std::vector<Cord> (cords))
        if (c.src == uid || c.dst == uid)
            removeCordInternal (c);

    graph.removeNode (rec->graphNode->nodeID, UpdateKind::none);
    nodes.erase (std::remove_if (nodes.begin(), nodes.end(),
                                 [uid] (const NodeRecord& n) { return n.uid == uid; }),
                 nodes.end());
}

void UltrapatcherProcessor::removeNode (NodeUid uid)
{
    auto* rec = findNode (uid);

    if (rec == nullptr)
        return;

    // A rack's mixer strips go with their chain, never on their own.
    if (mode == RackMode::rack && rec->isStrip)
        return;

    beginStructureChange (uid);

    if (mode == RackMode::rack)
    {
        if (auto* chain = chainOfDevice (uid))
            chain->devices.erase (std::remove (chain->devices.begin(), chain->devices.end(), uid), chain->devices.end());
    }
    else
    {
        // Keep the signal flowing: whatever fed this node now feeds whatever it fed.
        for (bool midi : { false, true })
        {
            std::vector<Cord> ins, outs;

            for (const auto& c : cords)
            {
                if (c.midi != midi) continue;
                if (c.dst == uid) ins.push_back (c);
                if (c.src == uid) outs.push_back (c);
            }

            for (const auto& i : ins)
                for (const auto& o : outs)
                    addCordInternal ({ i.src, o.dst, midi });
        }

        lastError.clear();
    }

    removeNodeCore (uid);

    if (mode == RackMode::rack)
        rebuildRackWiring();

    endStructureChange();
    patchChanged();
}

void UltrapatcherProcessor::setNodeBypassed (NodeUid uid, bool shouldBypass)
{
    if (auto* rec = findNode (uid))
    {
        rec->bypassed = shouldBypass;
        rec->graphNode->setBypassed (shouldBypass);
        sendChangeMessage();
    }
}

void UltrapatcherProcessor::setNodePosition (NodeUid uid, juce::Point<float> pos)
{
    if (uid == kInputUid)        inputPos = pos;
    else if (uid == kOutputUid)  outputPos = pos;
    else if (auto* rec = findNode (uid)) rec->pos = pos;
}

//==============================================================================
// Loading a plugin from a file path (browse, or drag and drop).
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
        setError ("Could not load " + file.getFileName() + " as a plugin");
        return;
    }

    savePluginList();
    doAddPlugin (*found.getFirst(), {});
}

//==============================================================================
// Project state. Version 3 stores the whole patch; versions 1 and 2 stored a
// single hosted plugin and still load, wired automatically.
void UltrapatcherProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto* mm = juce::MessageManager::getInstance();

    if (! mm->isThisTheMessageThread())
    {
        struct Ctx { UltrapatcherProcessor* self; juce::MemoryBlock* dest; } ctx { this, &destData };

        mm->callFunctionOnMessageThread ([] (void* p) -> void*
        {
            auto* c = static_cast<Ctx*> (p);
            c->self->getStateInformation (*c->dest);
            return nullptr;
        }, &ctx);

        return;
    }

    juce::XmlElement xml ("Ultrapatcher");
    xml.setAttribute ("version", 4);
    xml.setAttribute ("mode", mode == RackMode::rack ? "rack" : "patcher");
    xml.setAttribute ("nextUid", (int) nextUid);

    auto* io = xml.createNewChildElement ("IO");
    auto* in = io->createNewChildElement ("INPUT");
    in->setAttribute ("x", (double) inputPos.x);
    in->setAttribute ("y", (double) inputPos.y);
    auto* out = io->createNewChildElement ("OUTPUT");
    out->setAttribute ("x", (double) outputPos.x);
    out->setAttribute ("y", (double) outputPos.y);

    auto* nodesXml = xml.createNewChildElement ("NODES");

    for (const auto& n : nodes)
    {
        auto* e = nodesXml->createNewChildElement ("NODE");
        e->setAttribute ("uid", (int) n.uid);
        e->setAttribute ("bypassed", n.bypassed);
        e->setAttribute ("x", (double) n.pos.x);
        e->setAttribute ("y", (double) n.pos.y);

        juce::MemoryBlock state;
        n.graphNode->getProcessor()->getStateInformation (state);
        e->setAttribute ("state", state.toBase64Encoding());

        if (n.isStrip)
            e->setAttribute ("kind", "strip");
        else
            e->addChildElement (n.desc.createXml().release());
    }

    auto* rackXml = xml.createNewChildElement ("RACK");
    rackXml->setAttribute ("nextChainId", nextChainId);

    for (const auto& chain : chains)
    {
        auto* ce = rackXml->createNewChildElement ("CHAIN");
        ce->setAttribute ("id", chain.id);
        ce->setAttribute ("name", chain.name);
        ce->setAttribute ("strip", (int) chain.stripUid);

        for (auto device : chain.devices)
            ce->createNewChildElement ("DEVICE")->setAttribute ("uid", (int) device);
    }

    auto* cordsXml = xml.createNewChildElement ("CORDS");

    for (const auto& c : cords)
    {
        auto* e = cordsXml->createNewChildElement ("CORD");
        e->setAttribute ("src", (int) c.src);
        e->setAttribute ("dst", (int) c.dst);
        e->setAttribute ("midi", c.midi);
    }

    auto* surface = xml.createNewChildElement ("SURFACE");

    for (int slot = 0; slot < kPoolSize; ++slot)
    {
        const auto& k = knobs[(size_t) slot];

        if (k.targets.empty() && k.customName.isEmpty())
            continue;

        auto* e = surface->createNewChildElement ("KNOB");
        e->setAttribute ("slot", slot);
        e->setAttribute ("name", k.customName);

        for (const auto& t : k.targets)
        {
            auto* te = e->createNewChildElement ("TARGET");
            te->setAttribute ("uid", (int) t.uid);
            te->setAttribute ("index", t.paramIndex);
            te->setAttribute ("name", t.paramName);
            te->setAttribute ("min", (double) t.rangeMin);
            te->setAttribute ("max", (double) t.rangeMax);
        }
    }

    copyXmlToBinary (xml, destData);
}

UltrapatcherProcessor::SavedPatch UltrapatcherProcessor::parsePatch (const juce::XmlElement& xml)
{
    SavedPatch p;
    const int version = xml.getIntAttribute ("version", 1);

    auto readPoint = [] (const juce::XmlElement* e, juce::Point<float> fallback)
    {
        return e != nullptr ? juce::Point<float> ((float) e->getDoubleAttribute ("x", fallback.x),
                                                  (float) e->getDoubleAttribute ("y", fallback.y))
                            : fallback;
    };

    if (version >= 3)
    {
        p.nextUid = (NodeUid) juce::jmax ((int) kFirstPluginUid, xml.getIntAttribute ("nextUid", (int) kFirstPluginUid));

        if (auto* io = xml.getChildByName ("IO"))
        {
            p.inputPos  = readPoint (io->getChildByName ("INPUT"),  p.inputPos);
            p.outputPos = readPoint (io->getChildByName ("OUTPUT"), p.outputPos);
        }

        if (auto* nodesXml = xml.getChildByName ("NODES"))
        {
            for (auto* e : nodesXml->getChildWithTagNameIterator ("NODE"))
            {
                SavedNode sn;
                sn.uid = (NodeUid) e->getIntAttribute ("uid", 0);
                sn.isStrip = e->getStringAttribute ("kind") == "strip";
                auto* pluginXml = e->getChildByName ("PLUGIN");

                if (sn.uid < kFirstPluginUid)
                    continue;

                if (! sn.isStrip && (pluginXml == nullptr || ! sn.desc.loadFromXml (*pluginXml)))
                    continue;

                sn.state.fromBase64Encoding (e->getStringAttribute ("state"));
                sn.bypassed = e->getBoolAttribute ("bypassed", false);
                sn.pos = readPoint (e, { -1.0f, -1.0f });
                p.nodes.push_back (std::move (sn));
            }
        }

        p.mode = xml.getStringAttribute ("mode") == "rack" ? RackMode::rack : RackMode::patcher;

        if (auto* rackXml = xml.getChildByName ("RACK"))
        {
            p.nextChainId = juce::jmax (1, rackXml->getIntAttribute ("nextChainId", 1));

            for (auto* ce : rackXml->getChildWithTagNameIterator ("CHAIN"))
            {
                SavedChain chain;
                chain.id = ce->getIntAttribute ("id", 0);
                chain.name = ce->getStringAttribute ("name");
                chain.stripUid = (NodeUid) ce->getIntAttribute ("strip", 0);

                for (auto* de : ce->getChildWithTagNameIterator ("DEVICE"))
                    chain.devices.push_back ((NodeUid) de->getIntAttribute ("uid", 0));

                if (chain.id > 0)
                    p.chains.push_back (std::move (chain));
            }
        }

        if (auto* cordsXml = xml.getChildByName ("CORDS"))
            for (auto* e : cordsXml->getChildWithTagNameIterator ("CORD"))
                p.cords.push_back ({ (NodeUid) e->getIntAttribute ("src"), (NodeUid) e->getIntAttribute ("dst"),
                                     e->getBoolAttribute ("midi", false) });

        if (auto* surface = xml.getChildByName ("SURFACE"))
        {
            for (auto* e : surface->getChildWithTagNameIterator ("KNOB"))
            {
                SavedKnob k;
                k.slot = e->getIntAttribute ("slot", -1);
                k.name = e->getStringAttribute ("name");

                for (auto* te : e->getChildWithTagNameIterator ("TARGET"))
                {
                    MapTarget t;
                    t.uid = (NodeUid) te->getIntAttribute ("uid");
                    t.paramIndex = te->getIntAttribute ("index", -1);
                    t.paramName = te->getStringAttribute ("name");
                    t.rangeMin = (float) te->getDoubleAttribute ("min", 0.0);
                    t.rangeMax = (float) te->getDoubleAttribute ("max", 1.0);
                    k.targets.push_back (t);
                }

                if (juce::isPositiveAndBelow (k.slot, kPoolSize))
                    p.knobs.push_back (std::move (k));
            }
        }

        return p;
    }

    // Version 1 or 2: one hosted plugin plus optional pool mappings.
    p.migrated = true;

    if (auto* pluginXml = xml.getChildByName ("PLUGIN"))
    {
        SavedNode sn;
        sn.uid = kFirstPluginUid;

        if (sn.desc.loadFromXml (*pluginXml))
        {
            sn.state.fromBase64Encoding (xml.getStringAttribute ("pluginState"));
            sn.pos = { -1.0f, -1.0f };
            p.nodes.push_back (std::move (sn));
            p.nextUid = kFirstPluginUid + 1;

            if (auto* maps = xml.getChildByName ("MAPS"))
            {
                for (auto* m : maps->getChildWithTagNameIterator ("MAP"))
                {
                    SavedKnob k;
                    k.slot = m->getIntAttribute ("slot", -1);

                    MapTarget t;
                    t.uid = kFirstPluginUid;
                    t.paramIndex = m->getIntAttribute ("index", -1);
                    t.paramName = m->getStringAttribute ("name");
                    k.targets.push_back (t);

                    if (juce::isPositiveAndBelow (k.slot, kPoolSize))
                        p.knobs.push_back (std::move (k));
                }
            }
        }
    }

    return p;
}

void UltrapatcherProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr || ! xml->hasTagName ("Ultrapatcher"))
        return;

    juce::WeakReference<UltrapatcherProcessor> weak (this);

    juce::MessageManager::callAsync ([weak, patch = parsePatch (*xml)]
    {
        if (weak != nullptr)
            weak->restorePatch (patch);
    });
}

//==============================================================================
// Restoring: plugins load asynchronously, then cords and mappings are applied.
struct UltrapatcherProcessor::RestoreJob
{
    SavedPatch patch;
    std::map<NodeUid, std::unique_ptr<juce::AudioPluginInstance>> instances;
    int remaining = 0;
    int generation = 0;
};

void UltrapatcherProcessor::clearPatch (bool addDefaultCords)
{
    beginStructureChange (0);

    for (int i = 0; i < kPoolSize; ++i)
    {
        knobs[(size_t) i] = {};
        pool[(size_t) i]->setMappedName ({});
    }

    {
        const juce::SpinLock::ScopedLockType sl (mappingLock);

        for (auto& s : slots)
            s = {};
    }

    for (auto& n : nodes)
        graph.removeNode (n.graphNode->nodeID, UpdateKind::none);

    nodes.clear();

    for (const auto& conn : graph.getConnections())
        graph.removeConnection (conn, UpdateKind::none);

    cords.clear();
    mode = RackMode::patcher;
    chains.clear();
    nextChainId = 1;
    activeChainId = 0;

    if (addDefaultCords)
        createDefaultCords();

    endStructureChange();
    patchChanged();
}

void UltrapatcherProcessor::restorePatch (SavedPatch patch)
{
    lastError.clear();
    clearPatch (false);

    inputPos = patch.inputPos;
    outputPos = patch.outputPos;
    nextUid = juce::jmax (kFirstPluginUid, patch.nextUid);

    const int generation = ++restoreGeneration;

    auto job = std::make_shared<RestoreJob>();
    job->patch = std::move (patch);
    job->remaining = (int) std::count_if (job->patch.nodes.begin(), job->patch.nodes.end(),
                                          [] (const SavedNode& n) { return ! n.isStrip; });
    job->generation = generation;

    if (job->remaining == 0)
    {
        finishRestore (job->patch, job->instances);
        return;
    }

    const double sr = getSampleRate() > 0 ? getSampleRate() : 44100.0;
    const int    bs = getBlockSize()  > 0 ? getBlockSize()  : 512;

    juce::WeakReference<UltrapatcherProcessor> weak (this);

    for (const auto& sn : job->patch.nodes)
    {
        if (sn.isStrip)
            continue;

        formatManager.createPluginInstanceAsync (sn.desc, sr, bs,
            [weak, job, uid = sn.uid] (std::unique_ptr<juce::AudioPluginInstance> instance, const juce::String&)
            {
                if (weak == nullptr || weak->restoreGeneration != job->generation)
                    return;

                job->instances[uid] = std::move (instance);

                if (--job->remaining == 0)
                    weak->finishRestore (job->patch, job->instances);
            });
    }
}

void UltrapatcherProcessor::finishRestore (SavedPatch& patch,
                                           std::map<NodeUid, std::unique_ptr<juce::AudioPluginInstance>>& instances)
{
    if (patch.migrated)
        createDefaultCords();

    int missing = 0;

    for (const auto& sn : patch.nodes)
    {
        if (sn.isStrip)
        {
            installStrip (sn.uid, sn.state);
            continue;
        }

        auto it = instances.find (sn.uid);

        if (it == instances.end() || it->second == nullptr)
        {
            ++missing;
            continue;
        }

        auto instance = std::move (it->second);

        if (sn.state.getSize() > 0)
            instance->setStateInformation (sn.state.getData(), (int) sn.state.getSize());

        installNode (std::move (instance), sn.desc, sn.uid, sn.pos, sn.bypassed, patch.migrated);
    }

    if (patch.mode == RackMode::rack)
    {
        // The rack regenerates its own cords. Keep only chains whose mixer
        // survived, and only devices that loaded.
        mode = RackMode::rack;
        nextChainId = juce::jmax (1, patch.nextChainId);

        for (const auto& sc : patch.chains)
        {
            const auto* strip = findNode (sc.stripUid);

            if (strip == nullptr || ! strip->isStrip)
                continue;

            ChainRecord chain;
            chain.id = sc.id;
            chain.name = sc.name.isNotEmpty() ? sc.name : "Chain " + juce::String (sc.id);
            chain.stripUid = sc.stripUid;

            for (auto device : sc.devices)
                if (findNode (device) != nullptr)
                    chain.devices.push_back (device);

            nextChainId = juce::jmax (nextChainId, chain.id + 1);
            chains.push_back (std::move (chain));
        }

        activeChainId = chains.empty() ? 0 : chains.front().id;
        rebuildRackWiring();
    }
    else
    {
        for (const auto& c : patch.cords)
            addCordInternal (c);
    }

    // Mappings: match by uid, then by parameter name, then by index.
    for (const auto& k : patch.knobs)
    {
        knobs[(size_t) k.slot].customName = k.name;

        for (const auto& t : k.targets)
        {
            auto* proc = getNodeProcessor (t.uid);

            if (proc == nullptr)
                continue;

            const auto& params = proc->getParameters();
            int index = -1;

            if (juce::isPositiveAndBelow (t.paramIndex, params.size()) && params[t.paramIndex]->getName (64) == t.paramName)
                index = t.paramIndex;
            else
                for (int i = 0; i < params.size(); ++i)
                    if (params[i]->getName (64) == t.paramName) { index = i; break; }

            if (index >= 0 && addTarget (k.slot, t.uid, index))
                setTargetRange (k.slot, (int) knobs[(size_t) k.slot].targets.size() - 1, t.rangeMin, t.rangeMax);
        }

        refreshSlotName (k.slot);
    }

    lastError = missing > 0 ? juce::String (missing) + (missing == 1 ? " plugin" : " plugins") + " could not be loaded"
                            : juce::String();
    patchChanged();
}
