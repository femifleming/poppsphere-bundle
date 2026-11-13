#include "Bank.h"
#include <array>
#include <cmath>

namespace
{
using juce::uint32;
constexpr double kTwoPi = juce::MathConstants<double>::twoPi;

struct Rng
{
    uint32 s;
    explicit Rng (uint32 seed) : s (seed * 2654435761u + 0x9E3779B9u) { if (s == 0) s = 1; next(); next(); }
    uint32 next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float uni() { return (float) (next() >> 8) * (1.0f / 16777216.0f); }
    float range (float a, float b) { return a + (b - a) * uni(); }
    int below (int n) { return n <= 1 ? 0 : (int) (next() % (uint32) n); }
    bool chance (float p) { return uni() < p; }
};

struct SineTable
{
    std::array<float, 4097> t {};
    SineTable() { for (int i = 0; i <= 4096; ++i) t[(size_t) i] = (float) std::sin (kTwoPi * i / 4096.0); }
    float operator() (double cycles) const
    {
        const double p = cycles - std::floor (cycles);
        const double x = p * 4096.0;
        const int i = (int) x;
        const float f = (float) (x - i);
        return t[(size_t) i] + (t[(size_t) i + 1] - t[(size_t) i]) * f;
    }
};
const SineTable& sine() { static SineTable s; return s; }

// D major-ish pitch set shared by every built-in loop, so any loops combine
const int kScale[] = { 0, 2, 4, 7, 9, 11 };
double noteHz (int degree, int octave)
{
    int o = octave + (degree >= 0 ? degree / 6 : -((5 - degree) / 6));
    int d = ((degree % 6) + 6) % 6;
    return 146.832 * std::pow (2.0, (kScale[d] + 12 * o) / 12.0);
}

const int kChords[8][3] = { { 0, 2, 4 }, { 1, 3, 5 }, { 2, 4, 6 }, { 3, 5, 7 },
                            { 0, 3, 5 }, { 4, 6, 8 }, { 1, 4, 6 }, { 5, 7, 9 } };

const juce::uint32 kPalette[8] = { 0xff2848a8, 0xffe07828, 0xff2e9a48, 0xffe878a8,
                                   0xff1e1e1e, 0xfff0c818, 0xffd83030, 0xff48a8c8 };

using Buf = juce::AudioBuffer<float>;

Buf make (double secs) { Buf b (2, juce::jmax (64, (int) (secs * bank::kRate))); b.clear(); return b; }

Buf organ (const int* chord, double secs, Rng& r)
{
    Buf b = make (secs);
    const float harm[] = { 1, 2, 3, 4, 6, 8 };
    const float amp[]  = { 1.0f, 0.62f, 0.38f, 0.26f, 0.13f, 0.08f };
    const auto& sn = sine();
    const double sr = bank::kRate;
    const float trem = r.range (4.6f, 6.4f);
    auto* L = b.getWritePointer (0); auto* R = b.getWritePointer (1);
    for (int n = 0; n < 3; ++n)
    {
        const double f = noteHz (chord[n], 0) * (1.0 + r.range (-0.0015f, 0.0015f));
        for (int h = 0; h < 6; ++h)
        {
            const double inc = f * harm[h] / sr;
            if (f * harm[h] > 16000.0) continue;
            double ph = r.uni();
            const float a = amp[h] * (n == 0 ? 1.0f : 0.8f);
            for (int i = 0; i < b.getNumSamples(); ++i, ph += inc)
            {
                const float v = a * sn (ph);
                const double t = i / sr;
                L[i] += v * (1.0f - 0.16f * (0.5f + 0.5f * sn (trem * t)));
                R[i] += v * (1.0f - 0.16f * (0.5f + 0.5f * sn (trem * t + 0.31)));
            }
        }
    }
    return b;
}

Buf guitar (const int* chord, double secs, Rng& r)
{
    Buf b = make (secs);
    const double sr = bank::kRate;
    const int total = b.getNumSamples();
    std::vector<float> line;
    double t = r.range (0.0f, 0.1f);
    while (t < secs)
    {
        const int deg = chord[r.below (3)] + (r.chance (0.35f) ? 6 : 0);
        const double f = noteHz (deg, -1);
        const int N = juce::jmax (2, (int) std::lround (sr / f));
        line.assign ((size_t) N, 0.0f);
        float lp = 0;
        for (auto& x : line) { lp += 0.5f * ((r.uni() * 2 - 1) - lp); x = lp; }
        const float pan = r.range (0.2f, 0.8f), gain = r.range (0.5f, 0.9f);
        const int start = (int) (t * sr);
        const int len = juce::jmin (total - start, (int) (sr * 3.0));
        int idx = 0;
        for (int i = 0; i < len; ++i)
        {
            const int nx = (idx + 1) % N;
            const float y = line[(size_t) idx];
            line[(size_t) idx] = 0.5f * (line[(size_t) idx] + line[(size_t) nx]) * 0.9965f;
            idx = nx;
            b.addSample (0, start + i, y * gain * (1.0f - pan) * 1.6f);
            b.addSample (1, start + i, y * gain * pan * 1.6f);
        }
        t += r.range (0.17f, 0.85f);
    }
    return b;
}

Buf feedback (const int* chord, double secs, Rng& r)
{
    Buf b = make (secs);
    const double sr = bank::kRate;
    const auto& sn = sine();
    for (int n = 0; n < 2; ++n)
    {
        const double f = noteHz (chord[n * 2], 1);
        double phL = r.uni(), phR = r.uni();
        const float wob = r.range (0.04f, 0.11f), fl = r.range (5.0f, 8.0f), off = r.uni();
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const double t = i / sr;
            const float env = 0.35f + 0.65f * (0.5f - 0.5f * sn (t / secs * 1.0 + 0.25 + n * 0.37));
            const double drift = 1.0 + 0.0035 * sn (wob * t + off);
            phL += f * drift / sr; phR += (f * drift + 0.45) / sr;
            const float flut = 1.0f - 0.12f * (0.5f + 0.5f * sn (fl * t + off));
            const float drive = 1.2f + 2.5f * env;
            auto shape = [&] (double ph) {
                const float s = sn (ph) + 0.3f * sn (ph * 2.0) + 0.12f * sn (ph * 3.0);
                return std::tanh (drive * s) / std::tanh (drive);
            };
            b.addSample (0, i, 0.5f * env * flut * shape (phL));
            b.addSample (1, i, 0.5f * env * flut * shape (phR));
        }
    }
    return b;
}

Buf skips (int col, double secs, Rng& r)
{
    Buf b = make (secs);
    const double sr = bank::kRate;
    const int total = b.getNumSamples();
    const auto& sn = sine();
    double t = 0.01;
    while (t < secs)
    {
        const int start = (int) (t * sr);
        const float pan = r.uni();
        auto put = [&] (int i, float v) {
            if (i >= 0 && i < total) { b.addSample (0, i, v * (1.0f - pan)); b.addSample (1, i, v * pan); }
        };
        const int kind = r.below (4);
        if (kind == 0)        // click
        {
            const float a = r.range (0.4f, 0.9f) * (r.chance (0.5f) ? 1 : -1);
            for (int k = 0; k < 6; ++k) put (start + k, a * std::pow (0.45f, (float) k));
        }
        else if (kind == 1)   // pitched blip
        {
            const double f = noteHz (r.below (12), 3 + r.below (2));
            const int len = (int) (sr * r.range (0.003f, 0.03f));
            for (int k = 0; k < len; ++k)
                put (start + k, 0.5f * sn (f * k / sr) * std::exp (-6.0f * k / (float) len));
        }
        else if (kind == 2)   // noise tick
        {
            const int len = (int) (sr * r.range (0.001f, 0.006f));
            for (int k = 0; k < len; ++k) put (start + k, 0.4f * (r.uni() * 2 - 1) * (1.0f - k / (float) len));
        }
        else                  // stutter: a CD stuck on the same frame
        {
            const int reps = 3 + r.below (6);
            const int gap = (int) (sr * r.range (0.008f, 0.04f));
            const double f = noteHz (r.below (12), 2 + r.below (2));
            for (int q = 0; q < reps; ++q)
                for (int k = 0; k < gap; ++k)
                    put (start + q * gap + k, 0.3f * sn (f * k / sr) * (1.0f - k / (float) gap));
        }
        t += r.range (0.03f, 0.42f) * (1.0f - 0.07f * (float) col);
    }
    return b;
}

Buf glass (const int* chord, double secs, Rng& r, int col)
{
    Buf b = make (secs);
    const double sr = bank::kRate;
    const int total = b.getNumSamples();
    const auto& sn = sine();
    const double ratio = (col % 2 == 0) ? 3.5 : 1.414;
    double t = r.range (0.0f, 0.15f);
    while (t < secs)
    {
        const double f = noteHz (chord[r.below (3)], 1 + r.below (2));
        const int start = (int) (t * sr);
        const int len = juce::jmin (total - start, (int) (sr * 2.6));
        const float pan = r.range (0.25f, 0.75f);
        for (int k = 0; k < len; ++k)
        {
            const double tt = k / sr;
            const float I = 2.6f * std::exp (-3.5f * (float) tt) + 0.25f;
            const float a = 0.35f * std::exp (-1.7f * (float) tt) * juce::jmin (1.0f, (float) tt / 0.003f);
            const float yl = sn (f * tt + I * sn (f * ratio * tt) / kTwoPi);
            const float yr = sn (f * 1.0021 * tt + I * sn (f * ratio * tt) / kTwoPi);
            b.addSample (0, start + k, a * yl * (1.0f - pan) * 1.4f);
            b.addSample (1, start + k, a * yr * pan * 1.4f);
        }
        t += r.range (0.28f, 1.3f);
    }
    return b;
}

Buf low (const int* chord, double secs, Rng& r)
{
    Buf b = make (secs);
    const double sr = bank::kRate;
    const int total = b.getNumSamples();
    const auto& sn = sine();
    double t = 0;
    while (t < secs)
    {
        const double f = noteHz (chord[r.below (2) * 2], -1);
        const double dur = r.range (1.0f, 3.0f);
        const int start = (int) (t * sr), len = juce::jmin (total - start, (int) (dur * sr));
        double ph = 0;
        for (int k = 0; k < len; ++k)
        {
            const float tt = (float) (k / sr);
            const float env = juce::jmin (1.0f, tt / 0.35f) * juce::jmin (1.0f, ((float) dur - tt) / 0.6f);
            ph += f / sr;
            const float y = std::tanh (1.6f * (sn (ph) + 0.22f * sn (ph * 2.0) + 0.08f * sn (ph * 3.0))) * 0.5f * env;
            b.addSample (0, start + k, y);
            b.addSample (1, start + k, y);
        }
        t += dur * r.range (0.7f, 1.05f);
    }
    return b;
}

Buf noise (int col, double secs, Rng& r)
{
    Buf b = make (secs);
    const double sr = bank::kRate;
    const auto& sn = sine();
    for (int ch = 0; ch < 2; ++ch)
    {
        float ic1 = 0, ic2 = 0, hiss = 0;
        const float rate = r.range (0.06f, 0.25f), off = r.uni();
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const double t = i / sr;
            const float fc = 300.0f * std::pow (2.0f, 3.5f * (0.5f + 0.5f * sn (rate * t + off + ch * 0.1)) + col * 0.2f);
            const float g = std::tan (juce::MathConstants<float>::pi * juce::jmin (fc, 15000.0f) / (float) sr);
            const float k = 1.0f / 5.0f;
            const float x = r.uni() * 2 - 1;
            // TPT state-variable band-pass (Cytomic form)
            const float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
            const float v3 = x - ic2;
            const float bp = a1 * ic1 + a2 * v3;
            const float v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2 * bp - ic1; ic2 = 2 * v2 - ic2;
            hiss += 0.05f * (x - hiss);
            float y = 0.55f * bp + 0.25f * hiss;
            if (r.chance (0.0007f)) y += r.range (-0.7f, 0.7f);
            b.setSample (ch, i, y);
        }
    }
    return b;
}

void normalise (Buf& b, bool sparse)
{
    double sum = 0; float peak = 0;
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const float v = b.getSample (ch, i);
            sum += v * v; peak = juce::jmax (peak, std::abs (v));
        }
    if (peak < 1.0e-6f) return;
    const float rms = (float) std::sqrt (sum / juce::jmax (1, b.getNumChannels() * b.getNumSamples()));
    const float g = juce::jmin (sparse ? 0.5f / peak : 0.13f / juce::jmax (rms, 1.0e-6f), 0.85f / peak);
    b.applyGain (g);
}

void copyFrag (const Buf& src, int pos, int frag, Buf& out, int at, int ramp)
{
    const int S = src.getNumSamples(), N = out.getNumSamples();
    for (int k = 0; k < frag && at + k < N; ++k)
    {
        const float g = juce::jmin (1.0f, (float) k / ramp, (float) (frag - 1 - k) / ramp);
        const int si = (pos + k) % S;
        for (int ch = 0; ch < 2; ++ch)
            out.addSample (ch, at + k, src.getSample (juce::jmin (ch, src.getNumChannels() - 1), si) * g);
    }
}

bool readFile (const juce::String& path, double offset, double length, juce::AudioFormatManager& fm,
               Buf& out, double& rate)
{
    std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (juce::File (path)));
    if (rd == nullptr || rd->lengthInSamples < 64 || rd->sampleRate <= 0) return false;
    rate = rd->sampleRate;
    juce::int64 start = (juce::int64) (offset * rate);
    juce::int64 n = (juce::int64) (length * rate);
    if (start + 64 > rd->lengthInSamples) start = 0;
    n = juce::jlimit ((juce::int64) 64, rd->lengthInSamples - start, n);
    out.setSize (2, (int) n);
    out.clear();
    rd->read (&out, 0, (int) n, start, true, true);
    if (rd->numChannels == 1) out.copyFrom (1, 0, out, 0, 0, (int) n);
    return true;
}

double fileSeconds (const juce::File& f, juce::AudioFormatManager& fm)
{
    std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (f));
    return (rd != nullptr && rd->sampleRate > 0) ? rd->lengthInSamples / rd->sampleRate : 0.0;
}
} // namespace

//==============================================================================
juce::ValueTree CellSource::toTree() const
{
    juce::ValueTree t ("cell");
    t.setProperty ("kind", (int) kind, nullptr);
    t.setProperty ("path", path, nullptr);
    t.setProperty ("offset", offset, nullptr);
    t.setProperty ("length", length, nullptr);
    t.setProperty ("intensity", intensity, nullptr);
    t.setProperty ("seed", (juce::int64) seed, nullptr);
    return t;
}

CellSource CellSource::fromTree (const juce::ValueTree& t)
{
    CellSource c;
    c.kind = (Kind) juce::jlimit (0, 2, (int) t.getProperty ("kind", 0));
    c.path = t.getProperty ("path").toString();
    c.offset = t.getProperty ("offset", 0.0);
    c.length = t.getProperty ("length", 0.0);
    c.intensity = t.getProperty ("intensity", 0.0f);
    c.seed = (juce::uint32) (juce::int64) t.getProperty ("seed", 0);
    return c;
}

namespace bank
{
juce::Colour outerColour (int cell) { return juce::Colour (kPalette[(cell / kCols) % 8]); }
juce::Colour innerColour (int cell)
{
    const int row = cell / kCols, col = cell % kCols;
    int k = (row + col * 3 + 1) % 8;
    if (k == row) k = (k + 1) % 8;
    return juce::Colour (kPalette[k]);
}

juce::String familyName (int row)
{
    static const char* names[] = { "organ", "guitar", "feedback", "skips", "glass", "low", "noise", "skipped" };
    return names[juce::jlimit (0, 7, row)];
}

double loopSeconds (int cell)
{
    static const double base[] = { 0.93, 1.37, 1.81, 2.47, 3.19, 4.13, 5.29, 6.71 };
    static const double rowF[] = { 1.0, 0.85, 1.2, 0.55, 1.0, 1.3, 0.9, 0.62 };
    return base[cell % kCols] * rowF[(cell / kCols) % 8];
}

std::shared_ptr<Sound> makeBuiltin (int cell, juce::uint32 seed)
{
    const int row = cell / kCols, col = cell % kCols;
    Rng r ((uint32) (cell * 7919 + 17) ^ (seed * 0x85EBCA6Bu));
    const double loop = loopSeconds (cell);
    const double srcSecs = juce::jmin (14.0, loop * 1.8 + 1.5);
    const int* chord = kChords[col];

    Buf src;
    switch (row)
    {
        case 0: src = organ (chord, srcSecs, r); break;
        case 1: src = guitar (chord, srcSecs, r); break;
        case 2: src = feedback (chord, srcSecs, r); break;
        case 3: src = skips (col, srcSecs, r); break;
        case 4: src = glass (chord, srcSecs, r, col); break;
        case 5: src = low (chord, srcSecs, r); break;
        case 6: src = noise (col, srcSecs, r); break;
        default:
        {
            src = organ (chord, srcSecs, r);
            src.applyGain (0.5f);
            auto g = glass (chord, srcSecs, r, col);
            for (int ch = 0; ch < 2; ++ch) src.addFrom (ch, 0, g, ch, 0, juce::jmin (src.getNumSamples(), g.getNumSamples()), 0.8f);
            break;
        }
    }
    static const float baseI[] = { 0.12f, 0.22f, 0.08f, 0.45f, 0.2f, 0.05f, 0.3f, 0.72f };
    float intensity = juce::jmin (0.95f, baseI[row] + col * 0.055f + (seed != 0 ? 0.2f : 0.0f));

    auto s = std::make_shared<Sound>();
    s->sampleRate = kRate;
    cdSkip (src, s->audio, kRate, loop, intensity, (uint32) cell * 31u + seed * 977u + 5u);
    normalise (s->audio, row == 3);
    s->name = familyName (row) + " " + juce::String (col + 1) + (seed != 0 ? " *" : "");
    return s;
}

std::shared_ptr<Sound> make (int cell, const CellSource& c, juce::AudioFormatManager& fm)
{
    if (c.kind == CellSource::Builtin) return makeBuiltin (cell, c.seed);

    auto s = std::make_shared<Sound>();
    const juce::File f (c.path);
    if (c.kind == CellSource::File)
    {
        if (! readFile (c.path, c.offset, juce::jlimit (0.05, 30.0, c.length > 0 ? c.length : 30.0), fm, s->audio, s->sampleRate))
            return nullptr;
    }
    else
    {
        Buf src; double rate = kRate;
        const double len = juce::jmax (0.05, c.length);
        if (! readFile (c.path, c.offset, juce::jmin (16.0, len * 1.8 + 1.5), fm, src, rate)) return nullptr;
        s->sampleRate = rate;
        cdSkip (src, s->audio, rate, len, c.intensity, c.seed);
    }
    normalise (s->audio, false);
    s->name = f.getFileNameWithoutExtension();
    return s;
}

void cdSkip (const Buf& src, Buf& out, double sr, double targetSeconds, float intensity, juce::uint32 seed)
{
    Rng r (seed ^ 0xC0FFEEu);
    const int N = juce::jmax (64, (int) (targetSeconds * sr));
    out.setSize (2, N);
    out.clear();
    const int S = src.getNumSamples();
    if (S < 64) return;
    const int ramp = juce::jmax (8, (int) (sr * 0.0005));

    if (intensity <= 0.001f)
    {
        copyFrag (src, r.below (juce::jmax (1, S - N)), N, out, 0, ramp);
        return;
    }

    int pos = r.below (juce::jmax (1, S - N / 2));
    int i = 0;
    while (i < N)
    {
        const float maxMs = juce::jmap (intensity, 1500.0f, 70.0f);
        const float minMs = juce::jmap (intensity, 220.0f, 9.0f);
        const float ms = minMs * std::pow (maxMs / minMs, r.uni());
        const int frag = juce::jmax (ramp * 2 + 2, (int) (ms * sr / 1000.0));
        const int reps = r.chance (intensity * 0.6f) ? 2 + r.below (1 + (int) (intensity * 7)) : 1;
        for (int q = 0; q < reps && i < N; ++q)
        {
            copyFrag (src, pos, frag, out, i, ramp);
            i += frag;
        }
        if (i < N && r.chance (intensity * 0.12f))       // a read error
        {
            const int e = (int) (r.range (2.0f, 25.0f) * sr / 1000.0);
            switch (r.below (3))
            {
                case 0: i += e; break;                     // drop-out
                case 1:                                    // frozen, bit-crushed frame
                {
                    float held[2] = { 0, 0 };
                    for (int k = 0; k < e && i + k < N; ++k)
                        for (int ch = 0; ch < 2; ++ch)
                        {
                            if (k % 9 == 0) held[ch] = std::round (src.getSample (juce::jmin (ch, src.getNumChannels() - 1), (pos + k) % S) * 8.0f) / 8.0f;
                            out.addSample (ch, i + k, held[ch] * 0.8f);
                        }
                    i += e;
                    break;
                }
                default:
                    for (int ch = 0; ch < 2; ++ch) out.addSample (ch, i, r.chance (0.5f) ? 0.5f : -0.5f);
                    i += e / 4 + 1;
                    break;
            }
        }
        if (r.chance (intensity * 0.35f)) pos = r.below (juce::jmax (1, S - frag));
        else if (r.chance (intensity * 0.2f)) pos = juce::jmax (0, pos - frag * 2);
        else
        {
            pos += frag;
            if (pos + frag >= S) pos = r.below (juce::jmax (1, S / 2));
        }
    }
}

juce::Array<juce::File> collectAudioFiles (const juce::Array<juce::File>& in, juce::AudioFormatManager& fm, int maxFiles)
{
    juce::Array<juce::File> out;
    auto ok = [&] (const juce::File& f) {
        return f.existsAsFile() && fm.findFormatForFileExtension (f.getFileExtension()) != nullptr
               && ! f.getFileName().startsWithChar ('.');
    };
    for (auto& f : in)
    {
        if (f.isDirectory())
        {
            auto kids = f.findChildFiles (juce::File::findFiles, true);
            kids.sort();
            for (auto& k : kids) if (ok (k)) out.add (k);
        }
        else if (ok (f)) out.add (f);
        if (out.size() >= maxFiles) break;
    }
    out.resize (juce::jmin (out.size(), maxFiles));
    return out;
}

std::vector<CellSource> planFromFiles (const juce::Array<juce::File>& files, juce::AudioFormatManager& fm, juce::uint32 seed)
{
    std::vector<CellSource> plan;
    std::vector<double> lens;
    juce::Array<juce::File> usable;
    for (auto& f : files)
    {
        const double s = fileSeconds (f, fm);
        if (s > 0.02) { usable.add (f); lens.push_back (s); }
    }
    if (usable.isEmpty()) return plan;

    Rng r (seed + 99);
    std::vector<int> order;
    while ((int) order.size() < kCells)
    {
        std::vector<int> round;
        for (int i = 0; i < usable.size(); ++i) round.push_back (i);
        for (int i = (int) round.size() - 1; i > 0; --i) std::swap (round[(size_t) i], round[(size_t) r.below (i + 1)]);
        order.insert (order.end(), round.begin(), round.end());
    }
    for (int cell = 0; cell < kCells; ++cell)
    {
        const int fi = order[(size_t) cell];
        const double fl = lens[(size_t) fi];
        CellSource c;
        c.path = usable[fi].getFullPathName();
        c.length = loopSeconds (cell);
        if (fl <= c.length * 1.2)
        {
            c.kind = CellSource::File;
            c.length = fl;
        }
        else
        {
            c.kind = CellSource::Skipped;
            c.offset = r.range (0.0f, (float) juce::jmax (0.0, fl - c.length * 1.8 - 1.5));
            c.intensity = 0.05f + (float) (cell % kCols) / 7.0f * 0.85f;
            c.seed = r.next();
        }
        plan.push_back (c);
    }
    return plan;
}
} // namespace bank
