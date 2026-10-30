#include "SurfacePanel.h"

namespace
{
    // red, orange, yellow, green, cyan, blue, indigo, violet
    const juce::Colour kRainbow[8] = { juce::Colour (0xffe5484d), juce::Colour (0xfff5a524), juce::Colour (0xffe6d84a),
                                       juce::Colour (0xff46c37b), juce::Colour (0xff35c1d6), juce::Colour (0xff4c8dff),
                                       juce::Colour (0xff6c5ce7), juce::Colour (0xffb35cf0) };
}

//==============================================================================
// Edits the targets of one knob: which parameters it drives, and the range
// each one sweeps as the knob goes from 0 to 1. Setting the first value above
// the second reverses the control.
class MappingEditor : public juce::Component
{
public:
    MappingEditor (UltrapatcherProcessor& p, int slotIndex) : proc (p), slot (slotIndex) { rebuild(); }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff23262b));

        g.setColour (juce::Colours::grey);
        g.setFont (juce::Font (juce::FontOptions (12.0f)));

        if (rows.isEmpty())
        {
            g.drawFittedText ("No mappings yet. Press Learn on the knob, then move a parameter in a plugin window.",
                              getLocalBounds().reduced (16), juce::Justification::centred, 3);
        }
        else
        {
            g.drawText ("Parameter", 12, 6, 200, 18, juce::Justification::centredLeft);
            g.drawText ("Knob at 0", 232, 6, 150, 18, juce::Justification::centred);
            g.drawText ("Knob at 1", 390, 6, 150, 18, juce::Justification::centred);
        }
    }

    void resized() override
    {
        int y = 28;

        for (auto* r : rows)
        {
            r->label.setBounds (12, y, 214, 24);
            r->low.setBounds (232, y, 150, 24);
            r->high.setBounds (390, y, 150, 24);
            r->remove.setBounds (548, y, 26, 24);
            y += 30;
        }
    }

private:
    struct Row
    {
        juce::Label label;
        juce::Slider low, high;
        juce::TextButton remove { "x" };
    };

    void rebuild()
    {
        rows.clear();
        const auto targets = proc.getSlotTargets (slot);

        for (int i = 0; i < (int) targets.size(); ++i)
        {
            auto* r = rows.add (new Row());
            const auto& t = targets[(size_t) i];

            r->label.setText (t.nodeName + ": " + t.paramName, juce::dontSendNotification);
            r->label.setFont (juce::Font (juce::FontOptions (13.0f)));
            r->label.setMinimumHorizontalScale (0.8f);

            for (auto* s : { &r->low, &r->high })
            {
                s->setSliderStyle (juce::Slider::LinearHorizontal);
                s->setTextBoxStyle (juce::Slider::TextBoxRight, false, 46, 20);
                s->setRange (0.0, 1.0, 0.001);
                s->setNumDecimalPlacesToDisplay (2);
            }

            r->low.setValue (t.rangeMin, juce::dontSendNotification);
            r->high.setValue (t.rangeMax, juce::dontSendNotification);

            auto apply = [this, i, r] { proc.setTargetRange (slot, i, (float) r->low.getValue(), (float) r->high.getValue()); };
            r->low.onValueChange = apply;
            r->high.onValueChange = apply;

            r->remove.onClick = [this, i]
            {
                juce::Component::SafePointer<MappingEditor> safeThis (this);

                juce::MessageManager::callAsync ([safeThis, this, i]
                {
                    if (safeThis == nullptr)
                        return;

                    proc.removeTarget (slot, i);
                    rebuild();
                });
            };

            for (auto* c : std::initializer_list<juce::Component*> { &r->label, &r->low, &r->high, &r->remove })
                addAndMakeVisible (c);
        }

        setSize (586, juce::jmax (80, 34 + (int) targets.size() * 30));
        resized();
        repaint();
    }

    UltrapatcherProcessor& proc;
    int slot;
    juce::OwnedArray<Row> rows;
};

//==============================================================================
class SurfacePanel::KnobCell : public juce::Component
{
public:
    KnobCell (UltrapatcherProcessor& p, int slotIndex)
        : proc (p), slot (slotIndex), colour (kRainbow[slotIndex % 8])
    {
        addAndMakeVisible (knob);
        addAndMakeVisible (nameLabel);
        addAndMakeVisible (learnButton);
        addAndMakeVisible (menuButton);

        knob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        knob.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 16);
        knob.setNumDecimalPlacesToDisplay (2);
        knob.setColour (juce::Slider::rotarySliderFillColourId, colour);
        knob.setColour (juce::Slider::thumbColourId, colour);
        attachment = std::make_unique<juce::SliderParameterAttachment> (*proc.getPoolParameter (slot), knob, nullptr);

        nameLabel.setJustificationType (juce::Justification::centred);
        nameLabel.setEditable (false, true);
        nameLabel.setFont (juce::Font (juce::FontOptions (12.5f)));
        nameLabel.onTextChange = [this]
        {
            const auto text = nameLabel.getText().trim();

            // Unchanged auto names are not stored as custom names.
            if (text != proc.getSlotDisplayName (slot) && text != defaultName())
                proc.setSlotCustomName (slot, text);
            else if (text.isEmpty())
                proc.setSlotCustomName (slot, {});

            refresh();
        };

        learnButton.setColour (juce::TextButton::buttonOnColourId, colour);
        learnButton.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
        learnButton.onClick = [this]
        {
            if (proc.getLearnSlot() == slot)
                proc.cancelLearn();
            else
                proc.startLearn (slot);
        };

        menuButton.onClick = [this] { showMenu(); };
        refresh();
    }

    void refresh()
    {
        const auto name = proc.getSlotDisplayName (slot);
        nameLabel.setText (name.isNotEmpty() ? name : defaultName(), juce::dontSendNotification);
        nameLabel.setColour (juce::Label::textColourId, name.isNotEmpty() ? juce::Colours::white : juce::Colours::grey);
        learnButton.setToggleState (proc.getLearnSlot() == slot, juce::dontSendNotification);
        menuButton.setEnabled (! proc.getSlotTargets (slot).empty());
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced (3.0f);
        g.setColour (juce::Colour (0xff23262b));
        g.fillRoundedRectangle (b, 6.0f);
        g.setColour (colour.withAlpha (0.9f));
        g.fillRoundedRectangle (b.removeFromTop (3.0f), 1.5f);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (6, 6);
        area.removeFromTop (4);

        auto buttons = area.removeFromBottom (22);
        learnButton.setBounds (buttons.removeFromLeft (buttons.getWidth() - 30));
        buttons.removeFromLeft (4);
        menuButton.setBounds (buttons);

        area.removeFromBottom (4);
        nameLabel.setBounds (area.removeFromBottom (20));
        knob.setBounds (area);
    }

private:
    juce::String defaultName() const { return "Knob " + juce::String (slot + 1); }

    void showMenu()
    {
        juce::PopupMenu menu;
        menu.addItem (1, "Edit mappings...");
        menu.addItem (2, "Clear mappings");
        menu.addItem (3, "Reset name");

        juce::Component::SafePointer<KnobCell> safeThis (this);

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&menuButton), [safeThis] (int result)
        {
            if (safeThis == nullptr)
                return;

            if (result == 1)
            {
                juce::CallOutBox::launchAsynchronously (std::make_unique<MappingEditor> (safeThis->proc, safeThis->slot),
                                                        safeThis->menuButton.getScreenBounds(), nullptr);
            }
            else if (result == 2)  safeThis->proc.clearSlot (safeThis->slot);
            else if (result == 3)  safeThis->proc.setSlotCustomName (safeThis->slot, {});
        });
    }

    UltrapatcherProcessor& proc;
    int slot;
    juce::Colour colour;

    juce::Slider knob;
    juce::Label nameLabel;
    juce::TextButton learnButton { "Learn" };
    juce::TextButton menuButton { "..." };
    std::unique_ptr<juce::SliderParameterAttachment> attachment;
};

//==============================================================================
SurfacePanel::SurfacePanel (UltrapatcherProcessor& p) : proc (p)
{
    for (int i = 0; i < UltrapatcherProcessor::kKnobs; ++i)
        addAndMakeVisible (cells.add (new KnobCell (proc, i)));
}

SurfacePanel::~SurfacePanel() = default;

void SurfacePanel::refresh()
{
    for (auto* c : cells)
        c->refresh();
}

void SurfacePanel::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1c1e22));
}

void SurfacePanel::resized()
{
    auto area = getLocalBounds().reduced (4);
    const int w = area.getWidth() / juce::jmax (1, cells.size());

    for (auto* c : cells)
        c->setBounds (area.removeFromLeft (w));
}
