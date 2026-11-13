// Spectral freeze, after Jean-Francois Charles' freeze8 (as used in ppooll's pr.spectfreeze):
// the last 8 analysis frames of a sound (magnitude and phase advance per bin) are held; every hop
// each bin takes one of the 8 frames at random and its phase keeps accumulating, so the frozen
// sound keeps its pitches and shimmers a little. Re-freezing glides to the new frames (slide);
// denoise drops bins below a share of the loudest one. 4096-point FFT, 4x overlap, stereo.
// Scan mode (after Charles' freeze~): the last 64 frames (~1.4 s) are held instead, and playback
// sweeps back and forth through them; at speed 0 it holds the one frame it is on.
#pragma once
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <vector>

class Freezer
{
public:
    static constexpr int kOrder = 12, kN = 1 << kOrder, kHop = kN / 4, kBins = kN / 2 + 1, kFrames = 8, kScan = 64;

    Freezer() : fft (kOrder)
    {
        window.resize (kN);
        for (int i = 0; i < kN; ++i) window[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * i / kN);
        for (auto& c : ch) c.alloc();
        work.resize (kN * 2);
    }

    void reset()
    {
        for (auto& c : ch) c.clear();
        pos = 0; framesSeen = 0; captured = false; wet = 0.0f; sinceFreeze = 0.0;
        scanLen = 0; scanPos = 0.0f; scanDir = 1.0f; wasScan = scanMode;
    }

    // ask for a new freeze (the next hop takes the last 8 frames)
    void freezeAgain() { pending = true; }

    // in place on a block of the voice; slide 0..1, denoise 0..1, every: seconds (0 = never)
    // mode 0: matrix (8 frames at random), 1: scan (sweep through 64 frames at scanSpeed 0..1)
    void process (float* L, float* R, int n, double sampleRate, float slide, float denoise, float every,
                  int mode = 0, float scanSpeed = 0.3f)
    {
        scanMode = mode == 1;
        scanStep = scanSpeed * scanSpeed * 2.0f;    // frames per hop; 0.71 = as recorded
        const float slideHops = 1.0f + slide * slide * 160.0f;        // up to ~4 s at 48 kHz
        for (int i = 0; i < n; ++i)
        {
            float* io[2] = { L, R };
            for (int c = 0; c < 2; ++c)
            {
                auto& s = ch[(size_t) c];
                s.in[(size_t) pos] = io[c][i];
                io[c][i] = io[c][i] * (1.0f - wet) + s.out[(size_t) pos] * wet;
                s.out[(size_t) pos] = 0.0f;
            }
            wet += ((captured ? 1.0f : 0.0f) - wet) * 0.0005f;
            if (++pos >= kN) pos = 0;
            if (++hopCount >= kHop)
            {
                hopCount = 0;
                sinceFreeze += (double) kHop / sampleRate;
                if (every > 0.0f && captured && sinceFreeze >= every) pending = true;
                if (scanMode && ! wasScan) pending = true;   // switching mode freezes again
                wasScan = scanMode;
                hop (slideHops, denoise);
            }
        }
    }

private:
    struct Channel
    {
        std::vector<float> in, out, lastPhase, acc;
        std::vector<float> recMag, recDph;      // ring of the last 8 frames
        std::vector<float> tgtMag, tgtDph;      // the frozen frames
        std::vector<float> playMag;             // slid towards tgtMag
        std::vector<float> ringMag, ringDph;    // scan: the last kScan frames, always recording
        std::vector<float> scanMag, scanDph;    // scan: the frozen stretch
        void alloc()
        {
            ringMag.assign (kScan * kBins, 0); ringDph.assign (kScan * kBins, 0);
            scanMag.assign (kScan * kBins, 0); scanDph.assign (kScan * kBins, 0);
            in.assign (kN, 0); out.assign (kN, 0); lastPhase.assign (kBins, 0); acc.assign (kBins, 0);
            recMag.assign (kFrames * kBins, 0); recDph.assign (kFrames * kBins, 0);
            tgtMag.assign (kFrames * kBins, 0); tgtDph.assign (kFrames * kBins, 0); playMag.assign (kFrames * kBins, 0);
        }
        void clear() { for (auto* v : { &in, &out, &lastPhase, &acc, &recMag, &recDph, &tgtMag, &tgtDph, &playMag,
                                        &ringMag, &ringDph, &scanMag, &scanDph }) std::fill (v->begin(), v->end(), 0.0f); }
    };

    void hop (float slideHops, float denoise)
    {
        const int slot = framesSeen % kFrames;
        const int rslot = framesSeen % kScan;
        ++framesSeen;
        const int need = scanMode ? juce::jmin (kScan, 24) : kFrames;   // scan starts once ~0.5 s is there
        const bool freezeNow = (! captured && framesSeen >= need) || (pending && framesSeen >= need);
        if (freezeNow) { scanLen = juce::jmin (framesSeen, kScan); scanPos = 0.0f; scanDir = 1.0f; scanStart = framesSeen % kScan; }
        for (auto& s : ch)
        {
            // analysis of the last kN samples
            for (int i = 0; i < kN; ++i) work[(size_t) i] = s.in[(size_t) ((pos + i) % kN)] * window[(size_t) i];
            std::fill (work.begin() + kN, work.end(), 0.0f);
            fft.performRealOnlyForwardTransform (work.data(), true);
            for (int b = 0; b < kBins; ++b)
            {
                const float re = work[(size_t) b * 2], im = work[(size_t) b * 2 + 1];
                const float ph = std::atan2 (im, re);
                float d = ph - s.lastPhase[(size_t) b];
                s.lastPhase[(size_t) b] = ph;
                d -= juce::MathConstants<float>::twoPi * std::round (d / juce::MathConstants<float>::twoPi);
                s.recMag[(size_t) (slot * kBins + b)] = std::sqrt (re * re + im * im);
                s.recDph[(size_t) (slot * kBins + b)] = d;
                s.ringMag[(size_t) (rslot * kBins + b)] = s.recMag[(size_t) (slot * kBins + b)];
                s.ringDph[(size_t) (rslot * kBins + b)] = d;
            }
            if (freezeNow)
            {
                s.tgtMag = s.recMag;
                s.tgtDph = s.recDph;
                if (! captured) s.playMag = s.tgtMag;
                if (scanMode) { s.scanMag = s.ringMag; s.scanDph = s.ringDph; }
            }
            if (! captured && ! freezeNow) continue;
            if (scanMode)
            {
                synthScan (s, denoise);
                continue;
            }

            // resynthesis: per bin one of the 8 frames, phase accumulating
            const float k = 1.0f / slideHops;
            float peak = 0.0f;
            for (size_t j = 0; j < s.playMag.size(); ++j) { s.playMag[j] += (s.tgtMag[j] - s.playMag[j]) * k; peak = std::max (peak, s.playMag[j]); }
            const float thr = denoise * denoise * 0.25f * peak;
            for (int b = 0; b < kBins; ++b)
            {
                const int f = (int) (rnd() * kFrames) % kFrames;
                float a = s.playMag[(size_t) (f * kBins + b)];
                // the true phase advance for this bin from the frame's deviation
                const float expected = juce::MathConstants<float>::twoPi * (float) b * kHop / kN;
                float dev = s.tgtDph[(size_t) (f * kBins + b)] - expected;
                dev -= juce::MathConstants<float>::twoPi * std::round (dev / juce::MathConstants<float>::twoPi);
                s.acc[(size_t) b] += expected + dev;
                if (a < thr) a = 0.0f;
                work[(size_t) b * 2] = a * std::cos (s.acc[(size_t) b]);
                work[(size_t) b * 2 + 1] = a * std::sin (s.acc[(size_t) b]);
            }
            for (int b = kBins; b < kN; ++b) { work[(size_t) b * 2] = 0; work[(size_t) b * 2 + 1] = 0; }
            fft.performRealOnlyInverseTransform (work.data());
            // Hann analysis + Hann synthesis at 4x overlap sum to 1.5
            const float g = 1.0f / 1.5f;
            for (int i = 0; i < kN; ++i) s.out[(size_t) ((pos + i) % kN)] += work[(size_t) i] * window[(size_t) i] * g;
        }
        if (freezeNow) { captured = true; pending = false; sinceFreeze = 0.0; }
        if (scanMode && scanLen > 1)
        {
            // ping-pong through the held stretch
            scanPos += scanDir * scanStep;
            if (scanPos > scanLen - 1) { scanPos = 2.0f * (scanLen - 1) - scanPos; scanDir = -1.0f; }
            if (scanPos < 0.0f) { scanPos = -scanPos; scanDir = 1.0f; }
            scanPos = juce::jlimit (0.0f, (float) (scanLen - 1), scanPos);
        }
    }

    void synthScan (Channel& s, float denoise)
    {
        // the frame at scanPos (oldest = 0), magnitudes between the two neighbours, phase from the nearer
        const int i0 = (int) scanPos, i1 = juce::jmin (i0 + 1, scanLen - 1);
        const float f = scanPos - (float) i0;
        auto frameIndex = [&] (int k) { return ((scanStart - scanLen + k) % kScan + kScan) % kScan; };
        const int a0 = frameIndex (i0), a1 = frameIndex (i1), an = f < 0.5f ? a0 : a1;
        float peak = 0.0f;
        for (int b = 0; b < kBins; ++b) peak = std::max (peak, s.scanMag[(size_t) (a0 * kBins + b)]);
        const float thr = denoise * denoise * 0.25f * peak;
        for (int b = 0; b < kBins; ++b)
        {
            float a = s.scanMag[(size_t) (a0 * kBins + b)] * (1.0f - f) + s.scanMag[(size_t) (a1 * kBins + b)] * f;
            const float expected = juce::MathConstants<float>::twoPi * (float) b * kHop / kN;
            float dev = s.scanDph[(size_t) (an * kBins + b)] - expected;
            dev -= juce::MathConstants<float>::twoPi * std::round (dev / juce::MathConstants<float>::twoPi);
            s.acc[(size_t) b] += expected + dev;
            if (a < thr) a = 0.0f;
            work[(size_t) b * 2] = a * std::cos (s.acc[(size_t) b]);
            work[(size_t) b * 2 + 1] = a * std::sin (s.acc[(size_t) b]);
        }
        for (int b = kBins; b < kN; ++b) { work[(size_t) b * 2] = 0; work[(size_t) b * 2 + 1] = 0; }
        fft.performRealOnlyInverseTransform (work.data());
        const float g = 1.0f / 1.5f;
        for (int i = 0; i < kN; ++i) s.out[(size_t) ((pos + i) % kN)] += work[(size_t) i] * window[(size_t) i] * g;
    }

    float rnd() { seed = seed * 1664525u + 1013904223u; return (float) (seed >> 8) / 16777216.0f; }

    juce::dsp::FFT fft;
    std::vector<float> window, work;
    std::array<Channel, 2> ch;
    int pos = 0, hopCount = 0, framesSeen = 0;
    bool captured = false, pending = false, scanMode = false, wasScan = false;
    int scanLen = 0, scanStart = 0;
    float scanPos = 0.0f, scanDir = 1.0f, scanStep = 0.18f;
    float wet = 0.0f;
    double sinceFreeze = 0.0;
    juce::uint32 seed = 22222;
};
