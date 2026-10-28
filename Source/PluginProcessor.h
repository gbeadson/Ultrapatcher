#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

// Milestone 1: hosts one third-party plugin inside an AudioProcessorGraph.
// With nothing loaded, audio and MIDI pass straight through.
class UltrapatcherProcessor : public juce::AudioProcessor,
                              public juce::ChangeBroadcaster
{
public:
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
    void loadPlugin (const juce::PluginDescription&, const juce::MemoryBlock& state = {});
    void unloadPlugin();
    juce::AudioPluginInstance* getHostedPlugin() const;
    juce::String getHostedName() const { return hostedDescription.name; }

    // True while the hosted plugin is about to be replaced. Editors holding
    // a hosted plugin's GUI must destroy it when they see this flag.
    bool isHostedChanging() const { return hostedChanging.load(); }
    juce::String getLastError() const { return lastError; }

    // Plugin scanning (background thread).
    void startScan();
    bool isScanning() const { return scanning.load(); }

    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPlugins;

private:
    using Node = juce::AudioProcessorGraph::Node;

    class ScanThread;

    void doLoad (const juce::PluginDescription&, const juce::MemoryBlock&);
    void doUnload();
    void rebuildConnections();
    void scanFinished();
    juce::File getSupportDirectory() const;

    juce::AudioProcessorGraph graph;
    Node::Ptr audioInNode, audioOutNode, midiInNode, midiOutNode, hostedNode;
    juce::PluginDescription hostedDescription;

    std::atomic<bool> hostedChanging { false };
    std::atomic<bool> scanning { false };
    juce::String lastError;
    std::unique_ptr<ScanThread> scanThread;

    JUCE_DECLARE_WEAK_REFERENCEABLE (UltrapatcherProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UltrapatcherProcessor)
};
