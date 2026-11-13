#pragma once
#include "PluginProcessor.h"

class QuellEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit QuellEditor (QuellProcessor&);
    ~QuellEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    static constexpr int baseW = 980, baseH = 640;
    void setScale (float s);
    void refreshNow() { timerCallback(); }

private:
    void timerCallback() override;
    QuellProcessor& proc;
    std::unique_ptr<juce::Component> content;
    std::unique_ptr<juce::TooltipWindow> tips;
    juce::ComponentBoundsConstrainer constrainer;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (QuellEditor)
};
