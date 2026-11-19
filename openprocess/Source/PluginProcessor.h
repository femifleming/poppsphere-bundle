#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "Bank.h"
#include "Engine.h"
#include "Model.h"
#include <initializer_list>

// One processor, four plug-ins: openprocess and openfield (OP_SPATIAL), each as an instrument and
// as an effect ("... fx", JucePlugin_IsSynth 0) for hosts that keep the two apart.
class OpProcessor : public juce::AudioProcessor, public juce::VST3ClientExtensions, private juce::Timer
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
    // VST3: an instrument's audio input is a side-chain (hosts route another track into it);
    // the effect's input is the track it sits on
    juce::VST3ClientExtensions* getVST3ClientExtensions() override { return this; }
    bool getPluginHasMainInput() const override { return ! isInstrument(); }
    static bool isInstrument();

    juce::AudioProcessorValueTreeState params;

    //== model access (message thread) =========================================
    Model getModel() const;
    void setModel (const Model&);
    template <class F> void edit (F&& f)
    {
        SlotValues v;
        {
            const juce::SpinLock::ScopedLockType sl (lock);
            f (model);
            model.link();
            v = slotValues (model);
        }
        pushSlots (v);   // the host's "Sound nn ..." parameters follow the play area
    }
    // the timeline is sounding now (started by the red flag / the Timeline Play parameter, or run by the host)
    bool timelinePlaying() const { return playingNow.load(); }

    //== the host ================================================================
    // Every sound of the play area has a slot (1..24) with its own automatable parameters: where it
    // is and which part of it loops. They go both ways: moving a sound moves the parameters, and
    // the host's automation moves the sound.
    static constexpr int kSlots = Model::kMaxItems;
    enum SlotField { fX = 0, fY, fStart, fLength, fDist, kNumFields };
    static juce::String slotParamId (int slot, int field);
    // a drag of that sound begins / ends: a gesture on its parameters, so a host can record it
    void slotGesture (int itemId, std::initializer_list<int> fields, bool begin);
    // Follow Host: while the host's transport runs, the timeline plays at the host's position
    bool followsHost() const;
    void setFollowsHost (bool);
    bool hostRunning() const { return hostDriving.load(); }   // the host is playing the timeline now
    bool hasHostTransport() const { return ! isStandalone(); }
    // the editor's size is kept with the session
    int editorWidth() const { return uiW.load(); }
    void setEditorWidth (int w) { uiW = w; }
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
    void clearAll();   // a new piece: play area, reels and timeline

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
    static const char* productName() { return OP_SPATIAL ? "openfield" : "openprocess"; }   // the same for "... fx"
    bool isStandalone() const { return wrapperType == wrapperType_Standalone; }

private:
    static BusesProperties buses();
    void timerCallback() override;

    // parameters <-> play area
    struct SlotValues { bool has[kSlots] = {}; float v[kSlots][kNumFields] = {}; };
    static SlotValues slotValues (const Model&);
    void pushSlots (const SlotValues&);
    void applySlotParams (Model&);            // host -> play area; call with the lock held
    void sceneFromHost (bool canRecall);      // the Recall Snapshot parameter; lock held
    void recallOnAudioThread (int index);     // lock held, no allocation
    void setSceneParam (int value);
    // A VST3 host sends every change the plug-in reports back to the plug-in. Normally that echo comes
    // a block later and changes nothing; if the host's audio was idle in between it can come after
    // newer values and would undo them. So the plug-in remembers what it reported for a while and
    // does not take those values back.
    struct Reported
    {
        float latest = 0.0f;      // what the parameter is meant to be now
        float old[4] = {};        // what was reported lately
        int count = 0;
        juce::int64 at = 0;       // when (in processed samples)
    };
    void report (Reported&, float value, bool notified) const;
    bool isStaleEcho (Reported&, float value) const;
    std::array<std::array<Reported, kNumFields>, kSlots> slotReported {};   // guarded by lock
    Reported sceneReported;                                                // guarded by lock
    std::atomic<juce::int64> samplesDone { 0 };
    std::atomic<bool> repush { false };
    void restoreSpace (const SpaceParams&);
    void setPlayParam (bool);
    std::array<std::array<juce::RangedAudioParameter*, kNumFields>, kSlots> slotPrm {};
    std::array<std::array<std::atomic<float>*, kNumFields>, kSlots> slotRaw {};
    std::array<std::array<float, kNumFields>, kSlots> slotSeen {};     // guarded by lock
    std::array<std::array<bool, kNumFields>, kSlots> slotHeld {};      // message thread: a gesture is open
    std::atomic<float>* playRaw = nullptr;
    std::atomic<float>* syncRaw = nullptr;
    std::atomic<float>* sceneRaw = nullptr;
    int seenScene = 0, sceneWanted = 0;       // guarded by lock: the parameter as last read, and as the plug-in means it
    std::atomic<bool> seenPlay { false };
    std::atomic<int> recalled { -1 };         // a recall the audio thread did: the message thread tidies up
    std::atomic<int> blockCount { 0 };
    int lastBlockCount = -1, idleTicks = 0;   // message thread
    std::atomic<bool> playingNow { false }, hostDriving { false };
    bool hasSegs = false, wasManual = false;  // audio thread
    std::atomic<int> uiW { 0 };
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
    std::atomic<bool> timelinePlayingFlag { false };
    std::atomic<double> tlPos { 0.0 };
    std::vector<float> scratchL, scratchR;
    juce::AudioBuffer<float> inCopy, fold;
    double motionClock = 0;
    int orderCap = 7;
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
