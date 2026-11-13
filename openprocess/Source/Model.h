// What is on screen: the mix window, the snapshots and the timeline.
#pragma once
#include <juce_data_structures/juce_data_structures.h>
#include <vector>
#include <cmath>

namespace fx { enum { None = -1, FadeInOut = 0, FadeIn = 1, FadeOut = 2, Mute = 3, Solo = 4, Count = 5 }; }

// opensphere: ways a sound can move through the space by itself
namespace motion { enum { None = -1, Orbit = 0, Swing = 1, Rise = 2, Drift = 3, Count = 4 }; }

// opensphere: the live input, placed in the space like a sound from the browser
constexpr int kLiveCell = 64;

struct Item                 // one looping sound in the mix window
{
    int id = 0;
    int cell = 0;           // browser cell 0..63
    float x = 0.5f;         // pan: 0 left .. 1 right
    float y = 0.7f;         // volume: 0 soft (bottom) .. 1 loud (top)
    float s = 0.0f, e = 1.0f;   // played part of the sample (fractions)
    int effect = fx::None;
    // opensphere only: x is the direction around you, y the height, d the distance
    float d = 0.3f;         // 0 at your head .. 1 far away
    int motion = motion::None;
    bool isLive() const { return cell == kLiveCell; }
};

struct Snapshot
{
    int id = 0;
    int colour = 0;
    std::vector<Item> items;
};

struct Segment              // a snapshot placed on the timeline (keeps its own copy)
{
    int id = 0;
    float start = 0.0f, len = 0.125f;   // fractions of the composition length
    Snapshot snap;
};

struct Model
{
    std::vector<Item> mix;
    std::vector<Snapshot> snaps;
    std::vector<Segment> segs;
    float minutes = 1.0f;       // composition length, 1..30
    float playStart = 0.0f;     // play pointer position on the timeline, 0..1
    bool timelinePlaying = false;
    int nextId = 1;

    static constexpr int kMaxItems = 24, kMaxSnaps = 10, kMaxSegs = 256;
    static constexpr float kMinMinutes = 1.0f, kMaxMinutes = 30.0f;

    Item* findItem (int id);
    Segment* findSeg (int id);
    juce::ValueTree toTree() const;
    static Model fromTree (const juce::ValueTree&);
};

inline float volumeGain (float y) { return y * y; }

// opensphere's play area is the sphere unrolled: left to right goes once round you (the middle is
// straight ahead, both edges straight behind), bottom to top from 45 degrees below to overhead.
namespace space
{
    constexpr float kPi = 3.14159265358979f;
    constexpr float kElLow = -45.0f, kElHigh = 90.0f;           // degrees
    constexpr float kHorizonY = -kElLow / (kElHigh - kElLow);    // where the horizon is, 0..1
    inline float azimuth (float x) { return (0.5f - x) * 2.0f * kPi; }          // radians, + = left
    inline float elevation (float y) { return (kElLow + y * (kElHigh - kElLow)) * kPi / 180.0f; }
    inline float xForAzimuth (float az) { float x = 0.5f - az / (2.0f * kPi); return x - std::floor (x); }
    inline float yForElevationDeg (float deg) { return (deg - kElLow) / (kElHigh - kElLow); }
    // level by distance: 0 dB at your head, -6 dB at 0.35, about -30 dB at the far edge
    inline float distanceGain (float d) { const float g = 1.0f - 0.82f * d; return g * g * 1.15f / (1.0f + 0.15f * g); }
    // how far a stereo sound is spread: wide when near, a point when far (radians, the whole width)
    inline float width (float d) { return (80.0f - 70.0f * d) * kPi / 180.0f; }

    // where a sound is at time t (seconds) once its motion is applied, in play-area units
    inline void moved (const Item& i, double t, float& x, float& y)
    {
        x = i.x; y = i.y;
        const float ph = (float) ((i.id * 0.6180339887) - std::floor (i.id * 0.6180339887)) * 2.0f * kPi;
        const double tw = 2.0 * kPi;
        switch (i.motion)
        {
            case motion::Orbit: x -= (float) std::fmod (t / 24.0, 1.0); break;                       // once round you in 24 s
            case motion::Swing: x += 0.11f * (float) std::sin (tw * t / 8.0 + ph); break;           // +-40 degrees, 8 s
            case motion::Rise:  y += (1.0f - y) * 0.5f * (1.0f - (float) std::cos (tw * t / 16.0 + ph)); break; // up overhead and back, 16 s
            case motion::Drift:
                x += 0.07f * (float) (std::sin (0.31 * t + ph) + 0.6 * std::sin (0.17 * t + 2.0 * ph));
                y += 0.09f * (float) (std::sin (0.23 * t + 3.0 * ph) + 0.5 * std::sin (0.11 * t + ph));
                break;
            default: break;
        }
        x -= std::floor (x);
        y = y < 0.0f ? 0.0f : (y > 1.0f ? 1.0f : y);
    }
}
