// Plays the looping sounds. Every voice loops continuously; the timeline picks
// which snapshot's voices sound at any moment.
#pragma once
#include "Bank.h"
#include "Model.h"
#include <array>
#include <atomic>

struct VoiceSpec
{
    int id = 0;
    const Sound* snd = nullptr;
    float s = 0, e = 1;
    float gain = 1, pan = 0.5f;
    int effect = fx::None;
};

using CellArray = std::array<std::shared_ptr<Sound>, bank::kCells>;

class Engine
{
public:
    static constexpr int kSlots = 128;
    static constexpr int kMaxSpecs = 256;

    void prepare (double sampleRate);
    void reset();
    // adds into L/R
    void process (float* L, float* R, int n, const VoiceSpec* specs, int numSpecs);

    // voice specs for what should sound now; tlSeconds is the timeline position when playing
    static int buildMix (const Model&, const CellArray&, VoiceSpec* out, int max);
    static int buildTimeline (const Model&, const CellArray&, double tlSeconds, VoiceSpec* out, int max);
    static int timelineVoiceId (int segId, int itemIndex) { return 1000000 + segId * 256 + itemIndex; }

    // published for the editor (pointer positions)
    struct Pub { std::atomic<int> id { -1 }; std::atomic<float> frac { 0 }; };
    std::array<Pub, kSlots> pub;

private:
    struct Slot
    {
        int id = -1;
        const Sound* snd = nullptr;
        double phase = 0;
        float s = 0, e = 1;
        float tgtL = 0, tgtR = 0, gL = 0, gR = 0, env = 0;
        int effect = fx::None;
        bool used = false;
    };
    std::array<Slot, kSlots> slots;
    double sr = 44100.0;
    float kGain = 0.01f, kEnv = 0.05f;
};
