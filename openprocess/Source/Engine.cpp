#include "Engine.h"
#include <cmath>

void Engine::prepare (double sampleRate)
{
    sr = sampleRate > 0 ? sampleRate : 44100.0;
    kGain = 1.0f - std::exp (-1.0f / (float) (sr * 0.012));        // ~12 ms for moves and volume
    kEnv  = 1.0f - std::exp (-1.0f / (float) (sr * 0.0015));       // ~1.5 ms de-click on effects
    kBlock = 1.0f - std::exp (-(float) kSub / (float) (sr * 0.02)); // ~20 ms for the Ambisonic gains, per inner block
    for (auto& f : freezers) if (f == nullptr) f = std::make_unique<Freezer>();
    reset();
}

void Engine::reset()
{
    for (auto& s : slots) s = Slot();
    freezerUsed.fill (false);
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

void Engine::process (const Output& out, int n, const VoiceSpec* specs, int numSpecs, const float* inL, const float* inR)
{
    for (auto& s : slots) s.used = false;
    accAz.fill (0.0f);
    accEl.fill (0.0f);

    for (int k = 0; k < numSpecs; ++k)
    {
        const auto& v = specs[k];
        if (! v.live && (v.snd == nullptr || v.snd->audio.getNumSamples() < 64)) continue;
        Slot* slot = nullptr;
        for (auto& s : slots) if (s.id == v.id) { slot = &s; break; }
        if (slot == nullptr)
            for (auto& s : slots) if (s.id < 0) { slot = &s; break; }
        if (slot == nullptr) continue;
        const bool fresh = slot->id != v.id;
        if (fresh)
        {
            if (slot->fz >= 0) freezerUsed[(size_t) slot->fz] = false;
            *slot = Slot();
            slot->id = v.id;
            slot->phase = v.snd != nullptr ? v.s * v.snd->audio.getNumSamples() : 0.0;
        }
        slot->snd = v.snd;
        slot->live = v.live;
        slot->s = v.s;
        slot->e = v.e;
        slot->effect = v.effect;
        slot->fzSlide = v.fzSlide; slot->fzDenoise = v.fzDenoise; slot->fzEvery = v.fzEvery;
        if (v.effect == fx::Freeze && slot->fz < 0)
            for (int f = 0; f < kFreezers; ++f)
                if (! freezerUsed[(size_t) f] && freezers[(size_t) f] != nullptr)
                {
                    freezerUsed[(size_t) f] = true; slot->fz = f; slot->fzGen = v.fzGen;
                    freezers[(size_t) f]->reset();
                    break;
                }
        if (slot->fz >= 0 && v.effect != fx::Freeze) { freezerUsed[(size_t) slot->fz] = false; slot->fz = -1; }
        if (slot->fz >= 0 && v.fzGen != slot->fzGen) { slot->fzGen = v.fzGen; freezers[(size_t) slot->fz]->freezeAgain(); }
        const float pan = juce::jlimit (0.0f, 1.0f, v.pan);
        slot->tgtL = v.gain * juce::jmin (1.0f, 2.0f * (1.0f - pan));
        slot->tgtR = v.gain * juce::jmin (1.0f, 2.0f * pan);
        slot->az = v.az; slot->el = v.el; slot->dist = v.dist; slot->gain = v.gain;
        slot->air = v.air; slot->spread = v.spread; slot->blur = v.blur; slot->nearR = v.nearR;
        if (fresh) { slot->gL = 0; slot->gR = 0; }
        slot->used = true;
    }

    for (int si = 0; si < kSlots; ++si)
    {
        auto& sl = slots[(size_t) si];
        if (sl.id < 0) { pub[(size_t) si].id.store (-1, std::memory_order_relaxed); continue; }
        if (! sl.used) { sl.tgtL = 0; sl.tgtR = 0; sl.gain = 0; }
        if (sl.snd == nullptr && ! sl.live) { sl.id = -1; continue; }

        for (int off = 0; off < n; off += kSub)
            render (sl, si, out, off, juce::jmin (kSub, n - off), inL, inR);

        pub[(size_t) si].id.store (sl.id, std::memory_order_relaxed);

        const float level = out.order < 0 ? std::abs (sl.gL) + std::abs (sl.gR) : std::abs (sl.cL[0]) + std::abs (sl.cR[0]);
        if (! sl.used && level < 1.0e-4f)
        {
            if (sl.fz >= 0) { freezerUsed[(size_t) sl.fz] = false; sl.fz = -1; }
            sl.id = -1;
            pub[(size_t) si].id.store (-1, std::memory_order_relaxed);
        }
    }
    if (out.order >= 0)
    {
        for (int b = 0; b < kAzBins; ++b) meterAz[(size_t) b].store (accAz[(size_t) b], std::memory_order_relaxed);
        for (int b = 0; b < kElBins; ++b) meterEl[(size_t) b].store (accEl[(size_t) b], std::memory_order_relaxed);
    }
}

void Engine::render (Slot& sl, int si, const Output& out, int off, int n, const float* inL, const float* inR)
{
    // 1. the voice itself, into tmpL / tmpR, with its effect envelope
    if (sl.live)
    {
        for (int i = 0; i < n; ++i)
        {
            const float env = sl.effect == fx::Mute ? 0.0f : 1.0f;
            sl.env += (env - sl.env) * kEnv;
            tmpL[(size_t) i] = (inL != nullptr ? inL[off + i] : 0.0f) * sl.env;
            tmpR[(size_t) i] = (inR != nullptr ? inR[off + i] : (inL != nullptr ? inL[off + i] : 0.0f)) * sl.env;
        }
        pub[(size_t) si].frac.store (-1.0f, std::memory_order_relaxed);
    }
    else
    {
        const auto& a = sl.snd->audio;
        const int len = a.getNumSamples();
        const float* srcL = a.getReadPointer (0);
        const float* srcR = a.getReadPointer (a.getNumChannels() > 1 ? 1 : 0);
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
            float l = read (srcL, p), r = read (srcR, p);
            if (p > E - xf)
            {
                const float t = (float) ((p - (E - xf)) / xf);
                const double q = S + (p - (E - xf));
                l = l * (1.0f - t) + read (srcL, q) * t;
                r = r * (1.0f - t) + read (srcR, q) * t;
            }
            const float prog = (float) juce::jlimit (0.0, 1.0, (p - S) / body);
            sl.env += (effectEnv (sl.effect, prog) - sl.env) * kEnv;
            tmpL[(size_t) i] = l * sl.env;
            tmpR[(size_t) i] = r * sl.env;
            p += step;
            if (p >= E) p -= body;
            sl.phase = p;
        }
        pub[(size_t) si].frac.store ((float) juce::jlimit (0.0, 1.0, (sl.phase - S) / body), std::memory_order_relaxed);
    }

    // the freeze effect replaces the voice by its frozen spectrum
    if (sl.fz >= 0)
        freezers[(size_t) sl.fz]->process (tmpL.data(), tmpR.data(), n, sr, sl.fzSlide, sl.fzDenoise,
                                           sl.fzEvery <= 0.001f ? 0.0f : 0.5f * std::pow (40.0f, sl.fzEvery));

    // 2a. openprocess: left / right
    if (out.order < 0)
    {
        float* L = out.ch[0] + off;
        float* R = (out.numCh > 1 ? out.ch[1] : out.ch[0]) + off;
        for (int i = 0; i < n; ++i)
        {
            sl.gL += (sl.tgtL - sl.gL) * kGain;
            sl.gR += (sl.tgtR - sl.gR) * kGain;
            L[i] += tmpL[(size_t) i] * sl.gL;
            R[i] += tmpR[(size_t) i] * sl.gR;
        }
        return;
    }

    // 2b. opensphere: air (far sounds lose their top), then encode both channels, spread by distance
    SpaceParams sp;
    sp.air = sl.air; sp.spread = sl.spread; sp.blur = sl.blur; sp.nearR = sl.nearR;
    const float d = juce::jlimit (0.0f, 1.0f, sl.dist);
    const float cutoff = 18000.0f * std::pow (3500.0f / 18000.0f, d * d * sp.air * 2.0f);
    const float kAir = 1.0f - std::exp (-2.0f * juce::MathConstants<float>::pi * juce::jmin (cutoff, (float) sr * 0.45f) / (float) sr);
    for (int i = 0; i < n; ++i)
    {
        sl.lpL += (tmpL[(size_t) i] - sl.lpL) * kAir;
        sl.lpR += (tmpR[(size_t) i] - sl.lpR) * kAir;
        tmpL[(size_t) i] = sl.lpL;
        tmpR[(size_t) i] = sl.lpR;
    }

    const int order = juce::jlimit (0, ambi::kMaxOrder, out.order);
    const int nch = juce::jmin (out.numCh, ambi::chFor (order));
    const float half = space::width (d, sp) * 0.5f;
    const auto yL = ambi::sh (sl.az + half, sl.el, order);
    const auto yR = ambi::sh (sl.az - half, sl.el, order);
    // very near: the higher orders fade, so the sound is all around (inside your head at 0);
    // blur: far sounds lose their sharpness the same way
    const float nearK = juce::jlimit (0.0f, 1.0f, d / juce::jmax (0.001f, sp.nearR)) * (1.0f - 0.85f * sp.blur * d);
    const float g = sl.gain * 0.7071f;
    const float a = kBlock * (float) n / (float) kSub;
    float wl = 1.0f;
    int lNow = 0;
    for (int c = 0; c < nch; ++c)
    {
        const int l = ambi::orderOf (c);
        if (l != lNow) { wl *= nearK; lNow = l; }
        const float tL = g * wl * yL[(size_t) c], tR = g * wl * yR[(size_t) c];
        const float c0L = sl.cL[(size_t) c], c0R = sl.cR[(size_t) c];
        const float c1L = c0L + (tL - c0L) * a, c1R = c0R + (tR - c0R) * a;
        const float dL = (c1L - c0L) / (float) n, dR = (c1R - c0R) / (float) n;
        float* o = out.ch[c] + off;
        float gl = c0L, gr = c0R;
        for (int i = 0; i < n; ++i)
        {
            gl += dL; gr += dR;
            o[i] += tmpL[(size_t) i] * gl + tmpR[(size_t) i] * gr;
        }
        sl.cL[(size_t) c] = c1L;
        sl.cR[(size_t) c] = c1R;
    }

    // the meter: this voice's energy, spread over the direction bins round its place
    if (sl.gain > 0.0f)
    {
        float e = 0.0f;
        for (int i = 0; i < n; ++i) e += tmpL[(size_t) i] * tmpL[(size_t) i] + tmpR[(size_t) i] * tmpR[(size_t) i];
        e = e / (2.0f * (float) n) * sl.gain * sl.gain;
        if (e > 1.0e-12f)
        {
            const float xs = space::xForAzimuth (sl.az);
            const float ys = (sl.el * 180.0f / juce::MathConstants<float>::pi - space::kElLow) / (space::kElHigh - space::kElLow);
            const float sx = 0.012f + 0.5f * half / (2.0f * juce::MathConstants<float>::pi) + 0.06f * (1.0f - nearK);
            const float sy = 0.035f + 0.15f * (1.0f - nearK);
            float wa[kAzBins], we[kElBins], sa = 0, se = 0;
            for (int b = 0; b < kAzBins; ++b)
            {
                float dx = (b + 0.5f) / kAzBins - xs;
                dx -= std::round (dx);
                wa[b] = std::exp (-(dx * dx) / (sx * sx)); sa += wa[b];
            }
            for (int b = 0; b < kElBins; ++b)
            {
                const float dy = (b + 0.5f) / kElBins - ys;
                we[b] = std::exp (-(dy * dy) / (sy * sy)); se += we[b];
            }
            for (int b = 0; b < kAzBins; ++b) accAz[(size_t) b] += e * wa[b] / sa * (float) n / (float) kSub;
            for (int b = 0; b < kElBins; ++b) accEl[(size_t) b] += e * we[b] / se * (float) n / (float) kSub;
        }
    }
}

VoiceSpec Engine::specFor (const Item& i, const CellArray& cells, double tau, const SpaceParams& sp)
{
    VoiceSpec v;
    v.id = i.id;
    v.live = i.isLive();
    v.snd = v.live ? nullptr : cells[(size_t) juce::jlimit (0, bank::kCells - 1, i.cell)].get();
    v.s = i.s; v.e = i.e; v.effect = i.effect;
    v.fzSlide = i.fzSlide; v.fzDenoise = i.fzDenoise; v.fzEvery = i.fzEvery; v.fzGen = i.fzGen;
   #if OP_SPATIAL
    float x, y, d;
    space::moved (i, tau, x, y, d);
    if (i.motion == motion::Boids && sp.flock != nullptr)
        if (auto* b = sp.flock->find (i.id)) { x = b->x; y = b->y; d = b->d; }
    v.air = sp.air; v.spread = sp.spread; v.blur = sp.blur; v.nearR = sp.nearR;
    v.az = space::azimuth (x);
    v.el = space::elevation (y);
    space::rotate (v.az, v.el, sp);
    v.dist = d;
    v.gain = space::distanceGain (d, sp);
    v.pan = 0.5f;
   #else
    juce::ignoreUnused (tau, sp);
    v.pan = i.x;
    v.gain = volumeGain (i.y);
   #endif
    return v;
}

void Engine::feedFlock (Flock& f, const Model& m, bool timeline, double t, float dt, const SpaceParams& sp)
{
    f.begin();
    auto add = [&] (const Item& i, int id) {
        if (i.motion == motion::Boids) f.add (id, i.x, i.y, i.d, i.mrate * (0.85f + 0.3f * i.mseed));
    };
    float speed = sp.mspeed;
    if (timeline)
    {
        const double L = m.minutes * 60.0;
        for (auto& seg : m.segs)
            if (t >= seg.start * L && t < (seg.start + seg.len) * L)
            {
                for (size_t k = 0; k < seg.snap.items.size(); ++k) add (seg.snap.items[k], timelineVoiceId (seg.id, (int) k));
                if (seg.snap.hasSpace) speed = seg.snap.space.mspeed;
            }
    }
    else
        for (auto& i : m.mix) add (i, i.id);
    f.step (dt * speed);
}

int Engine::buildMix (const Model& m, const CellArray& cells, double t, VoiceSpec* out, int max, const SpaceParams& sp)
{
    bool anySolo = false;
    for (auto& i : m.mix) anySolo |= i.effect == fx::Solo;
    int n = 0;
    for (auto& i : m.mix)
    {
        if (n >= max) break;
        VoiceSpec v = specFor (i, cells, t - i.motionStart, sp);
        if (v.snd == nullptr && ! v.live) continue;
        if (i.effect == fx::Mute || (anySolo && i.effect != fx::Solo)) v.gain = 0;
        out[n++] = v;
    }
    return n;
}

int Engine::buildTimeline (const Model& m, const CellArray& cells, double t, VoiceSpec* out, int max, const SpaceParams& sp)
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
        // a snapshot taken in opensphere brings its own motion speed, distance settings and scene turn
        SpaceParams ssp = seg.snap.hasSpace ? seg.snap.space : sp;
        ssp.flock = sp.flock;
        for (size_t k = 0; k < seg.snap.items.size() && n < max; ++k)
        {
            Item i = seg.snap.items[k];
            i.id = timelineVoiceId (seg.id, (int) k);
            VoiceSpec v = specFor (i, cells, (t - t0) * ssp.mspeed, ssp);   // motions start with the segment
            if (v.snd == nullptr && ! v.live) continue;
            v.gain *= edge;
            if (i.effect == fx::Mute || (anySolo && i.effect != fx::Solo)) v.gain = 0;
            out[n++] = v;
        }
    }
    return n;
}
