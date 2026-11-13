// Plays the looping sounds. Every voice loops continuously; the timeline picks
// which snapshot's voices sound at any moment. openprocess pans them left / right;
// opensphere (OP_SPATIAL) encodes each one into Ambisonics, 1st to 7th order.
#pragma once
#include "Ambi.h"
#include "Bank.h"
#include "Model.h"
#include <array>
#include <atomic>

#ifndef OP_SPATIAL
 #define OP_SPATIAL 0
#endif

struct VoiceSpec
{
    int id = 0;
    const Sound* snd = nullptr;
    bool live = false;               // plays the live input instead of a sound
    float s = 0, e = 1;
    float gain = 1, pan = 0.5f;
    float az = 0, el = 0, dist = 0.3f;   // opensphere
    int effect = fx::None;
};

using CellArray = std::array<std::shared_ptr<Sound>, bank::kCells>;

class Engine
{
public:
    static constexpr int kSlots = 128;
    static constexpr int kMaxSpecs = 256;
    static constexpr int kSub = 256;   // samples per inner block

    struct Output
    {
        float* const* ch = nullptr;
        int numCh = 2;
        int order = -1;               // -1: plain stereo (left / right pan); 0..7: Ambisonics ACN/SN3D
    };

    void prepare (double sampleRate);
    void reset();
    // adds into the outputs; inL / inR is the live input (may be null)
    void process (const Output&, int n, const VoiceSpec* specs, int numSpecs, const float* inL = nullptr, const float* inR = nullptr);
    // adds into L/R (openprocess)
    void process (float* L, float* R, int n, const VoiceSpec* specs, int numSpecs)
    {
        float* ch[2] = { L, R };
        process (Output { ch, 2, -1 }, n, specs, numSpecs);
    }

    // voice specs for what should sound now; t is the time for the motions (and the timeline position)
    static int buildMix (const Model&, const CellArray&, double t, VoiceSpec* out, int max);
    static int buildTimeline (const Model&, const CellArray&, double tlSeconds, VoiceSpec* out, int max);
    static int timelineVoiceId (int segId, int itemIndex) { return 1000000 + segId * 256 + itemIndex; }
    static VoiceSpec specFor (const Item&, const CellArray&, double t);

    // published for the editor (pointer positions)
    struct Pub { std::atomic<int> id { -1 }; std::atomic<float> frac { 0 }; };
    std::array<Pub, kSlots> pub;

private:
    struct Slot
    {
        int id = -1;
        const Sound* snd = nullptr;
        bool live = false;
        double phase = 0;
        float s = 0, e = 1;
        float tgtL = 0, tgtR = 0, gL = 0, gR = 0, env = 0;
        float az = 0, el = 0, dist = 0.3f, gain = 0;
        ambi::Coefs cL {}, cR {};       // current encoding of the left / right channel
        float air = 1.0f, lpL = 0, lpR = 0;
        int effect = fx::None;
        bool used = false;
    };
    void render (Slot&, int si, const Output&, int off, int n, const float* inL, const float* inR);

    std::array<Slot, kSlots> slots;
    std::array<float, kSub> tmpL {}, tmpR {};
    double sr = 44100.0;
    float kGain = 0.01f, kEnv = 0.05f, kBlock = 0.2f;
};
