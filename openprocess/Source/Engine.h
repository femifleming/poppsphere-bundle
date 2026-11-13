// Plays the looping sounds. Every voice loops continuously; the timeline picks
// which snapshot's voices sound at any moment. openprocess pans them left / right;
// opensphere (OP_SPATIAL) encodes each one into Ambisonics, 1st to 7th order.
#pragma once
#include "Ambi.h"
#include "Bank.h"
#include "Freeze.h"
#include <memory>
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
    float air = 0.5f, spread = 0.5f, blur = 0.0f, nearR = 0.12f;   // its distance settings (its segment's, on the timeline)
    int effect = fx::None;
    float fzSlide = 0.3f, fzDenoise = 0.0f, fzEvery = 0.0f, fzScan = 0.6f;
    int fzGen = 0, fzMode = 0;
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
        const SpaceParams* space = nullptr;
    };

    // opensphere's spatial meter: energy by direction, published every block
    static constexpr int kAzBins = 28, kElBins = 10;
    std::array<std::atomic<float>, kAzBins> meterAz {};
    std::array<std::atomic<float>, kElBins> meterEl {};

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
    // (clock: the motion clock; each sound's motion runs from when it was put on)
    static int buildMix (const Model&, const CellArray&, double clock, VoiceSpec* out, int max, const SpaceParams& = {});
    static int buildTimeline (const Model&, const CellArray&, double tlSeconds, VoiceSpec* out, int max, const SpaceParams& = {});
    static int timelineVoiceId (int segId, int itemIndex) { return 1000000 + segId * 256 + itemIndex; }
    // the boids: gather this moment's birds and move the flock on by dt seconds
    static void feedFlock (Flock&, const Model&, bool timeline, double t, float dt, const SpaceParams&);
    static VoiceSpec specFor (const Item&, const CellArray&, double tau, const SpaceParams&);

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
        float air = 0.5f, spread = 0.5f, blur = 0.0f, nearR = 0.12f;
        ambi::Coefs cL {}, cR {};       // current encoding of the left / right channel
        float lpL = 0, lpR = 0;
        int effect = fx::None;
        bool used = false;
        int fz = -1;                      // its freezer from the pool, while it has the freeze effect
        int fzGen = 0;
        float fzSlide = 0.3f, fzDenoise = 0.0f, fzEvery = 0.0f, fzScan = 0.6f;
        int fzMode = 0;
    };
    void render (Slot&, int si, const Output&, int off, int n, const float* inL, const float* inR);

    std::array<Slot, kSlots> slots;
    static constexpr int kFreezers = 8;   // made up front, so the audio thread never allocates
    std::array<std::unique_ptr<Freezer>, kFreezers> freezers;
    std::array<bool, kFreezers> freezerUsed {};
    std::array<float, kSub> tmpL {}, tmpR {};
    std::array<float, kAzBins> accAz {};
    std::array<float, kElBins> accEl {};
    double sr = 44100.0;
    float kGain = 0.01f, kEnv = 0.05f, kBlock = 0.2f;
};
