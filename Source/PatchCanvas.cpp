#include "PatchCanvas.h"

namespace
{
    const juce::Colour kAudioColour (0xff4aa3ff);
    const juce::Colour kMidiColour  (0xffe8a33d);
    constexpr float kPortRadius = 6.0f;
}

PatchCanvas::PatchCanvas (UltrapatcherProcessor& p) : proc (p)
{
    setWantsKeyboardFocus (true);
    refresh();
}

void PatchCanvas::refresh()
{
    infos = proc.getPatchNodes();
    cords = proc.getCords();

    for (auto it = selected.begin(); it != selected.end();)
        it = infoFor (*it) == nullptr ? selected.erase (it) : std::next (it);

    if (primary != 0 && selected.count (primary) == 0)
        primary = selected.empty() ? 0 : *selected.begin();

    if (selectedCord >= (int) cords.size())
        selectedCord = -1;

    repaint();
}

//==============================================================================
const PatchNodeInfo* PatchCanvas::infoFor (NodeUid uid) const
{
    for (const auto& i : infos)
        if (i.uid == uid)
            return &i;

    return nullptr;
}

// A negative x means "right-aligned" (the Output node until it is moved).
// Positions are only clamped for display, so resizing never rewrites a layout.
juce::Rectangle<float> PatchCanvas::boundsOf (const PatchNodeInfo& i) const
{
    const float maxX = juce::jmax (0.0f, (float) getWidth() - kNodeW);
    const float maxY = juce::jmax (0.0f, (float) getHeight() - kNodeH);
    const float x = i.pos.x < 0.0f ? juce::jmax (0.0f, (float) getWidth() - kNodeW - 20.0f) : i.pos.x;

    return { juce::jmin (x, maxX), juce::jmin (i.pos.y, maxY), kNodeW, kNodeH };
}

bool PatchCanvas::hasPort (const PatchNodeInfo& i, bool midi, bool output) const
{
    if (midi)
        return output ? i.producesMidi : i.acceptsMidi;

    return output ? i.audioOuts > 0 : i.audioIns > 0;
}

juce::Point<float> PatchCanvas::portPos (const PatchNodeInfo& i, bool midi, bool output) const
{
    const auto b = boundsOf (i);
    return { output ? b.getRight() : b.getX(), b.getY() + (midi ? 66.0f : 50.0f) };
}

PatchCanvas::PortHit PatchCanvas::hitPort (juce::Point<float> p) const
{
    for (int n = infos.size(); --n >= 0;)
    {
        const auto& i = infos.getReference (n);

        for (bool midi : { false, true })
            for (bool output : { false, true })
            {
                if (! hasPort (i, midi, output))
                    continue;

                const auto pos = portPos (i, midi, output);

                if (pos.getDistanceFrom (p) <= kPortRadius + 4.0f)
                    return { true, i.uid, midi, output, pos };
            }
    }

    return {};
}

NodeUid PatchCanvas::hitNode (juce::Point<float> p) const
{
    for (int n = infos.size(); --n >= 0;)
        if (boundsOf (infos.getReference (n)).contains (p))
            return infos.getReference (n).uid;

    return 0;
}

juce::Path PatchCanvas::cordPath (juce::Point<float> from, juce::Point<float> to) const
{
    const float dx = juce::jmax (40.0f, std::abs (to.x - from.x) * 0.5f);
    juce::Path path;
    path.startNewSubPath (from);
    path.cubicTo (from.translated (dx, 0.0f), to.translated (-dx, 0.0f), to);
    return path;
}

int PatchCanvas::hitCord (juce::Point<float> p, float tolerance) const
{
    int best = -1;
    float bestDistance = tolerance;

    for (int c = 0; c < (int) cords.size(); ++c)
    {
        const auto* s = infoFor (cords[(size_t) c].src);
        const auto* d = infoFor (cords[(size_t) c].dst);

        if (s == nullptr || d == nullptr)
            continue;

        const auto a = portPos (*s, cords[(size_t) c].midi, true);
        const auto b = portPos (*d, cords[(size_t) c].midi, false);
        const float dx = juce::jmax (40.0f, std::abs (b.x - a.x) * 0.5f);
        const auto c1 = a.translated (dx, 0.0f), c2 = b.translated (-dx, 0.0f);

        juce::Point<float> prev = a;

        for (int step = 1; step <= 24; ++step)
        {
            const float t = (float) step / 24.0f, u = 1.0f - t;
            const juce::Point<float> q (u * u * u * a.x + 3 * u * u * t * c1.x + 3 * u * t * t * c2.x + t * t * t * b.x,
                                        u * u * u * a.y + 3 * u * u * t * c1.y + 3 * u * t * t * c2.y + t * t * t * b.y);

            const juce::Line<float> seg (prev, q);
            juce::Point<float> nearest;
            const float dist = seg.getDistanceFromPoint (p, nearest);

            if (dist < bestDistance)
            {
                bestDistance = dist;
                best = c;
            }

            prev = q;
        }
    }

    return best;
}

bool PatchCanvas::nodeIsConnected (NodeUid uid) const
{
    for (const auto& c : cords)
        if (c.src == uid || c.dst == uid)
            return true;

    return false;
}

void PatchCanvas::selectionChanged()
{
    repaint();

    if (onSelectionChanged)
        onSelectionChanged();
}

void PatchCanvas::select (NodeUid node, int cordIndex)
{
    selected.clear();

    if (node != 0)
        selected.insert (node);

    primary = node;
    selectedCord = cordIndex;
    selectionChanged();
}

std::vector<NodeUid> PatchCanvas::getSelectedNodes() const
{
    return { selected.begin(), selected.end() };
}

void PatchCanvas::selectAll()
{
    selected.clear();

    for (const auto& i : infos)
        selected.insert (i.uid);

    primary = selected.empty() ? 0 : *selected.begin();
    selectedCord = -1;
    selectionChanged();
}

void PatchCanvas::clearSelection()
{
    select (0, -1);
}

// Selects every node the rectangle touches. Additive keeps the selection that
// is already there.
void PatchCanvas::selectInRect (juce::Rectangle<float> area, bool additive)
{
    if (! additive)
        selected.clear();

    for (const auto& i : infos)
        if (area.intersects (boundsOf (i)))
            selected.insert (i.uid);

    primary = selected.empty() ? 0 : *selected.rbegin();
    selectedCord = -1;
    selectionChanged();
}

void PatchCanvas::deleteSelection()
{
    std::vector<NodeUid> doomed;

    for (auto uid : selected)
        if (uid != kInputUid && uid != kOutputUid)
            doomed.push_back (uid);

    selected.clear();
    primary = 0;
    selectionChanged();

    for (auto uid : doomed)
        proc.removeNode (uid);
}

juce::Rectangle<float> PatchCanvas::marqueeRect() const
{
    return juce::Rectangle<float> (marqueeStart, marqueeEnd);
}

//==============================================================================
void PatchCanvas::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff17191d));

    // faint grid
    g.setColour (juce::Colour (0x10ffffff));
    for (float x = 0.0f; x < (float) getWidth(); x += 32.0f) g.drawVerticalLine ((int) x, 0.0f, (float) getHeight());
    for (float y = 0.0f; y < (float) getHeight(); y += 32.0f) g.drawHorizontalLine ((int) y, 0.0f, (float) getWidth());

    // cords
    for (int c = 0; c < (int) cords.size(); ++c)
    {
        const auto& cord = cords[(size_t) c];
        const auto* s = infoFor (cord.src);
        const auto* d = infoFor (cord.dst);

        if (s == nullptr || d == nullptr)
            continue;

        const auto path = cordPath (portPos (*s, cord.midi, true), portPos (*d, cord.midi, false));
        const bool cordSelected = c == selectedCord, hinted = c == insertHint;
        auto colour = cord.midi ? kMidiColour : kAudioColour;

        if (hinted)
            colour = juce::Colours::white;

        g.setColour (colour.withAlpha (cordSelected || hinted ? 1.0f : 0.8f));
        g.strokePath (path, juce::PathStrokeType ((cord.midi ? 1.8f : 2.6f) + (cordSelected || hinted ? 1.6f : 0.0f)));
    }

    // cord being dragged
    if (dragMode == DragMode::cord && cordFrom.valid)
    {
        juce::Path path = cordPath (cordFrom.pos, cordEnd);
        juce::Path dashed;
        const float dashes[] = { 6.0f, 5.0f };
        juce::PathStrokeType (2.0f).createDashedStroke (dashed, path, dashes, 2);
        g.setColour ((cordFrom.midi ? kMidiColour : kAudioColour).withAlpha (0.9f));
        g.fillPath (dashed);
    }

    // nodes
    for (const auto& i : infos)
    {
        const auto b = boundsOf (i);
        const bool isSelected = selected.count (i.uid) > 0;
        const float alpha = i.bypassed ? 0.55f : 1.0f;

        g.setColour (juce::Colour (i.isIO ? 0xff23303f : 0xff2b2f36).withAlpha (alpha));
        g.fillRoundedRectangle (b, 7.0f);
        g.setColour ((isSelected ? juce::Colours::white : juce::Colour (0xff4a5059)).withAlpha (isSelected ? 0.95f : alpha));
        g.drawRoundedRectangle (b, 7.0f, isSelected ? 2.0f : 1.0f);

        auto text = b.reduced (12.0f, 8.0f);
        g.setColour (juce::Colours::white.withAlpha (alpha));
        g.setFont (juce::Font (juce::FontOptions (13.5f, juce::Font::bold)));
        g.drawText (i.name, text.removeFromTop (16.0f), juce::Justification::centredLeft, true);

        g.setColour (juce::Colours::grey.withAlpha (alpha));
        g.setFont (juce::Font (juce::FontOptions (11.0f)));
        g.drawText (i.bypassed ? "bypassed" : i.subtitle, text.removeFromTop (14.0f), juce::Justification::centredLeft, true);

        // port labels
        g.setFont (juce::Font (juce::FontOptions (10.0f)));
        g.setColour (juce::Colours::grey.withAlpha (0.8f));

        for (bool midi : { false, true })
        {
            if (hasPort (i, midi, false))
                g.drawText (midi ? "midi" : "audio",
                            juce::Rectangle<float> (b.getX() + 10.0f, portPos (i, midi, false).y - 6.0f, 40.0f, 12.0f),
                            juce::Justification::centredLeft);

            if (hasPort (i, midi, true))
                g.drawText (midi ? "midi" : "audio",
                            juce::Rectangle<float> (b.getRight() - 50.0f, portPos (i, midi, true).y - 6.0f, 40.0f, 12.0f),
                            juce::Justification::centredRight);
        }

        // ports
        for (bool midi : { false, true })
            for (bool output : { false, true })
            {
                if (! hasPort (i, midi, output))
                    continue;

                const auto pos = portPos (i, midi, output);
                const auto colour = midi ? kMidiColour : kAudioColour;
                bool compatible = false;

                if (dragMode == DragMode::cord && cordFrom.valid)
                    compatible = ! output && midi == cordFrom.midi && i.uid != cordFrom.uid;

                g.setColour (juce::Colour (0xff17191d));
                g.fillEllipse (pos.x - kPortRadius - 1.5f, pos.y - kPortRadius - 1.5f, (kPortRadius + 1.5f) * 2, (kPortRadius + 1.5f) * 2);
                g.setColour (compatible ? juce::Colours::white : colour);
                g.fillEllipse (pos.x - kPortRadius, pos.y - kPortRadius, kPortRadius * 2, kPortRadius * 2);
            }
    }

    if (dragMode == DragMode::marquee)
    {
        const auto r = marqueeRect();
        g.setColour (juce::Colours::dodgerblue.withAlpha (0.14f));
        g.fillRect (r);
        g.setColour (juce::Colours::dodgerblue.withAlpha (0.9f));
        g.drawRect (r, 1.0f);
    }

    if (infos.size() <= 2)
    {
        g.setColour (juce::Colours::grey);
        g.setFont (juce::Font (juce::FontOptions (14.0f)));
        g.drawFittedText ("Pick a plugin in the list and press Add, or drop a .vst3 here.\n"
                          "Drag from a port to connect. Drop a node on a cord to splice it in.",
                          getLocalBounds().reduced (60), juce::Justification::centred, 3);
    }
}

//==============================================================================
void PatchCanvas::mouseDown (const juce::MouseEvent& e)
{
    if (isShowing())
        grabKeyboardFocus();
    const auto p = e.position;
    const bool additive = e.mods.isShiftDown() || e.mods.isCommandDown() || e.mods.isCtrlDown();

    if (e.mods.isPopupMenu())
    {
        if (auto node = hitNode (p))
        {
            if (selected.count (node) == 0)
                select (node, -1);
            else
                primary = node;

            showNodeMenu (node);
        }
        else if (auto cord = hitCord (p, 8.0f); cord >= 0)
        {
            select (0, cord);
            showCordMenu (cord);
        }
        else if (onContextMenu)
        {
            onContextMenu();
        }

        return;
    }

    dragMoved = false;
    dragAdditive = additive;

    if (auto port = hitPort (p); port.valid && port.output)
    {
        dragMode = DragMode::cord;
        cordFrom = port;
        cordEnd = p;
        select (0, -1);
        return;
    }

    if (auto node = hitNode (p))
    {
        dragWasSelected = selected.count (node) > 0;

        if (additive)
        {
            if (dragWasSelected)
            {
                selected.erase (node);
                primary = selected.empty() ? 0 : *selected.rbegin();
                selectedCord = -1;
                selectionChanged();
                return;   // toggled off: nothing to drag
            }

            selected.insert (node);
            primary = node;
            selectedCord = -1;
            selectionChanged();
        }
        else if (! dragWasSelected)
        {
            select (node, -1);
        }
        else
        {
            primary = node;
            selectedCord = -1;
            repaint();
        }

        dragMode = DragMode::node;
        dragUid = node;
        dragMouseStart = p;
        dragStart.clear();

        for (auto uid : selected)
            if (const auto* info = infoFor (uid))
                dragStart[uid] = boundsOf (*info).getPosition();

        dragStartedUnconnected = selected.size() == 1 && ! nodeIsConnected (node)
                                   && node != kInputUid && node != kOutputUid;
        return;
    }

    if (auto cord = hitCord (p, 8.0f); cord >= 0)
    {
        select (0, cord);
        return;
    }

    // Empty space: start a selection rectangle.
    marqueeBase = additive ? selected : std::set<NodeUid>();
    dragMode = DragMode::marquee;
    marqueeStart = marqueeEnd = p;

    if (! additive)
        select (0, -1);
}

void PatchCanvas::mouseDrag (const juce::MouseEvent& e)
{
    const auto p = e.position;

    if (dragMode == DragMode::cord)
    {
        cordEnd = p;
        repaint();
    }
    else if (dragMode == DragMode::marquee)
    {
        marqueeEnd = p;
        selected = marqueeBase;

        for (const auto& i : infos)
            if (marqueeRect().intersects (boundsOf (i)))
                selected.insert (i.uid);

        primary = selected.empty() ? 0 : *selected.rbegin();
        selectedCord = -1;
        selectionChanged();
    }
    else if (dragMode == DragMode::node)
    {
        if (! dragMoved && p.getDistanceFrom (dragMouseStart) < 3.0f)
            return;

        dragMoved = true;
        auto delta = p - dragMouseStart;

        // Keep the whole group inside the canvas.
        float minX = 1.0e9f, minY = 1.0e9f, maxX = -1.0e9f, maxY = -1.0e9f;

        for (const auto& [uid, start] : dragStart)
        {
            minX = juce::jmin (minX, start.x);  maxX = juce::jmax (maxX, start.x);
            minY = juce::jmin (minY, start.y);  maxY = juce::jmax (maxY, start.y);
        }

        delta.x = juce::jlimit (-minX, juce::jmax (-minX, (float) getWidth()  - kNodeW - maxX), delta.x);
        delta.y = juce::jlimit (-minY, juce::jmax (-minY, (float) getHeight() - kNodeH - maxY), delta.y);

        for (const auto& [uid, start] : dragStart)
            proc.setNodePosition (uid, start + delta);

        infos = proc.getPatchNodes();

        // Dragging a lone unconnected node over a cord previews the splice.
        insertHint = -1;

        if (dragStartedUnconnected)
            if (const auto* info = infoFor (dragUid))
                insertHint = hitCord (boundsOf (*info).getCentre(), 14.0f);

        repaint();
    }
}

void PatchCanvas::mouseUp (const juce::MouseEvent& e)
{
    const auto p = e.position;

    if (dragMode == DragMode::cord)
    {
        NodeUid targetUid = 0;
        const auto port = hitPort (p);

        if (port.valid && ! port.output && port.midi == cordFrom.midi)
            targetUid = port.uid;
        else if (auto node = hitNode (p))
            if (const auto* info = infoFor (node); info != nullptr && hasPort (*info, cordFrom.midi, false))
                targetUid = node;

        const auto from = cordFrom;
        dragMode = DragMode::none;
        cordFrom = {};

        if (targetUid != 0 && targetUid != from.uid)
            proc.addCord ({ from.uid, targetUid, from.midi });

        refresh();
        return;
    }

    if (dragMode == DragMode::marquee)
    {
        dragMode = DragMode::none;
        repaint();
        return;
    }

    if (dragMode == DragMode::node)
    {
        const auto uid = dragUid;
        const bool splice = dragMoved && dragStartedUnconnected && insertHint >= 0 && insertHint < (int) cords.size();
        const Cord target = splice ? cords[(size_t) insertHint] : Cord();
        const bool collapse = ! dragMoved && ! dragAdditive && dragWasSelected && selected.size() > 1;

        dragMode = DragMode::none;
        dragUid = 0;
        insertHint = -1;
        dragStart.clear();

        if (collapse)
            select (uid, -1);   // a plain click inside a group selects just that node

        if (splice)
            proc.insertNodeOnCord (uid, target);

        refresh();
    }
}

void PatchCanvas::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (auto node = hitNode (e.position); node != 0 && node != kInputUid && node != kOutputUid && onOpenNode)
        onOpenNode (node);
}

bool PatchCanvas::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        if (selectedCord >= 0 && selectedCord < (int) cords.size())
        {
            const auto cord = cords[(size_t) selectedCord];
            selectedCord = -1;
            proc.removeCord (cord);
            return true;
        }

        if (! selected.empty())
        {
            deleteSelection();
            return true;
        }
    }

    if (key == juce::KeyPress ('a', juce::ModifierKeys::commandModifier, 0)
        || key == juce::KeyPress ('a', juce::ModifierKeys::ctrlModifier, 0))
    {
        selectAll();
        return true;
    }

    if (key == juce::KeyPress::escapeKey)
    {
        clearSelection();
        return true;
    }

    return false;
}

//==============================================================================
void PatchCanvas::showNodeMenu (NodeUid)
{
    std::vector<NodeUid> targets;

    for (auto n : selected)
        if (n != kInputUid && n != kOutputUid)
            targets.push_back (n);

    juce::PopupMenu menu;

    if (targets.empty())
    {
        menu.addItem (4, "Input and Output stay in the patch", false);
    }
    else
    {
        bool allBypassed = true;

        for (auto n : targets)
            if (const auto* info = infoFor (n))
                allBypassed = allBypassed && info->bypassed;

        const auto count = juce::String (targets.size());
        menu.addItem (1, targets.size() == 1 ? "Open plugin window" : "Open " + count + " plugin windows");
        menu.addItem (2, "Bypass", true, allBypassed);
        menu.addSeparator();
        menu.addItem (3, targets.size() == 1 ? "Remove" : "Remove " + count + " nodes");

        juce::Component::SafePointer<PatchCanvas> safeThis (this);

        menu.showMenuAsync (juce::PopupMenu::Options(), [safeThis, targets, allBypassed] (int result)
        {
            if (safeThis == nullptr)
                return;

            if (result == 1 && safeThis->onOpenNode)
                for (auto n : targets)
                    safeThis->onOpenNode (n);
            else if (result == 2)
                for (auto n : targets)
                    safeThis->proc.setNodeBypassed (n, ! allBypassed);
            else if (result == 3)
                safeThis->deleteSelection();
        });

        return;
    }

    menu.showMenuAsync (juce::PopupMenu::Options(), nullptr);
}

void PatchCanvas::showCordMenu (int cordIndex)
{
    if (cordIndex < 0 || cordIndex >= (int) cords.size())
        return;

    const auto cord = cords[(size_t) cordIndex];

    juce::PopupMenu menu;
    menu.addItem (1, "Delete cord");

    juce::Component::SafePointer<PatchCanvas> safeThis (this);

    menu.showMenuAsync (juce::PopupMenu::Options(), [safeThis, cord] (int result)
    {
        if (safeThis != nullptr && result == 1)
            safeThis->proc.removeCord (cord);
    });
}
