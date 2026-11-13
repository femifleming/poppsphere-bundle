// What is on screen: the mix window, the snapshots and the timeline.
#pragma once
#include <juce_data_structures/juce_data_structures.h>
#include <vector>

namespace fx { enum { None = -1, FadeInOut = 0, FadeIn = 1, FadeOut = 2, Mute = 3, Solo = 4, Count = 5 }; }

struct Item                 // one looping sound in the mix window
{
    int id = 0;
    int cell = 0;           // browser cell 0..63
    float x = 0.5f;         // pan: 0 left .. 1 right
    float y = 0.7f;         // volume: 0 soft (bottom) .. 1 loud (top)
    float s = 0.0f, e = 1.0f;   // played part of the sample (fractions)
    int effect = fx::None;
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
    float minutes = 1.0f;       // composition length, 1..4
    float playStart = 0.0f;     // play pointer position on the timeline, 0..1
    bool timelinePlaying = false;
    int nextId = 1;

    static constexpr int kMaxItems = 24, kMaxSnaps = 10, kMaxSegs = 64;

    Item* findItem (int id);
    Segment* findSeg (int id);
    juce::ValueTree toTree() const;
    static Model fromTree (const juce::ValueTree&);
};

inline float volumeGain (float y) { return y * y; }
