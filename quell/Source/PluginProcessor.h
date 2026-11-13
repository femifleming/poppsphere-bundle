#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "Engine.h"

namespace ql
{
juce::String bandId (int b, const char* what);

struct FactoryPreset { const char* name; const char* category; std::function<void (juce::AudioProcessorValueTreeState&)> apply; };
const std::vector<FactoryPreset>& factoryPresets();
}

class QuellProcessor : public juce::AudioProcessor
{
public:
    QuellProcessor();
    ~QuellProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Quell"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- editor API
    juce::UndoManager undo { 30000, 60 };
    juce::AudioProcessorValueTreeState apvts;
    float get (const juce::String& id) const { if (auto* p = apvts.getRawParameterValue (id)) return p->load(); return 0.0f; }
    void set (const juce::String& id, float v);          // sets and notifies host (value in real units)
    juce::RangedAudioParameter* param (const juce::String& id) { return apvts.getParameter (id); }

    ql::Settings currentSettings() const;
    bool readDisplay (ql::Engine::Display& d) { return engine.readDisplay (d); }
    float meter (int c) const { return engine.meterReduction (c); }
    float sidechainLevel() const { return scLevel.load(); }
    int numMainChannels() const { return mainChannels.load(); }
    bool sidechainConnected() const { return scConnected.load(); }

    // A / B
    int abSlot = 0;
    void switchAB (int slot);
    void copyAB();
    // presets
    juce::String presetName { "Default" };
    void loadFactory (int i);
    juce::File userPresetFolder() const;
    bool saveUserPreset (const juce::String& name);
    bool loadPresetFile (const juce::File& f);
    void resetToDefaults();
    void resetBand (int i);
    // undo history kept per A/B slot: switching clears it
    int uiWidth = 0, uiHeight = 0;

private:
    juce::ValueTree abStates[2];
    ql::Engine engine;
    juce::AudioBuffer<float> scratch, scBuf, dryDelay;
    int dryPos = 0, latency = 0;
    float bypassMix = 0.0f, outGainSm = 1.0f;
    std::atomic<float> scLevel { 0.0f };
    std::atomic<int> mainChannels { 2 };
    std::atomic<bool> scConnected { false };
    void updateLatency (const ql::Settings& s);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (QuellProcessor)
};
