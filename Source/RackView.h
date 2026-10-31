#pragma once

#include "PluginProcessor.h"

// Rack mode view: one row per chain, each with a mixer header (name, mute,
// solo, volume, pan) and its devices as cards, in signal order.
//  - Click a card to select it, double-click to open the plugin window.
//  - Click a card's dot to bypass it.
//  - Drag a card to reorder it, or onto another chain to move it there.
//  - The "+" slot adds the plugin selected in the browser to that chain.
class RackView : public juce::Component
{
public:
    explicit RackView (UltrapatcherProcessor&);
    ~RackView() override;

    void refresh();
    void releaseRows();   // must run before nodes are destroyed (rows hold parameter attachments)

    NodeUid getSelectedDevice() const { return selectedDevice; }

    std::function<void()> onSelectionChanged;
    std::function<void (NodeUid)> onOpenDevice;
    std::function<void (int chainId)> onAddToChain;
    std::function<void()> onContextMenu;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;

    static constexpr int kTopBar = 44, kRowH = 88, kHeaderW = 300;
    static constexpr int kCardW = 140, kCardH = 62, kCardGap = 8, kPlusW = 44;

    // Layout helpers, public so they can be tested.
    juce::Rectangle<int> rowRect (int chainIndex) const;
    juce::Rectangle<int> cardRect (int chainIndex, int deviceIndex) const;
    juce::Rectangle<int> plusRect (int chainIndex) const;

private:
    class ChainRow;

    struct CardHit { int chain = -1, device = -1; bool power = false; };

    int cardWidth (int chainIndex) const;
    const PatchNodeInfo* infoFor (NodeUid) const;
    CardHit hitCard (juce::Point<int>) const;
    int hitPlus (juce::Point<int>) const;
    int rowAt (int y) const;
    void select (NodeUid device, int chainId);
    void showDeviceMenu (NodeUid);
    void updateDropTarget (juce::Point<int>);
    void clampScroll();

    UltrapatcherProcessor& proc;
    juce::TextButton addChainButton { "Add chain" };
    juce::OwnedArray<ChainRow> rows;
    std::vector<int> rowIds;
    std::vector<ChainInfo> chains;
    juce::Array<PatchNodeInfo> infos;

    NodeUid selectedDevice = 0;
    int scrollY = 0;

    NodeUid dragUid = 0;
    bool dragging = false;
    juce::Point<int> dragStartMouse, dragPos;
    int dropChain = -1, dropIndex = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RackView)
};
