#include "Engine.h"
#include <cmath>

void Engine::prepare (double sampleRate)
{
    sr = sampleRate > 0 ? sampleRate : 44100.0;
    kGain = 1.0f - std::exp (-1.0f / (float) (sr * 0.012));   // ~12 ms for moves and volume
    kEnv  = 1.0f - std::exp (-1.0f / (float) (sr * 0.0015));  // ~1.5 ms de-click on effects
    reset();
}

void Engine::reset()
{
    for (auto& s : slots) s = Slot();
    for (auto& p : pub) { p.id.store (-1); p.frac.store (0); }
}

static float effectEnv (int fxType, float prog)
{
    switch (fxType)
    {
        case fx::FadeInOut: return std::sin (juce::MathConstants<float>::pi * prog);
        case fx::FadeIn:    return prog;
        case fx::FadeOut:   return 1.0f - prog;
        default:            return 1.0f;
    }
}

void Engine::process (float* L, float* R, int n, const VoiceSpec* specs, int numSpecs)
{
    for (auto& s : slots) s.used = false;

    for (int k = 0; k < numSpecs; ++k)
    {
        const auto& v = specs[k];
        if (v.snd == nullptr || v.snd->audio.getNumSamples() < 64) continue;
        Slot* slot = nullptr;
        for (auto& s : slots) if (s.id == v.id) { slot = &s; break; }
        if (slot == nullptr)
            for (auto& s : slots) if (s.id < 0) { slot = &s; break; }
        if (slot == nullptr) continue;
        const bool fresh = slot->id != v.id;
        if (fresh)
        {
            *slot = Slot();
            slot->id = v.id;
            slot->phase = v.s * v.snd->audio.getNumSamples();
        }
        slot->snd = v.snd;
        slot->s = v.s;
        slot->e = v.e;
        slot->effect = v.effect;
        const float pan = juce::jlimit (0.0f, 1.0f, v.pan);
        slot->tgtL = v.gain * juce::jmin (1.0f, 2.0f * (1.0f - pan));
        slot->tgtR = v.gain * juce::jmin (1.0f, 2.0f * pan);
        if (fresh) { slot->gL = 0; slot->gR = 0; }
        slot->used = true;
    }

    for (int si = 0; si < kSlots; ++si)
    {
        auto& sl = slots[(size_t) si];
        if (sl.id < 0) { pub[(size_t) si].id.store (-1, std::memory_order_relaxed); continue; }
        if (! sl.used) { sl.tgtL = 0; sl.tgtR = 0; }
        if (sl.snd == nullptr) { sl.id = -1; continue; }

        const auto& a = sl.snd->audio;
        const int len = a.getNumSamples();
        const float* inL = a.getReadPointer (0);
        const float* inR = a.getReadPointer (a.getNumChannels() > 1 ? 1 : 0);
        double S = juce::jlimit (0.0, (double) len - 2, (double) sl.s * len);
        double E = juce::jlimit (S + 2, (double) len - 1, (double) sl.e * len);
        const double minLen = juce::jmax (32.0, sl.snd->sampleRate * 0.004);
        if (E - S < minLen) E = juce::jmin ((double) len - 1, S + minLen);
        const double xf = juce::jmin (256.0, (E - S) * 0.25);
        const double body = juce::jmax (1.0, E - S - xf);
        const double step = sl.snd->sampleRate / sr;
        if (sl.phase < S || sl.phase >= E) sl.phase = S;

        auto read = [&] (const float* p, double pos) {
            const int i = (int) pos;
            const float f = (float) (pos - i);
            const int j = juce::jmin (i + 1, len - 1);
            return p[i] + (p[j] - p[i]) * f;
        };

        for (int i = 0; i < n; ++i)
        {
            double p = sl.phase;
            float l = read (inL, p), r = read (inR, p);
            if (p > E - xf)
            {
                const float t = (float) ((p - (E - xf)) / xf);
                const double q = S + (p - (E - xf));
                l = l * (1.0f - t) + read (inL, q) * t;
                r = r * (1.0f - t) + read (inR, q) * t;
            }
            const float prog = (float) juce::jlimit (0.0, 1.0, (p - S) / body);
            sl.env += (effectEnv (sl.effect, prog) - sl.env) * kEnv;
            sl.gL += (sl.tgtL - sl.gL) * kGain;
            sl.gR += (sl.tgtR - sl.gR) * kGain;
            L[i] += l * sl.gL * sl.env;
            R[i] += r * sl.gR * sl.env;
            p += step;
            if (p >= E) p -= body;
            sl.phase = p;
        }

        pub[(size_t) si].frac.store ((float) juce::jlimit (0.0, 1.0, (sl.phase - S) / body), std::memory_order_relaxed);
        pub[(size_t) si].id.store (sl.id, std::memory_order_relaxed);

        if (! sl.used && std::abs (sl.gL) + std::abs (sl.gR) < 1.0e-4f)
        {
            sl.id = -1;
            pub[(size_t) si].id.store (-1, std::memory_order_relaxed);
        }
    }
}

int Engine::buildMix (const Model& m, const CellArray& cells, VoiceSpec* out, int max)
{
    bool anySolo = false;
    for (auto& i : m.mix) anySolo |= i.effect == fx::Solo;
    int n = 0;
    for (auto& i : m.mix)
    {
        if (n >= max) break;
        auto* snd = cells[(size_t) i.cell].get();
        if (snd == nullptr) continue;
        VoiceSpec v;
        v.id = i.id; v.snd = snd; v.s = i.s; v.e = i.e; v.pan = i.x; v.effect = i.effect;
        v.gain = volumeGain (i.y);
        if (i.effect == fx::Mute || (anySolo && i.effect != fx::Solo)) v.gain = 0;
        out[n++] = v;
    }
    return n;
}

int Engine::buildTimeline (const Model& m, const CellArray& cells, double t, VoiceSpec* out, int max)
{
    const double L = m.minutes * 60.0;
    int n = 0;
    for (auto& seg : m.segs)
    {
        const double t0 = seg.start * L, t1 = juce::jmin (L, (double) (seg.start + seg.len) * L);
        if (t < t0 || t >= t1) continue;
        const float edge = (float) juce::jmin (1.0, (t - t0) / 0.03, (t1 - t) / 0.03);
        bool anySolo = false;
        for (auto& i : seg.snap.items) anySolo |= i.effect == fx::Solo;
        for (size_t k = 0; k < seg.snap.items.size() && n < max; ++k)
        {
            const auto& i = seg.snap.items[k];
            auto* snd = cells[(size_t) i.cell].get();
            if (snd == nullptr) continue;
            VoiceSpec v;
            v.id = timelineVoiceId (seg.id, (int) k);
            v.snd = snd; v.s = i.s; v.e = i.e; v.pan = i.x; v.effect = i.effect;
            v.gain = volumeGain (i.y) * edge;
            if (i.effect == fx::Mute || (anySolo && i.effect != fx::Solo)) v.gain = 0;
            out[n++] = v;
        }
    }
    return n;
}
