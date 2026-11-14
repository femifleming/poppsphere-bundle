// What is on screen: the mix window, the snapshots and the timeline.
#pragma once
#include <juce_data_structures/juce_data_structures.h>
#include <vector>
#include <cmath>
#include <array>

namespace fx { enum { None = -1, FadeInOut = 0, FadeIn = 1, FadeOut = 2, Mute = 3, Solo = 4, Freeze = 5, Count = 6 }; }

// openfield: ways a sound can move through the space by itself
namespace motion { enum { None = -1, Orbit = 0, Swing, Rise, Drift, Spiral, Figure8, Arc, Approach, Hop, Flutter, Boids, Lorenz, Count }; }

// openfield: the live input, placed in the space like a sound from the browser
constexpr int kLiveCell = 64;

struct Item                 // one looping sound in the mix window
{
    int id = 0;
    int cell = 0;           // browser cell 0..63
    float x = 0.5f;         // pan: 0 left .. 1 right
    float y = 0.7f;         // volume: 0 soft (bottom) .. 1 loud (top)
    float s = 0.0f, e = 1.0f;   // played part of the sample (fractions)
    int effect = fx::None;
    // the freeze effect: slide (glide to a new freeze), denoise, re-freeze every (0 = never); fzGen counts "freeze again"
    float fzSlide = 0.3f, fzDenoise = 0.0f, fzEvery = 0.0f;
    int fzGen = 0;
    int fzMode = 0;           // 0 matrix (8 frames at random), 1 scan (sweeps through ~1.4 s)
    float fzScan = 0.6f;      // scan speed
    // openfield only: x is the direction around you, y the height, d the distance
    float d = 0.3f;         // 0 at your head .. 1 far away
    int motion = motion::None;
    double motionStart = 0;  // the motion clock when the motion was put on (each sound runs its own)
    float mrate = 1.0f;      // its speed, 0.25 .. 4
    float mseed = 0.5f;      // its own variation: direction, small differences of rate and path
    bool isLive() const { return cell == kLiveCell; }
};

// openfield's boids: every sounding sound with the boids motion is one bird of a single flock.
// The flock is simulated block by block (it is the one motion that depends on the others), with
// cohesion, separation, alignment, a pull home and a little wandering. Positions are offsets from
// each sound's own place, in play-area units (x wraps round you), plus distance.
struct Flock
{
    static constexpr int kMax = 128;
    struct Bird { int id = -1; float hx = 0, hy = 0, hd = 0; float x = 0, y = 0, d = 0, vx = 0, vy = 0, vd = 0; bool seen = false; float k = 1; };
    std::array<Bird, kMax> birds {};
    juce::uint32 rnd = 12345;

    float noise() { rnd = rnd * 1664525u + 1013904223u; return (float) ((rnd >> 8) & 0xffff) / 32768.0f - 1.0f; }
    void begin() { for (auto& b : birds) b.seen = false; }
    // a bird for this voice at its home place (rate k = its own speed); returns its index or -1
    int add (int id, float hx, float hy, float hd, float k)
    {
        int freeSlot = -1;
        for (int i = 0; i < kMax; ++i)
        {
            if (birds[(size_t) i].id == id) { auto& b = birds[(size_t) i]; b.hx = hx; b.hy = hy; b.hd = hd; b.k = k; b.seen = true; return i; }
            if (freeSlot < 0 && birds[(size_t) i].id < 0) freeSlot = i;
        }
        if (freeSlot < 0) return -1;
        auto& b = birds[(size_t) freeSlot];
        b = Bird();
        b.id = id; b.hx = b.x = hx; b.hy = b.y = hy; b.hd = b.d = hd; b.k = k; b.seen = true;
        b.vx = 0.03f * noise(); b.vy = 0.02f * noise();
        return freeSlot;
    }
    void step (float dt)
    {
        for (auto& b : birds) if (b.id >= 0 && ! b.seen) b.id = -1;
        if (dt <= 0.0f) return;
        for (auto& b : birds)
        {
            if (b.id < 0) continue;
            float cx = 0, cy = 0, cd = 0, ax = 0, ay = 0, ad = 0, sx = 0, sy = 0, sd = 0; int n = 0;
            for (auto& o : birds)
            {
                if (o.id < 0 || &o == &b) continue;
                float dx = o.x - b.x; dx -= std::round (dx);
                const float dy = o.y - b.y, dd = o.d - b.d;
                const float r2 = dx * dx + dy * dy + 0.5f * dd * dd;
                if (r2 > 0.09f) continue;                              // neighbours within about 100 degrees
                cx += dx; cy += dy; cd += dd; ax += o.vx; ay += o.vy; ad += o.vd; ++n;
                if (r2 < 0.004f) { const float k = 1.0f / (r2 + 0.0004f); sx -= dx * k; sy -= dy * k; sd -= dd * k; }
            }
            float fx = 0, fy = 0, fd = 0;
            if (n > 0)
            {
                fx += 0.9f * cx / n + 0.8f * (ax / n - b.vx) + 0.0009f * sx;
                fy += 0.9f * cy / n + 0.8f * (ay / n - b.vy) + 0.0009f * sy;
                fd += 0.6f * cd / n + 0.8f * (ad / n - b.vd) + 0.0009f * sd;
            }
            float hx = b.hx - b.x; hx -= std::round (hx);
            fx += 0.25f * hx + 0.06f * noise();
            fy += 0.25f * (b.hy - b.y) + 0.05f * noise();
            fd += 0.25f * (b.hd - b.d) + 0.04f * noise();
            const float h = dt * b.k;
            b.vx += fx * h; b.vy += fy * h; b.vd += fd * h;
            const float v = std::sqrt (b.vx * b.vx + b.vy * b.vy);
            if (v > 0.09f) { b.vx *= 0.09f / v; b.vy *= 0.09f / v; }
            b.vd = b.vd > 0.08f ? 0.08f : (b.vd < -0.08f ? -0.08f : b.vd);
        }
        for (auto& b : birds)
        {
            if (b.id < 0) continue;
            const float h = dt * b.k;
            b.x += b.vx * h; b.x -= std::floor (b.x);
            b.y += b.vy * h; if (b.y < 0) { b.y = 0; b.vy = -b.vy; } if (b.y > 1) { b.y = 1; b.vy = -b.vy; }
            b.d += b.vd * h; if (b.d < 0) { b.d = 0; b.vd = -b.vd; } if (b.d > 1) { b.d = 1; b.vd = -b.vd; }
        }
    }
    const Bird* find (int id) const { for (auto& b : birds) if (b.id == id) return &b; return nullptr; }
};

// openfield's distance settings and scene rotation (from the parameters)
struct SpaceParams
{
    float rolloff = 0.5f;   // how fast the level falls with distance
    float air = 0.5f;       // how much top far sounds lose
    float spread = 0.5f;    // how wide near sounds are
    float blur = 0.0f;      // how much far sounds lose their sharpness of direction
    float nearR = 0.12f;    // inside this distance a sound spreads all round you
    float yaw = 0.0f, pitch = 0.0f;   // scene rotation, radians
    float mspeed = 1.0f;    // all motions faster or slower
    const Flock* flock = nullptr;
};

struct Snapshot
{
    int id = 0;
    int colour = 0;
    std::vector<Item> items;
    // openfield: the global settings when it was taken (motion speed, distance, scene turn), so the
    // timeline can sequence them
    bool hasSpace = false;
    SpaceParams space;
};

struct Segment              // a snapshot placed on the timeline (keeps its own copy)
{
    int id = 0;
    float start = 0.0f, len = 0.125f;   // fractions of the composition length
    float morph = 0.0f;     // 0: a cut. above 0: the first part of the segment (this fraction of its length)
                            // glides smoothly from the scene before it into this one
    Snapshot snap;

    // worked out by Model::link() (not saved)
    int from = -1;          // index of the segment it glides from (-1: none, it fades in)
    bool handedOn = false;  // a later segment glides out of this one, so it ends where that one starts
    float end = 0.125f;     // where it really stops sounding (fraction)
    bool anySolo = false;
    std::vector<int> match; // for each of its sounds, the same sound in the scene before (-1: none)
    std::vector<int> vid;   // voice ids: a sound that carries on keeps its voice, so its loop runs on
};

// one sound on the timeline at a moment: in a glide it has a place in the scene before (a) and in
// this one (b), or only in one of them (fading out / in); w is how far the glide has come (0..1)
struct TlVoice
{
    const Item* a = nullptr;
    const Item* b = nullptr;
    const Segment* segA = nullptr;
    const Segment* segB = nullptr;
    float w = 1.0f, edge = 1.0f;
    int vid = 0;
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
    void link();    // after any change to the timeline: who glides from whom, and the voice ids
    static int voiceId (int segId, int itemIndex) { return 1000000 + segId * 256 + itemIndex; }

    // every sound of the timeline at t seconds (no allocation: the audio thread calls it)
    template <class F> void forEachTimelineVoice (double t, F&& f) const
    {
        const double L = minutes * 60.0;
        for (const auto& B : segs)
        {
            const double t0 = B.start * L, t1 = std::fmin (L, (double) (B.handedOn ? B.end : B.start + B.len) * L);
            if (t < t0 || t >= t1) continue;
            const double mlen = B.morph * B.len * L;
            float w = 1.0f;
            if (mlen > 0.0)
            {
                float u = (float) ((t - t0) / mlen);
                u = u < 0.0f ? 0.0f : (u > 1.0f ? 1.0f : u);
                w = u * u * (3.0f - 2.0f * u);
            }
            const float edgeIn = mlen > 0.0 ? 1.0f : (float) std::fmin (1.0, (t - t0) / 0.03);
            const float edgeOut = B.handedOn ? 1.0f : (float) std::fmin (1.0, (t1 - t) / 0.03);
            const float edge = std::fmin (edgeIn, edgeOut);
            const Segment* A = (B.from >= 0 && B.from < (int) segs.size() && w < 1.0f) ? &segs[(size_t) B.from] : nullptr;
            for (size_t k = 0; k < B.snap.items.size(); ++k)
            {
                const int j = (A != nullptr && k < B.match.size()) ? B.match[k] : -1;
                TlVoice v;
                v.a = j >= 0 ? &A->snap.items[(size_t) j] : nullptr;
                v.b = &B.snap.items[k];
                v.segA = A; v.segB = &B; v.w = w; v.edge = edge;
                v.vid = k < B.vid.size() ? B.vid[k] : voiceId (B.id, (int) k);
                f (v);
            }
            if (A != nullptr)
                for (size_t j = 0; j < A->snap.items.size(); ++j)
                {
                    bool taken = false;
                    for (int mk : B.match) taken |= mk == (int) j;
                    if (taken) continue;
                    TlVoice v;
                    v.a = &A->snap.items[j];
                    v.segA = A; v.segB = &B; v.w = w; v.edge = edge;
                    v.vid = j < A->vid.size() ? A->vid[j] : voiceId (A->id, (int) j);
                    f (v);
                }
        }
    }
    juce::ValueTree toTree() const;
    static Model fromTree (const juce::ValueTree&);
};

inline float volumeGain (float y) { return y * y; }

// straight ahead, both edges straight behind), bottom to top from 45 degrees below to overhead.
// straight ahead, both edges straight behind), bottom to top from 45 degrees below to overhead.
// openfield's play area is the sphere unrolled: left to right goes once round you (the middle is
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
    inline float clamp01 (float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

    // level by distance. rolloff 0.5: 0 dB at your head, -6 dB at 0.35, about -30 dB at the far edge
    inline float distanceGain (float d, const SpaceParams& sp = {})
    {
        const float a = 0.3f + 1.04f * sp.rolloff;
        float g = 1.0f - a * d;
        g = g < 0.03f ? 0.03f : g;
        return g * g * 1.15f / (1.0f + 0.15f * g);
    }
    // how far a stereo sound is spread: wide when near, a point when far (radians, the whole width)
    inline float width (float d, const SpaceParams& sp = {})
    {
        const float nearW = 20.0f + 120.0f * sp.spread;
        return (nearW - (nearW - 10.0f) * d) * kPi / 180.0f;
    }

    // the Lorenz attractor (sigma 10, rho 28, beta 8/3), integrated once: 30 minutes at 25 points a
    // second, a quarter of Lorenz time per second (one turn of a wing takes about three seconds)
    inline const std::vector<float>& lorenzTable()
    {
        static const std::vector<float> t = []
        {
            const int n = 45000;
            std::vector<float> v ((size_t) n * 3);
            double x = 0.1, y = 0.0, z = 20.0;
            auto f = [] (double a, double b, double c, double& da, double& db, double& dc) {
                da = 10.0 * (b - a); db = a * (28.0 - c) - b; dc = a * b - 8.0 / 3.0 * c; };
            for (int i = 0; i < 2000; ++i) { double a, b, c; f (x, y, z, a, b, c); x += a * 0.005; y += b * 0.005; z += c * 0.005; }
            for (int i = 0; i < n; ++i)
            {
                for (int s = 0; s < 2; ++s)
                {
                    double k1[3], k2[3], k3[3], k4[3]; const double h = 0.005;
                    f (x, y, z, k1[0], k1[1], k1[2]);
                    f (x + h / 2 * k1[0], y + h / 2 * k1[1], z + h / 2 * k1[2], k2[0], k2[1], k2[2]);
                    f (x + h / 2 * k2[0], y + h / 2 * k2[1], z + h / 2 * k2[2], k3[0], k3[1], k3[2]);
                    f (x + h * k3[0], y + h * k3[1], z + h * k3[2], k4[0], k4[1], k4[2]);
                    x += h / 6 * (k1[0] + 2 * k2[0] + 2 * k3[0] + k4[0]);
                    y += h / 6 * (k1[1] + 2 * k2[1] + 2 * k3[1] + k4[1]);
                    z += h / 6 * (k1[2] + 2 * k2[2] + 2 * k3[2] + k4[2]);
                }
                v[(size_t) i * 3] = (float) x; v[(size_t) i * 3 + 1] = (float) y; v[(size_t) i * 3 + 2] = (float) z;
            }
            return v;
        }();
        return t;
    }
    inline void lorenzAt (double sec, float out[3])
    {
        const auto& t = lorenzTable();
        const int n = (int) (t.size() / 3);
        double p = std::fmod (sec * 25.0, (double) (n - 1));
        if (p < 0) p += n - 1;
        const int i = (int) p; const float f = (float) (p - i);
        for (int k = 0; k < 3; ++k) out[k] = t[(size_t) i * 3 + k] + (t[(size_t) (i + 1) * 3 + k] - t[(size_t) i * 3 + k]) * f;
    }

    inline float hash01 (int n, float seed)
    {
        const double v = std::sin (n * 127.1 + seed * 311.7) * 43758.5453;
        return (float) (v - std::floor (v));
    }

    // where a sound is, tau seconds after its motion was put on, in play-area units (x, y) and distance d.
    // Every motion starts exactly where the sound was put (no jump), and each sound has its own rate,
    // direction and small differences (mseed), so two sounds never move as one.
    inline void moved (const Item& i, double tau, float& x, float& y, float& d)
    {
        x = i.x; y = i.y; d = i.d;
        if (i.motion == motion::None) return;
        const double tw = 2.0 * kPi;
        const float sd = i.mseed;
        const double k = i.mrate * (0.85 + 0.3 * sd);             // its own speed
        const double t = tau * k;
        const float dir = sd < 0.5f ? 1.0f : -1.0f;               // its own direction
        switch (i.motion)
        {
            case motion::Orbit: x -= dir * (float) std::fmod (t / 24.0, 1.0); break;              // once round you, 24 s
            case motion::Swing: x += dir * 0.11f * (float) std::sin (tw * t / 8.0); break;        // +-40 degrees, 8 s
            case motion::Rise:  y += (1.0f - y) * 0.5f * (1.0f - (float) std::cos (tw * t / 16.0)); break;  // up overhead and back
            case motion::Drift:
                x += dir * 0.07f * (float) (std::sin ((0.31 + 0.1 * sd) * t) + 0.6 * std::sin ((0.17 + 0.05 * sd) * t));
                y += 0.09f * (float) (std::sin ((0.23 + 0.07 * sd) * t) + 0.5 * std::sin ((0.11 + 0.04 * sd) * t));
                break;
            case motion::Spiral:  // round you while climbing towards overhead and back down, 40 s
                x -= dir * (float) std::fmod (t / 20.0, 1.0);
                y += (1.0f - y) * 0.5f * (1.0f - (float) std::cos (tw * t / 40.0));
                break;
            case motion::Figure8:  // a figure of eight in front of where it was, 10 s
                x += dir * 0.12f * (float) std::sin (tw * t / 10.0);
                y += 0.08f * (float) std::sin (2.0 * tw * t / 10.0);
                break;
            case motion::Arc:  // up over your head to the opposite side and back, 14 s
            {
                const float p = 0.5f * (1.0f - (float) std::cos (tw * t / 14.0));
                const float up = p < 0.5f ? 2.0f * p : 2.0f - 2.0f * p;
                y += (1.0f - y) * up;
                if (p > 0.5f) x += 0.5f;
                break;
            }
            case motion::Approach:  // comes up close to you and goes back, 12 s
                d = d * (0.1f + 0.9f * 0.5f * (1.0f + (float) std::cos (tw * t / 12.0)));
                break;
            case motion::Hop:  // jumps to a new place nearby every 3 s, with a short glide
            {
                auto at = [&] (int n, float& ox, float& oy) {
                    if (n <= 0) { ox = 0; oy = 0; return; }
                    ox = 0.3f * (hash01 (n, sd) - 0.5f);
                    oy = 0.3f * (hash01 (n + 977, sd) - 0.5f);
                };
                const double steps = t / 3.0;
                const int n = (int) std::floor (steps);
                const float f = (float) ((steps - n) * 3.0 / 0.15);
                const float g = f >= 1.0f ? 1.0f : f * f * (3.0f - 2.0f * f);
                float ax, ay, bx, by;
                at (n - 1, ax, ay); at (n, bx, by);
                x += ax + (bx - ax) * g;
                y += ay + (by - ay) * g;
                break;
            }
            case motion::Lorenz:  // its own stretch of the attractor: two wings round you, height and distance
            {
                const double t0 = sd * 1700.0;
                float a[3], b[3];
                lorenzAt (t0, a); lorenzAt (t0 + t, b);
                x += dir * (b[0] - a[0]) / 40.0f * 0.35f;
                y += (b[2] - a[2]) / 50.0f * 0.5f;
                d += (b[1] - a[1]) / 54.0f * 0.4f;
                break;
            }
            case motion::Flutter:  // small quick trembling, like wings
                x += 0.012f * (float) (std::sin (tw * (5.3 + sd) * t) + 0.7 * std::sin (tw * (7.9 - sd) * t));
                y += 0.016f * (float) (std::sin (tw * (4.1 + 0.5 * sd) * t) + 0.6 * std::sin (tw * 9.7 * t));
                d = clamp01 (d + 0.03f * (float) std::sin (tw * 3.3 * t));
                break;
            default: break;
        }
        x -= std::floor (x);
        y = clamp01 (y);
        d = clamp01 (d);
    }
    inline void moved (const Item& i, double tau, float& x, float& y) { float d; moved (i, tau, x, y, d); }

    // a direction turned by the scene rotation (yaw about the vertical, then pitch)
    inline void rotate (float& az, float& el, const SpaceParams& sp)
    {
        if (sp.yaw == 0.0f && sp.pitch == 0.0f) return;
        float vx = std::cos (el) * std::cos (az), vy = std::cos (el) * std::sin (az), vz = std::sin (el);
        const float cy = std::cos (sp.yaw), sy = std::sin (sp.yaw);
        const float x1 = vx * cy - vy * sy, y1 = vx * sy + vy * cy;
        const float cp = std::cos (sp.pitch), spi = std::sin (sp.pitch);
        const float x2 = x1 * cp - vz * spi, z2 = x1 * spi + vz * cp;
        az = std::atan2 (y1, x2);
        el = std::asin (z2 < -1.0f ? -1.0f : (z2 > 1.0f ? 1.0f : z2));
    }
}
