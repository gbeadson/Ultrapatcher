#pragma once

#include "PluginProcessor.h"

// The Patcher canvas: nodes you drag around, cords you pull between ports.
//  - Drag from an output port to an input port to connect (audio or MIDI).
//  - Drop an unconnected node onto a cord to splice it in.
//  - Delete removes the selected cord or node. Removing a node bridges the
//    cords around it so signal keeps flowing.
//  - Double-click a node to open its plugin window.
class PatchCanvas : public juce::Component
{
public:
    explicit PatchCanvas (UltrapatcherProcessor&);

    NodeUid getSelectedNode() const { return selectedNode; }
    void refresh();   // re-read the patch from the processor and repaint

    std::function<void()> onSelectionChanged;
    std::function<void (NodeUid)> onOpenNode;
    std::function<void()> onContextMenu;   // right-click on empty canvas

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

    static constexpr float kNodeW = 150.0f, kNodeH = 80.0f;

private:
    struct PortHit
    {
        bool valid = false;
        NodeUid uid = 0;
        bool midi = false, output = false;
        juce::Point<float> pos;
    };

    const PatchNodeInfo* infoFor (NodeUid) const;
    juce::Rectangle<float> boundsOf (const PatchNodeInfo&) const;
    juce::Point<float> portPos (const PatchNodeInfo&, bool midi, bool output) const;
    bool hasPort (const PatchNodeInfo&, bool midi, bool output) const;
    PortHit hitPort (juce::Point<float>) const;
    NodeUid hitNode (juce::Point<float>) const;
    int hitCord (juce::Point<float>, float tolerance) const;
    juce::Path cordPath (juce::Point<float> from, juce::Point<float> to) const;
    bool nodeIsConnected (NodeUid) const;
    void select (NodeUid node, int cordIndex);
    void showNodeMenu (NodeUid);
    void showCordMenu (int cordIndex);

    UltrapatcherProcessor& proc;
    juce::Array<PatchNodeInfo> infos;
    std::vector<Cord> cords;

    enum class DragMode { none, node, cord } dragMode = DragMode::none;
    NodeUid dragUid = 0;
    juce::Point<float> dragOffset;
    bool dragStartedUnconnected = false;
    PortHit cordFrom;
    juce::Point<float> cordEnd;
    int insertHint = -1;     // cord a dragged node would be spliced into

    NodeUid selectedNode = 0;
    int selectedCord = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PatchCanvas)
};
