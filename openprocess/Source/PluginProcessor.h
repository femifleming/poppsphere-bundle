#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "Bank.h"
#include "Engine.h"
#include "Model.h"

class OpProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    OpProcessor();
    ~OpProcessor() override;

    void prepareToPlay (double, int) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState params;

    //== model access (message thread) =========================================
    Model getModel() const;
    void setModel (const Model&);
    template <class F> void edit (F&& f)
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        f (model);
    }
    bool timelinePlaying() const { return timelinePlayingFlag.load(); }
    double timelineSeconds() const { return tlPos.load(); }
    double motionTime() const { return motionNow.load(); }      // the clock the motions run on
    int ambisonicOrder() const { return orderNow.load(); }      // 0 = stereo preview
    float inputLevel() const { return inPeak.load(); }
    SpaceParams readSpace() const;
    bool boidPos (int voiceId, float& x, float& y, float& d) const;   // where a bird of the flock is now
    // the spatial meter: energy (mean square) by azimuth / elevation bin
    float meterAz (int b) const { return engine.meterAz[(size_t) b].load (std::memory_order_relaxed); }
    float meterEl (int b) const { return engine.meterEl[(size_t) b].load (std::memory_order_relaxed); }
    // pointer position of a playing voice (0..1), or -1
    float voiceFrac (int voiceId) const;

    int addItem (int cell, float x, float y);
    void applyPreset (int k, bool additive);
    void operatorLength();
    void operatorPosition();
    int takeSnapshot();
    void loadSnapshot (int snapId);
    void toggleTimeline();
    void clearMix();

    //== sounds =================================================================
    bool cellReady (int cell) const;
    juce::String cellName (int cell) const;
    double cellSeconds (int cell) const;
    void loadFiles (const juce::Array<juce::File>&);
    void loadFileIntoCell (int cell, const juce::File&);
    void loadBuiltin();
    void reskip (int cell);
    void captureInput (int cell);
    void renderToFile (const juce::File&);
    bool busy() const { return pending.load() > 0; }

    juce::String status() const;
    void setStatus (const juce::String&);

    static juce::File userFolder();
    bool isStandalone() const { return wrapperType == wrapperType_Standalone; }

private:
    static BusesProperties buses();
    void timerCallback() override;
    void rebuildCell (int cell, const CellSource&);
    void rebuildAll();
    void handleMidi (int note);

    mutable juce::SpinLock lock;
    Model model;
    CellArray cells;
    std::array<CellSource, bank::kCells> sources;
    std::vector<std::pair<std::shared_ptr<Sound>, juce::uint32>> graveyard;
    std::atomic<int> pending { 0 };
    std::atomic<int> generation { 0 };

    juce::AudioFormatManager formats;
    juce::ThreadPool pool { 1 };
    juce::Random rng;

    // audio thread
    Engine engine;
    std::vector<VoiceSpec> specs;
    int numSpecs = 0;
    double tlTime = 0;
    bool wasPlaying = false;
    std::atomic<bool> timelinePlayingFlag { false };
    std::atomic<double> tlPos { 0.0 };
    std::vector<float> scratchL, scratchR;
    juce::AudioBuffer<float> inCopy, fold;
    double motionClock = 0;
    std::atomic<double> motionNow { 0.0 };
    std::atomic<int> orderNow { 0 };
    std::atomic<float> inPeak { 0.0f };
    Flock flock, flockShown;
    mutable juce::SpinLock flockLock;

    // input capture ring, last 8 seconds
    juce::AudioBuffer<float> ring;
    std::atomic<int> ringPos { 0 };
    double hostRate = 44100.0;

    // MIDI notes -> message thread
    juce::AbstractFifo midiFifo { 256 };
    std::array<int, 256> midiNotes {};

    mutable juce::CriticalSection statusLock;
    juce::String statusText;
    juce::uint32 statusTime = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OpProcessor)
};
