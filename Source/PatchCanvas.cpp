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

    if (selectedNode != 0 && infoFor (selectedNode) == nullptr)
        selectedNode = 0;

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

void PatchCanvas::select (NodeUid node, int cordIndex)
{
    selectedNode = node;
    selectedCord = cordIndex;
    repaint();

    if (onSelectionChanged)
        onSelectionChanged();
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
        const bool selected = c == selectedCord, hinted = c == insertHint;
        auto colour = cord.midi ? kMidiColour : kAudioColour;

        if (hinted)
            colour = juce::Colours::white;

        g.setColour (colour.withAlpha (selected || hinted ? 1.0f : 0.8f));
        g.strokePath (path, juce::PathStrokeType ((cord.midi ? 1.8f : 2.6f) + (selected || hinted ? 1.6f : 0.0f)));
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
        const bool selected = i.uid == selectedNode;
        const float alpha = i.bypassed ? 0.55f : 1.0f;

        g.setColour (juce::Colour (i.isIO ? 0xff23303f : 0xff2b2f36).withAlpha (alpha));
        g.fillRoundedRectangle (b, 7.0f);
        g.setColour ((selected ? juce::Colours::white : juce::Colour (0xff4a5059)).withAlpha (selected ? 0.95f : alpha));
        g.drawRoundedRectangle (b, 7.0f, selected ? 2.0f : 1.0f);

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
    grabKeyboardFocus();
    const auto p = e.position;

    if (e.mods.isPopupMenu())
    {
        if (auto node = hitNode (p))
        {
            select (node, -1);
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
        select (node, -1);
        dragMode = DragMode::node;
        dragUid = node;
        dragOffset = p - boundsOf (*infoFor (node)).getPosition();
        dragStartedUnconnected = ! nodeIsConnected (node) && node != kInputUid && node != kOutputUid;
        return;
    }

    if (auto cord = hitCord (p, 8.0f); cord >= 0)
    {
        select (0, cord);
        return;
    }

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
    else if (dragMode == DragMode::node)
    {
        const auto target = (p - dragOffset);
        proc.setNodePosition (dragUid, { juce::jlimit (0.0f, juce::jmax (0.0f, (float) getWidth() - kNodeW), target.x),
                                         juce::jlimit (0.0f, juce::jmax (0.0f, (float) getHeight() - kNodeH), target.y) });
        infos = proc.getPatchNodes();

        // Dragging an unconnected node over a cord previews the splice.
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

    if (dragMode == DragMode::node)
    {
        const auto uid = dragUid;
        const bool splice = dragStartedUnconnected && insertHint >= 0 && insertHint < (int) cords.size();
        const Cord target = splice ? cords[(size_t) insertHint] : Cord();

        dragMode = DragMode::none;
        dragUid = 0;
        insertHint = -1;

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

        if (selectedNode != 0 && selectedNode != kInputUid && selectedNode != kOutputUid)
        {
            const auto node = selectedNode;
            selectedNode = 0;
            proc.removeNode (node);

            if (onSelectionChanged)
                onSelectionChanged();

            return true;
        }
    }

    return false;
}

//==============================================================================
void PatchCanvas::showNodeMenu (NodeUid uid)
{
    const auto* info = infoFor (uid);

    if (info == nullptr)
        return;

    juce::PopupMenu menu;

    if (! info->isIO)
    {
        menu.addItem (1, "Open plugin window");
        menu.addItem (2, "Bypass", true, info->bypassed);
        menu.addSeparator();
        menu.addItem (3, "Remove");
    }
    else
    {
        menu.addItem (4, "Input and Output stay in the patch", false);
    }

    juce::Component::SafePointer<PatchCanvas> safeThis (this);
    const bool bypassed = info->bypassed;

    menu.showMenuAsync (juce::PopupMenu::Options(), [safeThis, uid, bypassed] (int result)
    {
        if (safeThis == nullptr)
            return;

        if (result == 1 && safeThis->onOpenNode)  safeThis->onOpenNode (uid);
        else if (result == 2)                      safeThis->proc.setNodeBypassed (uid, ! bypassed);
        else if (result == 3)                      safeThis->proc.removeNode (uid);
    });
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
