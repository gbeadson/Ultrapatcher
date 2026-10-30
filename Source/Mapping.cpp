#include "PluginProcessor.h"
#include "PoolParameter.h"

//==============================================================================
// Knob mappings.
//
// A knob is a pool slot: a generic DAW parameter that can drive any number of
// hosted parameters, each with its own range (min > max inverts). A slot with a
// single full-range target is two-way, so turning the knob in the plugin's own
// window moves the knob back. Slots with several targets are one-way, like a
// macro: the knob drives the plugins.
//
//  - Audio thread (applyPoolToHosted): a changed knob value is pushed to its
//    targets at block rate. It never blocks: if the message thread holds the
//    lock, that block skips the update and the next block catches up.
//  - Message thread (timerCallback, 30 Hz): hosted changes flow back for
//    two-way slots, and learn mode watches for a touched parameter.
namespace
{
    constexpr float kEpsilon = 1.0e-6f;
}

juce::RangedAudioParameter* UltrapatcherProcessor::getPoolParameter (int slot) const
{
    return juce::isPositiveAndBelow (slot, kPoolSize) ? pool[(size_t) slot] : nullptr;
}

std::vector<MapTarget> UltrapatcherProcessor::getSlotTargets (int slot) const
{
    return juce::isPositiveAndBelow (slot, kPoolSize) ? knobs[(size_t) slot].targets : std::vector<MapTarget>();
}

juce::String UltrapatcherProcessor::getSlotDisplayName (int slot) const
{
    if (! juce::isPositiveAndBelow (slot, kPoolSize))
        return {};

    const auto& k = knobs[(size_t) slot];

    if (k.customName.isNotEmpty())
        return k.customName;

    if (k.targets.empty())
        return {};

    return k.targets.front().paramName + (k.targets.size() > 1 ? " +" + juce::String (k.targets.size() - 1) : juce::String());
}

void UltrapatcherProcessor::refreshSlotName (int slot)
{
    const auto& k = knobs[(size_t) slot];

    pool[(size_t) slot]->setMappedName (k.customName.isNotEmpty() ? k.customName
                                        : k.targets.empty()       ? juce::String()
                                                                  : k.targets.front().paramName);
    updateHostDisplay (ChangeDetails().withParameterInfoChanged (true));
}

void UltrapatcherProcessor::setSlotCustomName (int slot, const juce::String& name)
{
    if (! juce::isPositiveAndBelow (slot, kPoolSize))
        return;

    knobs[(size_t) slot].customName = name.trim();
    refreshSlotName (slot);
    sendChangeMessage();
}

// Resolves a slot's targets to live parameter pointers for the audio thread.
// Pointers stay valid because a node's targets are dropped before the node is
// removed (see dropTargetsForNode).
void UltrapatcherProcessor::rebuildSlot (int slot)
{
    const auto& k = knobs[(size_t) slot];
    std::vector<LiveTarget> live;

    for (const auto& t : k.targets)
    {
        if (auto* proc = getNodeProcessor (t.uid))
        {
            const auto& params = proc->getParameters();

            if (juce::isPositiveAndBelow (t.paramIndex, params.size()))
                live.push_back ({ params[t.paramIndex], t.rangeMin, t.rangeMax });
        }
    }

    const bool twoWay = k.targets.size() == 1 && live.size() == 1
                     && juce::approximatelyEqual (k.targets.front().rangeMin, 0.0f)
                     && juce::approximatelyEqual (k.targets.front().rangeMax, 1.0f);

    {
        const juce::SpinLock::ScopedLockType sl (mappingLock);
        std::swap (slots[(size_t) slot].targets, live);
        slots[(size_t) slot].twoWay = twoWay;
    }
    // the old vector is freed here, outside the lock
}

bool UltrapatcherProcessor::addTarget (int slot, NodeUid uid, int paramIndex)
{
    if (! juce::isPositiveAndBelow (slot, kPoolSize))
        return false;

    auto* proc = getNodeProcessor (uid);

    if (proc == nullptr)
        return false;

    const auto& params = proc->getParameters();

    if (! juce::isPositiveAndBelow (paramIndex, params.size()))
        return false;

    auto& k = knobs[(size_t) slot];

    for (const auto& t : k.targets)
        if (t.uid == uid && t.paramIndex == paramIndex)
            return false;

    auto* param = params[paramIndex];
    const bool first = k.targets.empty();

    // The first target sets the knob to the parameter's current value, so
    // mapping never makes a parameter jump. Later targets are left alone until
    // the knob next moves.
    if (first)
        pool[(size_t) slot]->setValueNotifyingHost (param->getValue());

    MapTarget t;
    t.uid = uid;
    t.paramIndex = paramIndex;
    t.nodeName = getNodeName (uid);
    t.paramName = param->getName (64);
    k.targets.push_back (t);

    rebuildSlot (slot);

    {
        const juce::SpinLock::ScopedLockType sl (mappingLock);
        auto& s = slots[(size_t) slot];
        s.lastPoolApplied = pool[(size_t) slot]->get();

        if (first)
            s.lastHostedSeen = param->getValue();
    }

    refreshSlotName (slot);
    sendChangeMessage();
    return true;
}

void UltrapatcherProcessor::removeTarget (int slot, int targetIndex)
{
    if (! juce::isPositiveAndBelow (slot, kPoolSize))
        return;

    auto& k = knobs[(size_t) slot];

    if (! juce::isPositiveAndBelow (targetIndex, (int) k.targets.size()))
        return;

    k.targets.erase (k.targets.begin() + targetIndex);
    rebuildSlot (slot);
    refreshSlotName (slot);
    sendChangeMessage();
}

void UltrapatcherProcessor::setTargetRange (int slot, int targetIndex, float rangeMin, float rangeMax)
{
    if (! juce::isPositiveAndBelow (slot, kPoolSize))
        return;

    auto& k = knobs[(size_t) slot];

    if (! juce::isPositiveAndBelow (targetIndex, (int) k.targets.size()))
        return;

    k.targets[(size_t) targetIndex].rangeMin = juce::jlimit (0.0f, 1.0f, rangeMin);
    k.targets[(size_t) targetIndex].rangeMax = juce::jlimit (0.0f, 1.0f, rangeMax);
    rebuildSlot (slot);
    sendChangeMessage();
}

void UltrapatcherProcessor::clearSlot (int slot)
{
    if (! juce::isPositiveAndBelow (slot, kPoolSize))
        return;

    knobs[(size_t) slot] = {};
    rebuildSlot (slot);
    refreshSlotName (slot);
    sendChangeMessage();
}

void UltrapatcherProcessor::dropTargetsForNode (NodeUid uid)
{
    for (int slot = 0; slot < kPoolSize; ++slot)
    {
        auto& targets = knobs[(size_t) slot].targets;
        const auto before = targets.size();

        targets.erase (std::remove_if (targets.begin(), targets.end(),
                                       [uid] (const MapTarget& t) { return t.uid == uid; }),
                       targets.end());

        if (targets.size() != before)
        {
            rebuildSlot (slot);
            refreshSlotName (slot);
        }
    }
}

//==============================================================================
void UltrapatcherProcessor::applyPoolToHosted()
{
    const juce::SpinLock::ScopedTryLockType lock (mappingLock);

    if (! lock.isLocked())
        return;

    for (size_t i = 0; i < (size_t) kPoolSize; ++i)
    {
        auto& s = slots[i];

        if (s.targets.empty())
            continue;

        const float value = pool[i]->get();

        if (std::abs (value - s.lastPoolApplied) <= kEpsilon)
            continue;

        s.lastPoolApplied = value;

        for (auto& t : s.targets)
            t.param->setValue (juce::jlimit (0.0f, 1.0f, t.rangeMin + value * (t.rangeMax - t.rangeMin)));

        if (s.twoWay)
            s.lastHostedSeen = s.targets.front().param->getValue(); // read back: stepped params may snap
    }
}

void UltrapatcherProcessor::timerCallback()
{
    pollLearn();

    std::vector<std::pair<int, float>> updates;

    {
        const juce::SpinLock::ScopedLockType sl (mappingLock);

        for (size_t i = 0; i < (size_t) kPoolSize; ++i)
        {
            auto& s = slots[i];

            if (! s.twoWay || s.targets.empty())
                continue;

            const float hosted = s.targets.front().param->getValue();

            if (std::abs (hosted - s.lastHostedSeen) <= kEpsilon)
                continue;

            s.lastHostedSeen = hosted;

            if (std::abs (hosted - pool[i]->get()) > kEpsilon)
            {
                s.lastPoolApplied = hosted; // the knob is about to take this value
                updates.emplace_back ((int) i, hosted);
            }
        }
    }

    // Call into the host outside the lock.
    for (const auto& u : updates)
    {
        auto* p = pool[(size_t) u.first];
        p->beginChangeGesture();
        p->setValueNotifyingHost (u.second);
        p->endChangeGesture();
    }
}

//==============================================================================
// Learn: click a knob's Learn button, then move a parameter in a plugin
// window. The first parameter that changes is mapped to that knob.
void UltrapatcherProcessor::startLearn (int slot)
{
    cancelLearn();

    if (! juce::isPositiveAndBelow (slot, kPoolSize))
        return;

    learnBase.clear();

    for (const auto& n : nodes)
    {
        const auto& params = n.graphNode->getProcessor()->getParameters();

        for (int i = 0; i < params.size(); ++i)
            if (params[i]->isAutomatable())
                learnBase.push_back ({ n.uid, i, params[i]->getValue() });
    }

    if (learnBase.empty())
    {
        setError ("Add a plugin first, then use Learn");
        return;
    }

    lastError.clear();
    learnSlot = slot;
    sendChangeMessage();
}

void UltrapatcherProcessor::cancelLearn()
{
    if (learnSlot < 0)
        return;

    learnSlot = -1;
    learnBase.clear();
    sendChangeMessage();
}

void UltrapatcherProcessor::pollLearn()
{
    if (learnSlot < 0)
        return;

    for (const auto& snap : learnBase)
    {
        auto* proc = getNodeProcessor (snap.uid);

        if (proc == nullptr)
            continue;

        const auto& params = proc->getParameters();

        if (! juce::isPositiveAndBelow (snap.index, params.size()))
            continue;

        if (std::abs (params[snap.index]->getValue() - snap.value) <= 0.002f)
            continue;

        // Already mapped to this knob? Keep waiting for another parameter.
        bool alreadyHere = false;

        for (const auto& t : knobs[(size_t) learnSlot].targets)
            alreadyHere = alreadyHere || (t.uid == snap.uid && t.paramIndex == snap.index);

        if (alreadyHere)
            continue;

        const int slot = learnSlot;
        learnSlot = -1;
        learnBase.clear();
        addTarget (slot, snap.uid, snap.index);
        sendChangeMessage();
        return;
    }
}
