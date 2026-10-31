#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

// The mixer at the end of a rack chain: volume, constant-power pan, mute and
// solo. It is an ordinary node, so its parameters can be mapped to Surface
// knobs like any plugin parameter. Solo is rack-wide: the processor sets
// `suppressed` on every other strip while any strip is soloed.
class ChainStripProcessor : public juce::AudioProcessor
{
public:
    ChainStripProcessor()
        : AudioProcessor (BusesProperties()
                              .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                              .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
    {
        addParameter (volume = new juce::AudioParameterFloat (
            { "volume", 1 }, "Volume", juce::NormalisableRange<float> (0.0f, 2.0f), 1.0f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
            {
                return v <= 0.0001f ? juce::String ("-inf dB")
                                    : juce::String (juce::Decibels::gainToDecibels (v), 1) + " dB";
            })));

        addParameter (pan = new juce::AudioParameterFloat (
            { "pan", 1 }, "Pan", juce::NormalisableRange<float> (-1.0f, 1.0f), 0.0f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
            {
                const int pct = (int) std::round (std::abs (v) * 100.0f);
                return pct == 0 ? juce::String ("C") : juce::String (pct) + (v < 0.0f ? " L" : " R");
            })));

        addParameter (mute = new juce::AudioParameterBool ({ "mute", 1 }, "Mute", false));
        addParameter (solo = new juce::AudioParameterBool ({ "solo", 1 }, "Solo", false));
    }

    void prepareToPlay (double, int) override { first = true; }
    void releaseResources() override {}

    bool isBusesLayoutSupported (const BusesLayout& l) const override
    {
        return l.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
            && l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
    }

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals noDenormals;

        const bool silent = mute->get() || suppressed.load();
        const float v = silent ? 0.0f : volume->get();
        const float angle = (pan->get() + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
        const float targetL = v * std::cos (angle) * 1.41421356f;   // unity at the centre,
        const float targetR = v * std::sin (angle) * 1.41421356f;   // constant power across the field

        if (first)
        {
            lastL = targetL;
            lastR = targetR;
            first = false;
        }

        const int n = buffer.getNumSamples();

        if (buffer.getNumChannels() > 0) buffer.applyGainRamp (0, 0, n, lastL, targetL);
        if (buffer.getNumChannels() > 1) buffer.applyGainRamp (1, 0, n, lastR, targetR);

        lastL = targetL;
        lastR = targetR;
    }

    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }

    const juce::String getName() const override { return "Chain mixer"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& dest) override
    {
        juce::MemoryOutputStream out (dest, false);
        out.writeFloat (volume->get());
        out.writeFloat (pan->get());
        out.writeBool (mute->get());
        out.writeBool (solo->get());
    }

    void setStateInformation (const void* data, int size) override
    {
        juce::MemoryInputStream in (data, (size_t) size, false);

        if (size < 10)
            return;

        *volume = in.readFloat();
        *pan = in.readFloat();
        *mute = in.readBool();
        *solo = in.readBool();
    }

    juce::AudioParameterFloat* volume = nullptr;
    juce::AudioParameterFloat* pan = nullptr;
    juce::AudioParameterBool* mute = nullptr;
    juce::AudioParameterBool* solo = nullptr;
    std::atomic<bool> suppressed { false };

private:
    float lastL = 1.0f, lastR = 1.0f;
    bool first = true;
};
