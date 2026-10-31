#pragma once

#include <map>
#include <set>
#include "PluginProcessor.h"

// The Patcher canvas: nodes you drag around, cords you pull between ports.
//  - Drag from an output port to an input port to connect (audio or MIDI).
//  - Drop an unconnected node onto a cord to splice it in.
//  - Drag a rectangle on empty space to select several nodes. Shift, Ctrl or
//    Cmd-click adds or removes one. Dragging any selected node moves them all.
//  - Delete removes the selected cord or nodes. Removing a node bridges the
//    cords around it so signal keeps flowing.
//  - Ctrl or Cmd+A selects everything. Escape clears the selection.
//  - Double-click a node to open its plugin window.
class PatchCanvas : public juce::Component
{
public:
    explicit PatchCanvas (UltrapatcherProcessor&);

    NodeUid getSelectedNode() const { return primary; }          // the one clicked last
    std::vector<NodeUid> getSelectedNodes() const;               // every selected node
    bool isNodeSelected (NodeUid uid) const { return selected.count (uid) > 0; }

    void selectAll();
    void clearSelection();
    void selectInRect (juce::Rectangle<float> area, bool additive);   // canvas coordinates
    void deleteSelection();                                           // Input and Output stay

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
    void select (NodeUid node, int cordIndex);                 // select exactly one node or cord
    void selectionChanged();
    juce::Rectangle<float> marqueeRect() const;
    void showNodeMenu (NodeUid);
    void showCordMenu (int cordIndex);

    UltrapatcherProcessor& proc;
    juce::Array<PatchNodeInfo> infos;
    std::vector<Cord> cords;

    enum class DragMode { none, node, cord, marquee } dragMode = DragMode::none;
    NodeUid dragUid = 0;
    std::map<NodeUid, juce::Point<float>> dragStart;   // positions when a group drag began
    juce::Point<float> dragMouseStart;
    bool dragStartedUnconnected = false;
    bool dragMoved = false, dragWasSelected = false, dragAdditive = false;
    std::set<NodeUid> marqueeBase;                     // selection kept while adding with a marquee
    juce::Point<float> marqueeStart, marqueeEnd;
    PortHit cordFrom;
    juce::Point<float> cordEnd;
    int insertHint = -1;     // cord a dragged node would be spliced into

    std::set<NodeUid> selected;
    NodeUid primary = 0;
    int selectedCord = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PatchCanvas)
};
