#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "Settings.h"
#include "ChainStrip.h"

class PoolParameter;

// Ids for patch nodes. 1 and 2 are the fixed Input and Output nodes.
using NodeUid = uint32_t;
constexpr NodeUid kInputUid = 1;
constexpr NodeUid kOutputUid = 2;
constexpr NodeUid kFirstPluginUid = 3;

// What the canvas needs to draw one node.
struct PatchNodeInfo
{
    NodeUid uid = 0;
    juce::String name, subtitle;
    bool isIO = false, bypassed = false;
    juce::Point<float> pos;
    int audioIns = 0, audioOuts = 0;
    bool acceptsMidi = false, producesMidi = false;
};

// A cable from one node's output to another node's input. Audio cords carry
// the stereo pair (like the Grid's stereo signals); MIDI cords carry notes and
// control messages.
struct Cord
{
    NodeUid src = 0, dst = 0;
    bool midi = false;

    bool operator== (const Cord& o) const noexcept { return src == o.src && dst == o.dst && midi == o.midi; }
};

// One parameter a knob drives. The knob's 0..1 maps to rangeMin..rangeMax,
// so a reversed range (min > max) inverts the control.
struct MapTarget
{
    NodeUid uid = 0;
    int paramIndex = -1;
    juce::String nodeName, paramName;
    float rangeMin = 0.0f, rangeMax = 1.0f;
};

// What the rack view needs to draw one chain.
struct ChainInfo
{
    int id = 0;
    juce::String name;
    NodeUid stripUid = 0;
    std::vector<NodeUid> devices;   // in signal order
};

// Patcher mode: any number of hosted plugins wired with cords. Eight Surface
// knobs (out of a pool of 128 DAW parameters) can each drive many hosted
// parameters, each with its own range. Learn mode maps by touch.
// Rack mode: the same plugins arranged as parallel chains of serial devices,
// each chain ending in a mixer strip (volume, pan, mute, solo). The rack
// generates the cords itself. Converting to Patcher mode keeps the graph.
enum class RackMode { patcher, rack };

class UltrapatcherProcessor : public juce::AudioProcessor,
                              public juce::ChangeBroadcaster,
                              private juce::Timer
{
public:
    static constexpr int kPoolSize = 128;
    static constexpr int kKnobs = 8;

    UltrapatcherProcessor();
    ~UltrapatcherProcessor() override;

    // AudioProcessor
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Ultrapatcher"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    //==========================================================================
    // Patch. Message thread, except where noted.
    juce::Array<PatchNodeInfo> getPatchNodes() const;   // Input, Output, then plugins
    std::vector<Cord> getCords() const { return cords; }
    int getPatchGeneration() const { return patchGeneration.load(); }

    void addPlugin (const juce::PluginDescription&, const juce::MemoryBlock& state = {}); // any thread
    void loadPluginFromFile (const juce::File&);                                          // any thread
    void removeNode (NodeUid);
    bool addCord (const Cord&);
    void removeCord (const Cord&);
    bool insertNodeOnCord (NodeUid, const Cord&);
    void setNodeBypassed (NodeUid, bool);
    void setNodePosition (NodeUid, juce::Point<float>);

    juce::AudioProcessor* getNodeProcessor (NodeUid) const;  // null for Input/Output
    juce::String getNodeName (NodeUid) const;

    // True while nodes are about to be removed. Editors holding a node's
    // plugin window must destroy it when they see this. A uid of 0 means all.
    bool isStructureChanging() const { return structureChanging.load(); }
    NodeUid getChangingUid() const { return changingUid.load(); }
    juce::String getLastError() const { return lastError; }

    //==========================================================================
    // Modes and racks (message thread).
    RackMode getMode() const { return mode; }
    void setMode (RackMode);      // Patcher to Rack clears the patch; Rack to Patcher keeps it

    std::vector<ChainInfo> getChains() const;
    int addChain();                                   // returns the new chain's id
    void removeChain (int chainId);
    void renameChain (int chainId, const juce::String&);
    void moveDevice (NodeUid, int chainId, int index);  // index counts devices left in the chain
    void setActiveChain (int chainId) { activeChainId = chainId; }
    int getActiveChain() const { return activeChainId; }
    ChainStripProcessor* getChainStrip (int chainId) const;
    void updateSoloSuppression();                     // also runs on the 30 Hz timer

    //==========================================================================
    // Knob mappings.
    juce::RangedAudioParameter* getPoolParameter (int slot) const;
    std::vector<MapTarget> getSlotTargets (int slot) const;
    juce::String getSlotDisplayName (int slot) const;
    void setSlotCustomName (int slot, const juce::String&);
    bool addTarget (int slot, NodeUid, int paramIndex);
    void removeTarget (int slot, int targetIndex);
    void setTargetRange (int slot, int targetIndex, float rangeMin, float rangeMax);
    void clearSlot (int slot);

    // Learn: move a parameter in a plugin window and it is mapped to the knob.
    void startLearn (int slot);
    void cancelLearn();
    int getLearnSlot() const { return learnSlot; }

    //==========================================================================
    // Plugin scanning (background thread).
    void startScan (bool customFoldersOnly = false);
    bool isScanning() const { return scanning.load(); }

    // Settings (message thread). Changes are saved immediately.
    juce::StringArray getScanFolders() const { return settings.scanFolders; }
    void addScanFolder (const juce::File&);
    void removeScanFolder (int index);
    bool isWindowAlwaysOnTop() const { return settings.windowAlwaysOnTop; }
    void setWindowAlwaysOnTop (bool);

    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPlugins;

private:
    using Node = juce::AudioProcessorGraph::Node;
    using Conn = juce::AudioProcessorGraph::Connection;

    class ScanThread;

    struct NodeRecord
    {
        NodeUid uid = 0;
        Node::Ptr graphNode;
        juce::PluginDescription desc;
        juce::String name;
        bool bypassed = false;
        bool isStrip = false;   // a rack chain's mixer, not a hosted plugin
        juce::Point<float> pos;
    };

    struct ChainRecord
    {
        int id = 0;
        juce::String name;
        NodeUid stripUid = 0;
        std::vector<NodeUid> devices;
    };

    // A patch as stored in the project, before plugins are loaded.
    struct SavedNode { NodeUid uid = 0; juce::PluginDescription desc; juce::MemoryBlock state; bool bypassed = false; bool isStrip = false; juce::Point<float> pos; };
    struct SavedChain { int id = 0; juce::String name; NodeUid stripUid = 0; std::vector<NodeUid> devices; };
    struct SavedKnob { int slot = 0; juce::String name; std::vector<MapTarget> targets; };
    struct SavedPatch
    {
        std::vector<SavedNode> nodes;
        std::vector<Cord> cords;
        std::vector<SavedKnob> knobs;
        juce::Point<float> inputPos { 24.0f, 40.0f }, outputPos { -1.0f, 40.0f };   // x < 0: right-aligned
        NodeUid nextUid = kFirstPluginUid;
        bool migrated = false;  // saved by an older version: wire automatically
        RackMode mode = RackMode::patcher;
        std::vector<SavedChain> chains;
        int nextChainId = 1;
    };
    struct RestoreJob;

    // Live mapping state, shared with the audio thread under mappingLock.
    struct LiveTarget { juce::AudioProcessorParameter* param = nullptr; float rangeMin = 0.0f, rangeMax = 1.0f; };
    struct SlotState
    {
        std::vector<LiveTarget> targets;
        bool twoWay = false;           // one identity target: hosted changes flow back to the knob
        float lastPoolApplied = 0.0f;
        float lastHostedSeen = 0.0f;
    };
    struct KnobMeta { juce::String customName; std::vector<MapTarget> targets; };

    // patch (patch.cpp)
    NodeRecord* findNode (NodeUid);
    const NodeRecord* findNode (NodeUid) const;
    int audioInputsOf (NodeUid) const;
    int audioOutputsOf (NodeUid) const;
    bool midiInOf (NodeUid) const;
    bool midiOutOf (NodeUid) const;
    juce::AudioProcessorGraph::NodeID graphIdFor (NodeUid, bool midi) const;
    std::vector<Conn> connectionsFor (const Cord&) const;
    bool cordIsValid (const Cord&, juce::String& why) const;
    bool addCordInternal (const Cord&);
    void removeCordInternal (const Cord&);
    NodeUid installNode (std::unique_ptr<juce::AudioProcessor>, const juce::PluginDescription&,
                         NodeUid fixedUid, juce::Point<float> pos, bool bypassed, bool autoWire);
    void autoWireNode (NodeUid);
    void doAddPlugin (const juce::PluginDescription&, const juce::MemoryBlock&);
    void doLoadFromFile (const juce::File&);
    void clearPatch (bool addDefaultCords);
    void restorePatch (SavedPatch);
    void finishRestore (SavedPatch&, std::map<NodeUid, std::unique_ptr<juce::AudioPluginInstance>>&);
    static SavedPatch parsePatch (const juce::XmlElement&);
    void patchChanged();
    void beginStructureChange (NodeUid);
    void endStructureChange();
    void setError (const juce::String&);
    void createDefaultCords();
    void removeNodeCore (NodeUid);

    // rack (RackEngine.cpp)
    ChainRecord* findChain (int chainId);
    ChainRecord* chainOfDevice (NodeUid);
    NodeUid installStrip (NodeUid fixedUid = 0, const juce::MemoryBlock& state = {});
    void installIntoChain (std::unique_ptr<juce::AudioProcessor>, const juce::PluginDescription&, int chainId);
    void rebuildRackWiring();
    void layoutRackAsPatch();

    // mapping (mapping.cpp)
    void rebuildSlot (int slot);
    void refreshSlotName (int slot);
    void dropTargetsForNode (NodeUid);
    void applyPoolToHosted();
    void timerCallback() override;
    void pollLearn();

    // scanning & settings (PluginProcessor.cpp)
    void scanFinished();
    void savePluginList();
    void saveSettings();
    juce::File getSupportDirectory() const;

    UltrapatcherSettings settings;

    juce::AudioProcessorGraph graph;
    Node::Ptr audioInNode, audioOutNode, midiInNode, midiOutNode;
    std::vector<NodeRecord> nodes;
    std::vector<Cord> cords;
    juce::Point<float> inputPos { 24.0f, 40.0f }, outputPos { -1.0f, 40.0f };   // x < 0: right-aligned
    NodeUid nextUid = kFirstPluginUid;

    RackMode mode = RackMode::patcher;
    std::vector<ChainRecord> chains;
    int nextChainId = 1;
    int activeChainId = 0;

    std::array<PoolParameter*, kPoolSize> pool {};
    std::array<KnobMeta, kPoolSize> knobs;
    std::array<SlotState, kPoolSize> slots;
    juce::SpinLock mappingLock;

    int learnSlot = -1;
    struct LearnSnap { NodeUid uid; int index; float value; };
    std::vector<LearnSnap> learnBase;

    std::atomic<int> patchGeneration { 0 };
    std::atomic<bool> structureChanging { false };
    std::atomic<NodeUid> changingUid { 0 };
    std::atomic<bool> scanning { false };
    int restoreGeneration = 0;
    juce::String lastError;
    std::unique_ptr<ScanThread> scanThread;

    JUCE_DECLARE_WEAK_REFERENCEABLE (UltrapatcherProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UltrapatcherProcessor)
};
