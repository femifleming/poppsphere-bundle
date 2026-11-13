// Quell: dynamic resonance suppressor engine.
//
// Each hop the last N input samples are analysed with a zero-padded 2N FFT. A per-bin reduction curve is
// detected (Soft: relative to the local spectral envelope; Hard: against an absolute, depth-dependent
// threshold), smoothed across frequency (Detail) and in time (Attack/Release), scaled by the depth curve
// drawn with the bands, clamped to Max cut and turned into a filter:
//   - minimum phase (cepstrum) or linear phase, applied by weighted overlap-add with no synthesis window,
//     the filter's impulse response windowed to N (min) or +-N/2 (linear) so nothing wraps;
//   - low latency: the same minimum-phase filter truncated to a short FIR run sample by sample (no lookahead).
// Mix, wet trim, delta and band listen are folded into the filter, so dry and wet always line up.
#pragma once
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <cmath>
#include <vector>

namespace ql
{
constexpr int kBands = 8;
enum BandType { LowCut, LowShelf, HighShelf, HighCut, Bell, BandPass, BandReject, Tilt, kNumTypes };
inline const char* bandTypeName (int t)
{
    static const char* n[] = { "Low cut", "Low shelf", "High shelf", "High cut", "Bell", "Band pass", "Band reject", "Tilt" };
    return n[juce::jlimit (0, kNumTypes - 1, t)];
}
inline bool typeUsesSlope (int t) { return t == LowCut || t == HighCut || t == LowShelf || t == HighShelf || t == Tilt; }
inline bool typeHasGain (int t) { return t != LowCut && t != HighCut && t != BandReject; }

struct Band
{
    bool used = false, on = true;
    int type = Bell;
    float freq = 1000.0f, gain = 0.0f, q = 1.0f, focus = 0.0f; // gain in dB (depth change), focus -1 (L/M) .. +1 (R/S)
    bool operator== (const Band&) const = default;
};

struct Settings
{
    int mode = 0;                       // 0 soft, 1 hard
    float depth = 5, detail = 5, attack = 5, release = 5; // 0..10 (depth 0..20)
    float detailTilt[2] {}, attackTilt[2] {}, releaseTilt[2] {}; // low, high: -1..1
    int stereoMode = 0;                 // 0 L/R, 1 M/S
    float link = 1, focus = 0;          // 0..1, -1..1
    float maxCut = 40, wetTrim = 0, mix = 1, outGain = 0; // dB, dB, 0..1, dB
    bool delta = false, bypass = false, sidechain = false;
    int quality = 0;                    // 0 normal, 1 high, 2 ultra
    bool linearPhase = false, lowLatency = false;
    int listenBand = -1;
    std::array<Band, kBands> bands {};

    // things that change per-bin tables
    bool sameTables (const Settings& o) const
    {
        return mode == o.mode && depth == o.depth && detail == o.detail && attack == o.attack && release == o.release
            && std::equal (detailTilt, detailTilt + 2, o.detailTilt) && std::equal (attackTilt, attackTilt + 2, o.attackTilt)
            && std::equal (releaseTilt, releaseTilt + 2, o.releaseTilt) && focus == o.focus && stereoMode == o.stereoMode
            && bands == o.bands && listenBand == o.listenBand;
    }
};

// magnitude helpers for the depth curve -----------------------------------------------------------
struct CurvePoint { float db = 0, mul = 1, pass = -1; }; // dB added to depth, multiplier (cuts), band-pass membership (-1 = n/a)

inline float slopeOrder (float q) { return juce::jlimit (0.5f, 8.0f, q); } // slopes: q is used as the order (6 dB/oct each)

inline void bandAt (const Band& b, float f, CurvePoint& p)
{
    const float r = f / b.freq, lr = std::log2 (juce::jmax (1.0e-6f, r));
    switch (b.type)
    {
        case LowCut:   p.mul *= 1.0f / std::sqrt (1.0f + std::pow (1.0f / juce::jmax (1.0e-6f, r), 2.0f * slopeOrder (b.q))); break;
        case HighCut:  p.mul *= 1.0f / std::sqrt (1.0f + std::pow (r, 2.0f * slopeOrder (b.q))); break;
        case LowShelf: p.db += b.gain / (1.0f + std::pow (2.0f, 2.0f * slopeOrder (b.q) * lr * 0.5f)); break;
        case HighShelf:p.db += b.gain / (1.0f + std::pow (2.0f, -2.0f * slopeOrder (b.q) * lr * 0.5f)); break;
        case Tilt:     p.db += b.gain * 0.5f * std::tanh (lr * slopeOrder (b.q) * 0.5f); break;
        case Bell:
        {
            // analog peaking EQ magnitude (RBJ), in dB
            const float A = std::pow (10.0f, b.gain / 40.0f), w = r, Q = juce::jmax (0.05f, b.q);
            const float re = 1.0f - w * w, num = re * re + (w * A / Q) * (w * A / Q), den = re * re + (w / (A * Q)) * (w / (A * Q));
            p.db += 10.0f * std::log10 (num / den);
            break;
        }
        case BandPass:
        {
            const float Q = juce::jmax (0.05f, b.q), x = Q * (r - 1.0f / juce::jmax (1.0e-6f, r));
            const float m = 1.0f / (1.0f + x * x);
            p.pass = juce::jmax (p.pass, m);
            p.db += b.gain * m;
            break;
        }
        case BandReject:
        {
            const float Q = juce::jmax (0.05f, b.q), x = Q * (r - 1.0f / juce::jmax (1.0e-6f, r));
            p.mul *= 1.0f - 1.0f / (1.0f + x * x);
            break;
        }
        default: break;
    }
}

// band region used by band listen (solo the band's frequency region)
inline float bandRegion (const Band& b, float f)
{
    const float r = f / b.freq;
    switch (b.type)
    {
        case LowCut:    return 1.0f - 1.0f / std::sqrt (1.0f + std::pow (1.0f / juce::jmax (1.0e-6f, r), 2.0f * slopeOrder (b.q)));
        case HighCut:   return 1.0f - 1.0f / std::sqrt (1.0f + std::pow (r, 2.0f * slopeOrder (b.q)));
        case LowShelf:  return 1.0f / (1.0f + std::pow (r, 2.0f));
        case HighShelf: return 1.0f / (1.0f + std::pow (1.0f / juce::jmax (1.0e-6f, r), 2.0f));
        case Tilt:      return 1.0f;
        default:
        {
            const float Q = juce::jmax (0.3f, b.q), x = Q * (r - 1.0f / juce::jmax (1.0e-6f, r));
            return 1.0f / (1.0f + x * x);
        }
    }
}

// depth-curve value per channel (0 = L/M, 1 = R/S), the same function the editor draws
inline float depthCurve (const Settings& s, float f, int ch)
{
    CurvePoint p;
    float db = 0, mul = 1, pass = -1;
    for (const auto& b : s.bands)
    {
        if (! b.used || ! b.on) continue;
        CurvePoint q;
        bandAt (b, f, q);
        // per-band stereo focus: weight of this band's effect on this channel
        const float w = ch == 0 ? juce::jlimit (0.0f, 1.0f, 1.0f - b.focus) : juce::jlimit (0.0f, 1.0f, 1.0f + b.focus);
        db += q.db * w;
        mul *= 1.0f + (q.mul - 1.0f) * w;
        if (q.pass >= 0) pass = juce::jmax (pass, q.pass * w + (1.0f - w));
    }
    juce::ignoreUnused (p);
    if (pass >= 0) mul *= pass; // band passes: only their regions (in parallel) are processed
    const float sw = ch == 0 ? juce::jlimit (0.0f, 1.0f, 1.0f - s.focus) : juce::jlimit (0.0f, 1.0f, 1.0f + s.focus);
    return juce::jmax (0.0f, std::pow (10.0f, db / 20.0f) * mul * sw);
}

// low / high weights for the tilt controls: below ~500 Hz and above ~2 kHz, gentle slopes
inline float lowWeight (float f)  { return 1.0f / (1.0f + std::pow (f / 450.0f, 1.6f)); }
inline float highWeight (float f) { return 1.0f / (1.0f + std::pow (2000.0f / juce::jmax (1.0f, f), 1.6f)); }
inline float tiltScale (const float* t, float f) { return std::pow (2.0f, 1.6f * (t[0] * lowWeight (f) + t[1] * highWeight (f))); }

//==================================================================================================
class Engine
{
public:
    static constexpr int kDisplayPoints = 512;
    struct Display
    {
        std::array<float, kDisplayPoints> red[2] {}, input {};  // reduction dB (>= 0) per channel, input level dB
        int channels = 2;
    };

    void prepare (double sampleRate, int numChannels)
    {
        sr = sampleRate;
        chans = juce::jlimit (1, 2, numChannels);
        n = sr <= 50000.0 ? 2048 : sr <= 100000.0 ? 4096 : 8192;
        order = (int) std::round (std::log2 ((double) n * 2.0));
        fft = std::make_unique<juce::dsp::FFT> (order);
        M = n * 2;
        bins = n + 1;
        firLen = n / 8;
        win.resize ((size_t) n);
        for (int i = 0; i < n; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * ((float) i + 0.5f) / (float) n);
        freq.resize ((size_t) bins);
        for (int k = 0; k < bins; ++k) freq[(size_t) k] = (float) (k * sr / M);
        for (auto* v : { &envLo, &envHi, &smLo, &smHi }) v->assign ((size_t) bins, 0);
        for (auto* v : { &coefA, &coefR, &tmp, &tmp2, &L, &Ldisp, &gainMag, &listenMask }) v->assign ((size_t) bins, 0.0f);
        for (int c = 0; c < 2; ++c)
        {
            red[c].assign ((size_t) bins, 0.0f);
            redOut[c].assign ((size_t) bins, 0.0f);
            curve[c].assign ((size_t) bins, 1.0f);
            inRing[c].assign ((size_t) n, 0.0f);
            scRing[c].assign ((size_t) n, 0.0f);
            outAcc[c].assign ((size_t) (M + n), 0.0f);
            fir[c][0].assign ((size_t) firLen, 0.0f);
            fir[c][1].assign ((size_t) firLen, 0.0f);
            firHist[c].assign ((size_t) firLen * 2, 0.0f);
            X[c].assign ((size_t) M * 2, 0.0f);
            P[c].assign ((size_t) bins, 0.0f);
        }
        work.assign ((size_t) M * 2, 0.0f);
        work2.assign ((size_t) M * 2, 0.0f);
        filt.assign ((size_t) M * 2, 0.0f);
        meterLo = juce::jmax (1, (int) (20.0 * M / sr)); meterHi = juce::jmin (bins - 1, (int) (20000.0 * M / sr));
        displayBinLo.resize (kDisplayPoints);
        displayBinHi.resize (kDisplayPoints);
        for (int i = 0; i < kDisplayPoints; ++i)
        {
            const double f0 = 20.0 * std::pow (1000.0, (double) i / kDisplayPoints), f1 = 20.0 * std::pow (1000.0, (double) (i + 1) / kDisplayPoints);
            displayBinLo[(size_t) i] = juce::jlimit (1, bins - 1, (int) std::floor (f0 * M / sr));
            displayBinHi[(size_t) i] = juce::jlimit (displayBinLo[(size_t) i] + 1, bins, (int) std::ceil (f1 * M / sr) + 1);
        }
        tablesValid = false;
        reset();
    }

    void reset()
    {
        for (int c = 0; c < 2; ++c)
        {
            std::fill (red[c].begin(), red[c].end(), 0.0f);
            std::fill (inRing[c].begin(), inRing[c].end(), 0.0f);
            std::fill (scRing[c].begin(), scRing[c].end(), 0.0f);
            std::fill (outAcc[c].begin(), outAcc[c].end(), 0.0f);
            std::fill (firHist[c].begin(), firHist[c].end(), 0.0f);
            for (auto& f : fir[c]) { std::fill (f.begin(), f.end(), 0.0f); f[0] = 1.0f; }
        }
        ringPos = 0; sinceHop = 0; accPos = 0; firPos = 0; firFade = 1.0f;
    }

    int fftSize() const { return n; }
    // latency in samples for a settings combination
    int latencyFor (bool lowLatency, bool linear) const { return lowLatency ? 0 : linear ? n + n / 2 : n; }

    int hopFor (const Settings& s) const
    {
        if (s.lowLatency) return n / 8;
        return n / (s.quality == 2 ? 32 : s.quality == 1 ? 16 : 8);
    }

    // in-place processing; sc may be null. Channels are already in L/R or M/S as the settings say.
    void process (float* const* io, const float* const* sc, int numSamples, const Settings& s)
    {
        if (! tablesValid || ! s.sameTables (cur) || s.lowLatency != cur.lowLatency || s.linearPhase != cur.linearPhase)
        {
            const bool pathChanged = tablesValid && (s.lowLatency != cur.lowLatency || s.linearPhase != cur.linearPhase);
            cur = s;
            buildTables();
            if (pathChanged) reset();
        }
        cur = s;
        hop = hopFor (s);

        for (int i = 0; i < numSamples; ++i)
        {
            for (int c = 0; c < chans; ++c)
            {
                inRing[c][(size_t) ringPos] = io[c][i];
                scRing[c][(size_t) ringPos] = sc != nullptr ? sc[c][i] : io[c][i];
            }
            ringPos = (ringPos + 1) % n;

            if (s.lowLatency)
            {
                for (int c = 0; c < chans; ++c) io[c][i] = runFir (c, io[c][i]);
                firFade = juce::jmin (1.0f, firFade + 1.0f / (float) hop);
            }
            else
            {
                for (int c = 0; c < chans; ++c)
                {
                    float& a = outAcc[c][(size_t) accPos];
                    io[c][i] = a;
                    a = 0.0f;
                }
                accPos = (accPos + 1) % (M + n);
            }

            if (++sinceHop >= hop)
            {
                sinceHop = 0;
                frame();
            }
        }
    }

    // display hand-off (audio thread writes, editor reads)
    bool readDisplay (Display& d)
    {
        const juce::SpinLock::ScopedTryLockType l (displayLock);
        if (! l.isLocked() || ! displayFresh) return false;
        d = display;
        displayFresh = false;
        return true;
    }

    float meterReduction (int c) const { return maxRed[c].load(); }

private:
    //----------------------------------------------------------------------------------------------
    void buildTables()
    {
        const auto& s = cur;
        // frequency smoothing widths (bins) from Detail and Detail tilt
        for (int k = 0; k < bins; ++k)
        {
            const float f = juce::jmax (freq[(size_t) k], (float) (sr / M));
            const float d = juce::jlimit (0.0f, 1.0f, s.detail / 10.0f * tiltScale (s.detailTilt, f));
            const float envOct = std::pow (2.0f, juce::jmap (d, std::log2 (1.2f), std::log2 (0.22f)));
            const float smOct = std::pow (2.0f, juce::jmap (d, std::log2 (0.5f), std::log2 (1.0f / 60.0f)));
            auto halfBins = [&] (float oct) { return (float) k * (std::pow (2.0f, oct * 0.5f) - 1.0f); };
            envHi[(size_t) k] = juce::jlimit (1, bins - 1, (int) std::round (halfBins (envOct) + 2.0f));
            smHi[(size_t) k] = juce::jlimit (0, bins - 1, (int) std::round (halfBins (smOct) * (s.mode == 1 ? 0.8f : 1.0f)));
        }
        // time constants (seconds at 1 kHz, faster at high frequencies), per hop applied in frame()
        for (int k = 0; k < bins; ++k)
        {
            const float f = juce::jlimit (30.0f, 20000.0f, freq[(size_t) k]);
            float ta = 0.0006f * std::pow (2.0f, s.attack * 0.75f) * std::pow (1000.0f / f, 0.35f) * tiltScale (s.attackTilt, f);
            float tr = 0.006f * std::pow (2.0f, s.release * 0.72f) * std::pow (1000.0f / f, 0.3f) * tiltScale (s.releaseTilt, f);
            if (s.mode == 0 && s.detail > 6.0f) { ta *= 1.0f + (s.detail - 6.0f) * 0.12f; tr *= 1.0f + (s.detail - 6.0f) * 0.08f; }
            coefA[(size_t) k] = ta; coefR[(size_t) k] = tr;
        }
        for (int c = 0; c < 2; ++c)
            for (int k = 0; k < bins; ++k) curve[c][(size_t) k] = depthCurve (s, juce::jmax (1.0f, freq[(size_t) k]), c);
        if (s.listenBand >= 0 && s.listenBand < kBands && s.bands[(size_t) s.listenBand].used)
            for (int k = 0; k < bins; ++k) listenMask[(size_t) k] = bandRegion (s.bands[(size_t) s.listenBand], juce::jmax (1.0f, freq[(size_t) k]));
        tablesValid = true;
    }

    // centred moving average with per-bin half widths (prefix sums)
    void smooth (const std::vector<float>& in, std::vector<float>& out, const std::vector<int>& half)
    {
        prefix.resize ((size_t) bins + 1);
        prefix[0] = 0.0;
        for (int k = 0; k < bins; ++k) prefix[(size_t) k + 1] = prefix[(size_t) k] + in[(size_t) k];
        for (int k = 0; k < bins; ++k)
        {
            const int h = half[(size_t) k], a = juce::jmax (0, k - h), b = juce::jmin (bins, k + h + 1);
            out[(size_t) k] = (float) ((prefix[(size_t) b] - prefix[(size_t) a]) / (double) (b - a));
        }
    }

    void frame()
    {
        const auto& s = cur;
        const float norm = 4.0f / (float) n;  // full-scale sine -> 1.0
        const float hopSec = (float) hop / (float) sr;

        // 1. analyse input (for filtering) and detector signal
        for (int c = 0; c < chans; ++c)
        {
            loadFrame (inRing[c], X[c]);
            fft->performRealOnlyForwardTransform (X[c].data(), true);
            if (s.sidechain)
            {
                loadFrame (scRing[c], work);
                fft->performRealOnlyForwardTransform (work.data(), true);
                powerOf (work, P[c], norm);
            }
            else powerOf (X[c], P[c], norm);
        }

        float frameMax[2] {};
        for (int c = 0; c < chans; ++c)
        {
            // 2. stereo link: blend own detector with the channel average
            const int o = chans == 2 ? 1 - c : c;
            const float ln = chans == 2 ? s.link : 0.0f;
            for (int k = 0; k < bins; ++k)
            {
                const float p = (1.0f - ln) * P[c][(size_t) k] + ln * 0.5f * (P[c][(size_t) k] + P[o][(size_t) k]);
                L[(size_t) k] = 10.0f * std::log10 (p + 1.0e-12f);
            }
            if (c == 0)
                for (int k = 0; k < bins; ++k)
                    Ldisp[(size_t) k] = chans == 2 ? 10.0f * std::log10 (0.5f * (P[0][(size_t) k] + P[1][(size_t) k]) + 1.0e-12f) : L[(size_t) k];

            // 3. spectral envelope (two passes of a log-width box) and the excess above it
            smooth (L, tmp, envHi);
            smooth (tmp, tmp2, envHi);
            const float depthN = s.depth / 10.0f;
            for (int k = 0; k < bins; ++k)
            {
                const float ex = L[(size_t) k] - tmp2[(size_t) k];
                float r;
                if (s.mode == 0)
                {
                    // soft: adaptive (relative) threshold, soft knee
                    const float x = ex - 2.5f;
                    const float knee = x > 3.0f ? x : x < -3.0f ? 0.0f : (x + 3.0f) * (x + 3.0f) / 12.0f;
                    r = knee * depthN * 1.1f;
                }
                else
                {
                    // hard: absolute threshold that falls with depth, weighted by how peaky the bin is
                    const float thr = -18.0f - s.depth * 3.6f;
                    const float over = juce::jmax (0.0f, L[(size_t) k] - thr);
                    const float peak = juce::jlimit (0.0f, 1.0f, (ex + 4.0f) / 10.0f);
                    const float ratio = 1.0f + s.depth * 0.7f;
                    r = over * (1.0f - 1.0f / ratio) * peak;
                }
                if (L[(size_t) k] < -110.0f) r = 0.0f;
                tmp[(size_t) k] = r;
            }
            // 4. Detail: width of the cuts
            smooth (tmp, tmp2, smHi);
            smooth (tmp2, tmp, smHi);
            // 5. time: attack / release per bin
            auto& R = red[c];
            for (int k = 0; k < bins; ++k)
            {
                const float target = tmp[(size_t) k];
                float& r = R[(size_t) k];
                const float t = target > r ? coefA[(size_t) k] : coefR[(size_t) k];
                r += (target - r) * (1.0f - std::exp (-hopSec / t));
            }
            // 6. depth curve, max cut, gain
            const float wet = std::pow (10.0f, s.wetTrim / 20.0f);
            auto& ro = redOut[c];
            for (int k = 0; k < bins; ++k)
            {
                const float rr = juce::jmin (s.maxCut, R[(size_t) k] * curve[c][(size_t) k]);
                ro[(size_t) k] = rr;
                const float g = std::pow (10.0f, -rr / 20.0f) * wet;
                gainMag[(size_t) k] = 1.0f - s.mix + s.mix * g;
                if (k >= meterLo && k <= meterHi && rr * s.mix > frameMax[c]) { frameMax[c] = rr * s.mix; maxBin = k; }
            }
            // 7. build the filter and apply it
            buildFilter (s);
            if (s.lowLatency) loadFir (c);
            else applyFilter (c);
        }
        for (int c = 0; c < chans; ++c) maxRed[c].store (frameMax[c]);
        if (s.lowLatency) firFade = 0.0f;
        publishDisplay (s);
    }

    void loadFrame (const std::vector<float>& ring, std::vector<float>& dst)
    {
        std::fill (dst.begin(), dst.end(), 0.0f);
        for (int i = 0; i < n; ++i) dst[(size_t) i] = ring[(size_t) ((ringPos + i) % n)] * win[(size_t) i];
    }

    void powerOf (const std::vector<float>& spec, std::vector<float>& p, float norm)
    {
        for (int k = 0; k < bins; ++k)
        {
            const float re = spec[(size_t) (2 * k)] * norm, im = spec[(size_t) (2 * k + 1)] * norm;
            p[(size_t) k] = re * re + im * im;
        }
    }

    // gainMag (zero phase, bins) -> filt (complex spectrum of a causal impulse response, M points)
    void buildFilter (const Settings& s)
    {
        const bool listen = s.listenBand >= 0 && s.listenBand < kBands && s.bands[(size_t) s.listenBand].used;
        const bool deltaOut = s.delta || listen;
        if (s.linearPhase && ! s.lowLatency)
        {
            for (int k = 0; k < bins; ++k)
            {
                float m = gainMag[(size_t) k];
                if (deltaOut) m = 1.0f - m;
                if (listen) m *= listenMask[(size_t) k];
                filt[(size_t) (2 * k)] = m; filt[(size_t) (2 * k + 1)] = 0.0f;
            }
            irWindowAndBack (true);
            return;
        }
        // minimum phase via the folded real cepstrum
        for (int k = 0; k < bins; ++k) { work[(size_t) (2 * k)] = std::log (juce::jmax (1.0e-5f, gainMag[(size_t) k])); work[(size_t) (2 * k + 1)] = 0.0f; }
        fft->performRealOnlyInverseTransform (work.data());
        // fold: c[0], 2c[1..M/2-1], c[M/2], 0
        for (int i = 1; i < M / 2; ++i) work[(size_t) i] *= 2.0f;
        for (int i = M / 2 + 1; i < M; ++i) work[(size_t) i] = 0.0f;
        for (int i = M; i < 2 * M; ++i) work[(size_t) i] = 0.0f;
        fft->performRealOnlyForwardTransform (work.data(), true);
        for (int k = 0; k < bins; ++k)
        {
            const float mag = std::exp (work[(size_t) (2 * k)]), ph = work[(size_t) (2 * k + 1)];
            float re = mag * std::cos (ph), im = mag * std::sin (ph);
            if (deltaOut) { re = 1.0f - re; im = -im; }
            if (listen) { re *= listenMask[(size_t) k]; im *= listenMask[(size_t) k]; }
            filt[(size_t) (2 * k)] = re; filt[(size_t) (2 * k + 1)] = im;
        }
        irWindowAndBack (false);
    }

    // impulse response: limit to N samples (min phase: [0,N), linear: [-N/2,N/2] then delayed by N/2)
    void irWindowAndBack (bool linear)
    {
        std::copy (filt.begin(), filt.begin() + 2 * bins, work.begin());
        std::fill (work.begin() + 2 * bins, work.end(), 0.0f);
        fft->performRealOnlyInverseTransform (work.data());
        std::fill (work2.begin(), work2.end(), 0.0f);
        if (linear)
        {
            const int h = n / 2, taper = n / 8;
            for (int i = -h; i <= h; ++i)
            {
                const int a = std::abs (i);
                float w = 1.0f;
                if (a > h - taper) w = 0.5f + 0.5f * std::cos (juce::MathConstants<float>::pi * (float) (a - (h - taper)) / (float) taper);
                work2[(size_t) (i + h)] = work[(size_t) ((i + M) % M)] * w;
            }
        }
        else
        {
            const int len = cur.lowLatency ? firLen : n, taper = len / 4;
            for (int i = 0; i < len; ++i)
            {
                float w = 1.0f;
                if (i > len - taper) w = 0.5f + 0.5f * std::cos (juce::MathConstants<float>::pi * (float) (i - (len - taper)) / (float) taper);
                work2[(size_t) i] = work[(size_t) i] * w;
            }
        }
        if (cur.lowLatency) { std::copy (work2.begin(), work2.begin() + firLen, firStage.begin()); return; }
        fft->performRealOnlyForwardTransform (work2.data(), true);
        std::copy (work2.begin(), work2.begin() + 2 * bins, filt.begin());
    }

    void applyFilter (int c)
    {
        const float olaNorm = 2.0f * (float) hop / (float) n; // Hann at hop H sums to N/(2H)
        auto& x = X[c];
        for (int k = 0; k < bins; ++k)
        {
            const float a = x[(size_t) (2 * k)], b = x[(size_t) (2 * k + 1)], fr = filt[(size_t) (2 * k)], fi = filt[(size_t) (2 * k + 1)];
            work[(size_t) (2 * k)] = (a * fr - b * fi) * olaNorm;
            work[(size_t) (2 * k + 1)] = (a * fi + b * fr) * olaNorm;
        }
        std::fill (work.begin() + 2 * bins, work.end(), 0.0f);
        fft->performRealOnlyInverseTransform (work.data());
        // frame covers input [t-N, t); output sample written at accPos is emitted now; latency N for causal
        // filters, so frame start t-N lands at accPos (emitted N samples from now), lasting M samples
        const int total = M + n;
        for (int i = 0; i < M; ++i)
            outAcc[c][(size_t) ((accPos + i) % total)] += work[(size_t) i];
    }

    // low latency: FIR crossfaded from the previous filter
    std::vector<float> firStage = std::vector<float> (8192, 0.0f);
    void loadFir (int c)
    {
        std::swap (fir[c][0], fir[c][1]);          // [1] = previous
        std::copy (firStage.begin(), firStage.begin() + firLen, fir[c][0].begin());
    }
    float runFir (int c, float x)
    {
        auto& h = firHist[c];
        // doubled history buffer so the dot product reads one contiguous block
        const int p = (firPos + (c == chans - 1 ? 0 : 0)) % firLen;
        juce::ignoreUnused (p);
        h[(size_t) firPosC[c]] = x;
        h[(size_t) (firPosC[c] + firLen)] = x;
        const float* hist = h.data() + firPosC[c] + 1; // oldest .. newest
        float yNew = 0, yOld = 0;
        const float* a = fir[c][0].data();
        const float* b = fir[c][1].data();
        for (int i = 0; i < firLen; ++i)
        {
            const float v = hist[firLen - 1 - i];
            yNew += a[i] * v;
            yOld += b[i] * v;
        }
        firPosC[c] = (firPosC[c] + 1) % firLen;
        return yOld + (yNew - yOld) * firFade;
    }

    void publishDisplay (const Settings& s)
    {
        const juce::SpinLock::ScopedTryLockType l (displayLock);
        if (! l.isLocked()) return;
        display.channels = (s.stereoMode == 0 && s.link >= 0.999f && chans == 2) ? 2 : chans;
        display.channels = chans;
        for (int i = 0; i < kDisplayPoints; ++i)
        {
            const int a = displayBinLo[(size_t) i], b = displayBinHi[(size_t) i];
            float mx = -200.0f, r0 = 0, r1 = 0;
            for (int k = a; k < b && k < bins; ++k)
            {
                mx = juce::jmax (mx, Ldisp[(size_t) k]);
                r0 = juce::jmax (r0, redOut[0][(size_t) k]);
                if (chans == 2) r1 = juce::jmax (r1, redOut[1][(size_t) k]);
            }
            display.input[(size_t) i] = mx;
            display.red[0][(size_t) i] = r0 * s.mix;
            display.red[1][(size_t) i] = (chans == 2 ? r1 : r0) * s.mix;
        }
        displayFresh = true;
    }

    double sr = 48000;
    int chans = 2, n = 2048, M = 4096, bins = 2049, order = 12, hop = 256, firLen = 256;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> win, freq, coefA, coefR, tmp, tmp2, L, Ldisp, gainMag, listenMask, work, work2, filt;
    std::vector<int> envLo, envHi, smLo, smHi, displayBinLo, displayBinHi;
    std::vector<double> prefix;
    std::vector<float> red[2], redOut[2], curve[2], inRing[2], scRing[2], outAcc[2], X[2], P[2], firHist[2];
    std::vector<float> fir[2][2];
    int firPosC[2] {};
    int meterLo = 1, meterHi = 1;
public:
    int maxBin = 0;
private:
    int ringPos = 0, sinceHop = 0, accPos = 0, firPos = 0;
    float firFade = 1.0f;
    Settings cur;
    bool tablesValid = false;
    std::atomic<float> maxRed[2] {};
    juce::SpinLock displayLock;
    Display display;
    bool displayFresh = false;
};
} // namespace ql
