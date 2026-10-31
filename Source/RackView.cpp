#include "RackView.h"

//==============================================================================
// The mixer header of one chain. Its controls bind straight to the chain
// strip's parameters, so they stay in step with Surface knob mappings.
class RackView::ChainRow : public juce::Component
{
public:
    ChainRow (UltrapatcherProcessor& p, int chainId) : proc (p), id (chainId)
    {
        setInterceptsMouseClicks (false, true);   // clicks on the cards go to the RackView

        for (auto* c : std::initializer_list<juce::Component*> { &nameLabel, &muteButton, &soloButton, &removeButton,
                                                                  &volSlider, &panSlider })
            addAndMakeVisible (c);

        nameLabel.setEditable (false, true);
        nameLabel.setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)));
        nameLabel.onTextChange = [this] { proc.renameChain (id, nameLabel.getText()); };

        muteButton.setClickingTogglesState (true);
        soloButton.setClickingTogglesState (true);
        muteButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffe5a000));
        soloButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff4c8dff));
        muteButton.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
        soloButton.setColour (juce::TextButton::textColourOnId, juce::Colours::black);

        for (auto* s : { &volSlider, &panSlider })
        {
            s->setSliderStyle (juce::Slider::LinearHorizontal);
            s->setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 20);
        }

        removeButton.onClick = [this]
        {
            // Removing the chain destroys this row, so do it after the click returns.
            juce::WeakReference<UltrapatcherProcessor> weak (&proc);
            const int idToRemove = id;

            juce::MessageManager::callAsync ([weak, idToRemove]
            {
                if (weak != nullptr)
                    weak->removeChain (idToRemove);
            });
        };

        if (auto* strip = proc.getChainStrip (id))
        {
            volAttachment  = std::make_unique<juce::SliderParameterAttachment> (*strip->volume, volSlider, nullptr);
            panAttachment  = std::make_unique<juce::SliderParameterAttachment> (*strip->pan, panSlider, nullptr);
            muteAttachment = std::make_unique<juce::ButtonParameterAttachment> (*strip->mute, muteButton, nullptr);
            soloAttachment = std::make_unique<juce::ButtonParameterAttachment> (*strip->solo, soloButton, nullptr);
        }
    }

    void setName (const juce::String& name)
    {
        if (! nameLabel.isBeingEdited() && nameLabel.getText() != name)
            nameLabel.setText (name, juce::dontSendNotification);
    }

    void resized() override
    {
        auto area = getLocalBounds().withWidth (kHeaderW).reduced (10, 10);

        auto top = area.removeFromTop (26);
        removeButton.setBounds (top.removeFromRight (26));
        top.removeFromRight (6);
        soloButton.setBounds (top.removeFromRight (28));
        top.removeFromRight (4);
        muteButton.setBounds (top.removeFromRight (28));
        top.removeFromRight (8);
        nameLabel.setBounds (top);

        area.removeFromTop (14);
        auto bottom = area.removeFromTop (26);
        volSlider.setBounds (bottom.removeFromLeft (150));
        bottom.removeFromLeft (8);
        panSlider.setBounds (bottom);
    }

private:
    UltrapatcherProcessor& proc;
    int id;

    juce::Label nameLabel;
    juce::TextButton muteButton { "M" }, soloButton { "S" }, removeButton { "x" };
    juce::Slider volSlider, panSlider;
    std::unique_ptr<juce::SliderParameterAttachment> volAttachment, panAttachment;
    std::unique_ptr<juce::ButtonParameterAttachment> muteAttachment, soloAttachment;
};

//==============================================================================
RackView::RackView (UltrapatcherProcessor& p) : proc (p)
{
    setWantsKeyboardFocus (true);
    addAndMakeVisible (addChainButton);
    addChainButton.onClick = [this] { proc.addChain(); };
    refresh();
}

RackView::~RackView() = default;

void RackView::releaseRows()
{
    rows.clear();
    rowIds.clear();
    dragUid = 0;
    dragging = false;
}

const PatchNodeInfo* RackView::infoFor (NodeUid uid) const
{
    for (const auto& i : infos)
        if (i.uid == uid)
            return &i;

    return nullptr;
}

void RackView::refresh()
{
    chains = proc.getChains();
    infos = proc.getPatchNodes();

    std::vector<int> ids;

    for (const auto& c : chains)
        ids.push_back (c.id);

    if (ids != rowIds)
    {
        rows.clear();

        for (const auto& c : chains)
            addAndMakeVisible (rows.add (new ChainRow (proc, c.id)));

        rowIds = ids;
    }

    for (int i = 0; i < rows.size(); ++i)
        rows[i]->setName (chains[(size_t) i].name);

    if (selectedDevice != 0 && infoFor (selectedDevice) == nullptr)
        selectedDevice = 0;

    clampScroll();
    resized();
    repaint();
}

//==============================================================================
juce::Rectangle<int> RackView::rowRect (int chainIndex) const
{
    return { 0, kTopBar + chainIndex * kRowH - scrollY, getWidth(), kRowH - 4 };
}

// Cards shrink (down to a floor) so a longer chain still fits its row and
// keeps its "+" slot in view.
int RackView::cardWidth (int chainIndex) const
{
    const int count = (int) chains[(size_t) chainIndex].devices.size();

    if (count == 0)
        return kCardW;

    const int available = getWidth() - kHeaderW - 14 - kPlusW - kCardGap - 16;
    return juce::jlimit (88, kCardW, (available - (count - 1) * kCardGap) / count);
}

juce::Rectangle<int> RackView::cardRect (int chainIndex, int deviceIndex) const
{
    const auto r = rowRect (chainIndex);
    const int w = cardWidth (chainIndex);
    return { kHeaderW + 14 + deviceIndex * (w + kCardGap), r.getY() + 10, w, kCardH };
}

juce::Rectangle<int> RackView::plusRect (int chainIndex) const
{
    const int count = (int) chains[(size_t) chainIndex].devices.size();
    const auto c = cardRect (chainIndex, count);
    return { c.getX(), c.getY(), kPlusW, kCardH };
}

int RackView::rowAt (int y) const
{
    if (chains.empty())
        return -1;

    return juce::jlimit (0, (int) chains.size() - 1, (y - kTopBar + scrollY) / kRowH);
}

RackView::CardHit RackView::hitCard (juce::Point<int> p) const
{
    for (int c = 0; c < (int) chains.size(); ++c)
        for (int d = 0; d < (int) chains[(size_t) c].devices.size(); ++d)
        {
            const auto r = cardRect (c, d);

            if (r.contains (p))
            {
                const juce::Rectangle<int> dot (r.getX() + 6, r.getY() + 6, 18, 18);
                return { c, d, dot.contains (p) };
            }
        }

    return {};
}

int RackView::hitPlus (juce::Point<int> p) const
{
    for (int c = 0; c < (int) chains.size(); ++c)
        if (plusRect (c).contains (p))
            return c;

    return -1;
}

void RackView::clampScroll()
{
    const int content = kTopBar + (int) chains.size() * kRowH + 8;
    scrollY = juce::jlimit (0, juce::jmax (0, content - getHeight()), scrollY);
}

void RackView::resized()
{
    addChainButton.setBounds (10, 8, 110, 28);
    clampScroll();

    for (int i = 0; i < rows.size(); ++i)
        rows[i]->setBounds (0, rowRect (i).getY(), getWidth(), kRowH - 4);
}

//==============================================================================
void RackView::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff17191d));

    g.setColour (juce::Colours::grey);
    g.setFont (juce::Font (juce::FontOptions (13.0f)));
    g.drawText ("Chains run in parallel and sum at the output. Devices in a chain run in order.",
                juce::Rectangle<int> (130, 8, getWidth() - 140, 28), juce::Justification::centredLeft);

    if (chains.empty())
    {
        g.setFont (juce::Font (juce::FontOptions (15.0f)));
        g.drawFittedText ("The rack is empty.\nPress Add chain, then add plugins to it.",
                          getLocalBounds().withTrimmedTop (kTopBar).reduced (40), juce::Justification::centred, 3);
        return;
    }

    for (int c = 0; c < (int) chains.size(); ++c)
    {
        const auto row = rowRect (c);

        if (row.getBottom() < kTopBar || row.getY() > getHeight())
            continue;

        const bool active = chains[(size_t) c].id == proc.getActiveChain();

        g.setColour (juce::Colour (active ? 0xff22262c : 0xff1d2025));
        g.fillRoundedRectangle (row.toFloat().reduced (4.0f, 0.0f), 7.0f);

        g.setColour (juce::Colour (0xff2a2e35));
        g.fillRoundedRectangle (juce::Rectangle<float> (4.0f, (float) row.getY(), (float) kHeaderW, (float) row.getHeight()), 7.0f);

        if (active)
        {
            g.setColour (juce::Colour (0xff4c8dff));
            g.fillRect (juce::Rectangle<int> (4, row.getY() + 8, 3, row.getHeight() - 16));
        }

        const auto& devices = chains[(size_t) c].devices;

        for (int d = 0; d < (int) devices.size(); ++d)
        {
            const auto* info = infoFor (devices[(size_t) d]);

            if (info == nullptr)
                continue;

            const auto r = cardRect (c, d).toFloat();
            const bool selectedCard = devices[(size_t) d] == selectedDevice;
            const bool beingDragged = dragging && devices[(size_t) d] == dragUid;
            const float alpha = (info->bypassed ? 0.5f : 1.0f) * (beingDragged ? 0.35f : 1.0f);

            g.setColour (juce::Colour (0xff30353d).withAlpha (alpha));
            g.fillRoundedRectangle (r, 6.0f);
            g.setColour ((selectedCard ? juce::Colours::white : juce::Colour (0xff4a5059)).withAlpha (selectedCard ? 0.95f : alpha));
            g.drawRoundedRectangle (r, 6.0f, selectedCard ? 2.0f : 1.0f);

            // power dot: lit when the device is active
            g.setColour ((info->bypassed ? juce::Colour (0xff666b73) : juce::Colour (0xff46c37b)).withAlpha (beingDragged ? 0.35f : 1.0f));
            g.fillEllipse (r.getX() + 9.0f, r.getY() + 9.0f, 12.0f, 12.0f);

            auto text = r.reduced (8.0f, 6.0f);
            text.removeFromLeft (20.0f);
            g.setColour (juce::Colours::white.withAlpha (alpha));
            g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
            g.drawText (info->name, text.removeFromTop (18.0f), juce::Justification::centredLeft, true);

            auto sub = r.reduced (10.0f, 0.0f);
            sub.removeFromTop (30.0f);
            g.setColour (juce::Colours::grey.withAlpha (alpha));
            g.setFont (juce::Font (juce::FontOptions (11.0f)));
            g.drawText (info->bypassed ? "bypassed" : info->subtitle, sub.removeFromTop (16.0f),
                        juce::Justification::centredLeft, true);
        }

        // the "+" slot
        const auto plus = plusRect (c).toFloat();
        const float dashes[] = { 4.0f, 4.0f };
        juce::Path outline, dashed;
        outline.addRoundedRectangle (plus, 6.0f);
        juce::PathStrokeType (1.2f).createDashedStroke (dashed, outline, dashes, 2);
        g.setColour (juce::Colour (0xff5a616b));
        g.fillPath (dashed);
        g.setFont (juce::Font (juce::FontOptions (22.0f)));
        g.drawText ("+", plus, juce::Justification::centred);
    }
}

void RackView::paintOverChildren (juce::Graphics& g)
{
    if (! dragging || dragUid == 0)
        return;

    // drop marker
    if (dropChain >= 0 && dropChain < (int) chains.size())
    {
        const auto r = cardRect (dropChain, dropIndex);
        g.setColour (juce::Colour (0xff4c8dff));
        g.fillRoundedRectangle ((float) r.getX() - 6.0f, (float) r.getY(), 3.0f, (float) r.getHeight(), 1.5f);
    }

    // card following the cursor
    if (const auto* info = infoFor (dragUid))
    {
        const auto r = juce::Rectangle<float> ((float) dragPos.x - kCardW * 0.5f, (float) dragPos.y - kCardH * 0.5f,
                                               (float) kCardW, (float) kCardH);
        g.setColour (juce::Colour (0xff3a4048).withAlpha (0.92f));
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (juce::Colours::white.withAlpha (0.9f));
        g.drawRoundedRectangle (r, 6.0f, 1.5f);
        g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
        g.drawText (info->name, r.reduced (10.0f, 0.0f), juce::Justification::centredLeft, true);
    }
}

//==============================================================================
void RackView::select (NodeUid device, int chainId)
{
    selectedDevice = device;

    if (chainId != 0)
        proc.setActiveChain (chainId);

    repaint();

    if (onSelectionChanged)
        onSelectionChanged();
}

void RackView::mouseDown (const juce::MouseEvent& e)
{
    if (isShowing())
        grabKeyboardFocus();
    const auto p = e.getPosition();
    const auto hit = hitCard (p);

    if (e.mods.isPopupMenu())
    {
        if (hit.chain >= 0)
        {
            const auto uid = chains[(size_t) hit.chain].devices[(size_t) hit.device];
            select (uid, chains[(size_t) hit.chain].id);
            showDeviceMenu (uid);
        }
        else if (onContextMenu)
        {
            onContextMenu();
        }

        return;
    }

    if (hit.chain >= 0)
    {
        const auto uid = chains[(size_t) hit.chain].devices[(size_t) hit.device];

        if (hit.power)
        {
            if (const auto* info = infoFor (uid))
                proc.setNodeBypassed (uid, ! info->bypassed);

            return;
        }

        select (uid, chains[(size_t) hit.chain].id);
        dragUid = uid;
        dragStartMouse = p;
        dragging = false;
        return;
    }

    if (const int plus = hitPlus (p); plus >= 0)
    {
        select (0, chains[(size_t) plus].id);

        if (onAddToChain)
            onAddToChain (chains[(size_t) plus].id);

        return;
    }

    if (const int row = rowAt (p.y); row >= 0 && p.y >= kTopBar)
        select (0, chains[(size_t) row].id);
}

void RackView::updateDropTarget (juce::Point<int> p)
{
    dropChain = rowAt (p.y);
    dropIndex = 0;

    if (dropChain < 0)
        return;

    const auto& devices = chains[(size_t) dropChain].devices;
    int index = 0;

    // Where the card would land, counting the devices that stay put.
    for (int d = 0; d < (int) devices.size(); ++d)
    {
        if (devices[(size_t) d] == dragUid)
            continue;

        if (cardRect (dropChain, d).getCentreX() < p.x)
            ++index;
    }

    dropIndex = index;
}

void RackView::mouseDrag (const juce::MouseEvent& e)
{
    if (dragUid == 0)
        return;

    const auto p = e.getPosition();

    if (! dragging && p.getDistanceFrom (dragStartMouse) < 6)
        return;

    dragging = true;
    dragPos = p;
    updateDropTarget (p);
    repaint();
}

void RackView::mouseUp (const juce::MouseEvent&)
{
    const auto uid = dragUid;
    const bool wasDragging = dragging;
    const int chain = dropChain, index = dropIndex;

    dragUid = 0;
    dragging = false;
    dropChain = dropIndex = -1;

    if (wasDragging && uid != 0 && chain >= 0 && chain < (int) chains.size())
        proc.moveDevice (uid, chains[(size_t) chain].id, index);

    repaint();
}

void RackView::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto hit = hitCard (e.getPosition());

    if (hit.chain >= 0 && ! hit.power && onOpenDevice)
        onOpenDevice (chains[(size_t) hit.chain].devices[(size_t) hit.device]);
}

void RackView::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    scrollY -= (int) (wheel.deltaY * 120.0f);
    resized();
    repaint();
}

bool RackView::keyPressed (const juce::KeyPress& key)
{
    if ((key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) && selectedDevice != 0)
    {
        const auto uid = selectedDevice;
        selectedDevice = 0;
        proc.removeNode (uid);

        if (onSelectionChanged)
            onSelectionChanged();

        return true;
    }

    return false;
}

void RackView::showDeviceMenu (NodeUid uid)
{
    const auto* info = infoFor (uid);

    if (info == nullptr)
        return;

    juce::PopupMenu menu;
    menu.addItem (1, "Open plugin window");
    menu.addItem (2, "Bypass", true, info->bypassed);
    menu.addSeparator();
    menu.addItem (3, "Remove");

    juce::Component::SafePointer<RackView> safeThis (this);
    const bool bypassed = info->bypassed;

    menu.showMenuAsync (juce::PopupMenu::Options(), [safeThis, uid, bypassed] (int result)
    {
        if (safeThis == nullptr)
            return;

        if (result == 1 && safeThis->onOpenDevice)  safeThis->onOpenDevice (uid);
        else if (result == 2)                        safeThis->proc.setNodeBypassed (uid, ! bypassed);
        else if (result == 3)                        safeThis->proc.removeNode (uid);
    });
}
