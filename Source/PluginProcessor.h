#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "Settings.h"

// Milestone 2: hosts one third-party plugin and exposes a fixed pool of generic
// DAW parameters ("Param 1".."Param 128") that can be mapped to the hosted
// plugin's parameters. Surface controls bind to pool slots, never directly to
// hosted parameters, so automation, MIDI learn and project reload behave the
// same everywhere.
class UltrapatcherProcessor : public juce::AudioProcessor,
                              public juce::ChangeBroadcaster,
                              private juce::Timer
{
public:
    static constexpr int kPoolSize = 128;

    // A saved or requested link between a pool slot and a hosted parameter.
    struct SlotMap
    {
        int slot = 0;
        int hostedIndex = -1;
        juce::String hostedName;
    };

    struct HostedParamInfo
    {
        int index = 0;
        juce::String name;
    };

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

    // Hosting. Call from any thread; the work happens on the message thread.
    void loadPlugin (const juce::PluginDescription&,
                     const juce::MemoryBlock& state = {},
                     const std::vector<SlotMap>& maps = {});
    void unloadPlugin();
    juce::AudioPluginInstance* getHostedPlugin() const;
    juce::String getHostedName() const { return hostedDescription.name; }

    // True while the hosted plugin is about to be replaced. Editors holding
    // a hosted plugin's GUI must destroy it when they see this flag.
    bool isHostedChanging() const { return hostedChanging.load(); }
    juce::String getLastError() const { return lastError; }

    // Incremented every time the hosted plugin is replaced or removed.
    int getHostedGeneration() const { return hostedGeneration.load(); }

    // Parameter pool (message thread).
    juce::RangedAudioParameter* getPoolParameter (int slot) const;
    juce::Array<HostedParamInfo> getHostedParameterList() const;
    bool mapSlot (int slot, int hostedParamIndex);
    void unmapSlot (int slot);
    int getSlotMapping (int slot) const;      // hosted parameter index, or -1
    juce::String getSlotName (int slot) const; // hosted parameter name, or empty

    // Plugin scanning (background thread). The normal scan covers each format's
    // default locations plus the custom folders; customFoldersOnly skips the defaults.
    void startScan (bool customFoldersOnly = false);
    bool isScanning() const { return scanning.load(); }

    // Load a plugin by file path (for example a .vst3 picked or dropped by the
    // user). Any thread. Also adds it to the known-plugin list.
    void loadPluginFromFile (const juce::File&);

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

    class ScanThread;
    class PoolParameter;

    // Per-slot sync state. Guarded by mappingLock: the message thread locks
    // it, the audio thread only try-locks it and skips the block if busy.
    struct SlotState
    {
        int hostedIndex = -1;
        float lastPoolApplied = 0.0f;  // last pool value pushed to the hosted plugin
        float lastHostedSeen  = 0.0f;  // last hosted value seen by either direction
    };

    void doLoad (const juce::PluginDescription&, const juce::MemoryBlock&, const std::vector<SlotMap>&);
    void doUnload();
    void detachHostedNode();
    void rebuildConnections();
    void scanFinished();
    void doLoadFromFile (const juce::File&);
    void savePluginList();
    void saveSettings();
    void timerCallback() override;
    void applyPoolToHosted();
    juce::File getSupportDirectory() const;

    UltrapatcherSettings settings;

    juce::AudioProcessorGraph graph;
    Node::Ptr audioInNode, audioOutNode, midiInNode, midiOutNode, hostedNode;
    juce::PluginDescription hostedDescription;

    std::array<PoolParameter*, kPoolSize> pool {};
    std::array<SlotState, kPoolSize> slots;
    juce::SpinLock mappingLock;

    std::atomic<bool> hostedChanging { false };
    std::atomic<int> hostedGeneration { 0 };
    std::atomic<bool> scanning { false };
    juce::String lastError;
    std::unique_ptr<ScanThread> scanThread;

    JUCE_DECLARE_WEAK_REFERENCEABLE (UltrapatcherProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UltrapatcherProcessor)
};
