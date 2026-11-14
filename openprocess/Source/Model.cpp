#include <algorithm>
#include "Model.h"

Item* Model::findItem (int id)
{
    for (auto& i : mix) if (i.id == id) return &i;
    return nullptr;
}

Segment* Model::findSeg (int id)
{
    for (auto& s : segs) if (s.id == id) return &s;
    return nullptr;
}

void Model::link()
{
    const int n = (int) segs.size();
    std::vector<int> order ((size_t) n);
    for (int i = 0; i < n; ++i) order[(size_t) i] = i;
    std::stable_sort (order.begin(), order.end(), [this] (int a, int b) { return segs[(size_t) a].start < segs[(size_t) b].start; });
    for (auto& s : segs)
    {
        s.from = -1; s.handedOn = false;
        s.end = juce::jmin (1.0f, s.start + s.len);
        s.anySolo = false;
        for (auto& i : s.snap.items) s.anySolo |= i.effect == fx::Solo;
        s.match.assign (s.snap.items.size(), -1);
        s.vid.resize (s.snap.items.size());
        for (size_t k = 0; k < s.vid.size(); ++k) s.vid[k] = voiceId (s.id, (int) k);
    }
    // a gap of up to half a second before a gliding segment still counts as "the scene before"
    const float tol = 0.5f / juce::jmax (1.0f, minutes * 60.0f);
    for (int bi : order)
    {
        auto& B = segs[(size_t) bi];
        if (B.morph <= 0.0f) continue;
        int best = -1;
        float bestEnd = -1.0f;
        for (int ai : order)
        {
            const auto& A = segs[(size_t) ai];
            if (ai == bi || A.handedOn || A.start >= B.start - 1.0e-6f) continue;
            const float e = A.start + A.len;
            if (e < B.start - tol) continue;
            if (e > bestEnd || (e == bestEnd && best >= 0 && A.start > segs[(size_t) best].start)) { best = ai; bestEnd = e; }
        }
        if (best < 0) continue;
        auto& A = segs[(size_t) best];
        A.handedOn = true;
        A.end = B.start;
        B.from = best;
        // the same sound in both scenes: first the very same sound of the play area, then the same cell
        std::vector<bool> used (A.snap.items.size(), false);
        for (int pass = 0; pass < 2; ++pass)
            for (size_t k = 0; k < B.snap.items.size(); ++k)
            {
                if (B.match[k] >= 0) continue;
                const auto& bi2 = B.snap.items[k];
                for (size_t j = 0; j < A.snap.items.size(); ++j)
                {
                    const auto& aj = A.snap.items[j];
                    if (used[j] || aj.cell != bi2.cell || (pass == 0 && aj.id != bi2.id)) continue;
                    used[j] = true;
                    B.match[k] = (int) j;
                    B.vid[k] = A.vid[j];
                    break;
                }
            }
    }
}

namespace
{
juce::ValueTree itemTree (const Item& i)
{
    juce::ValueTree t ("item");
    t.setProperty ("id", i.id, nullptr);
    t.setProperty ("cell", i.cell, nullptr);
    t.setProperty ("x", i.x, nullptr);
    t.setProperty ("y", i.y, nullptr);
    t.setProperty ("s", i.s, nullptr);
    t.setProperty ("e", i.e, nullptr);
    t.setProperty ("fx", i.effect, nullptr);
    t.setProperty ("fzs", i.fzSlide, nullptr);
    t.setProperty ("fzd", i.fzDenoise, nullptr);
    t.setProperty ("fze", i.fzEvery, nullptr);
    t.setProperty ("fzm", i.fzMode, nullptr);
    t.setProperty ("fzc", i.fzScan, nullptr);
    t.setProperty ("d", i.d, nullptr);
    t.setProperty ("motion", i.motion, nullptr);
    t.setProperty ("mrate", i.mrate, nullptr);
    t.setProperty ("mseed", i.mseed, nullptr);
    return t;
}

Item itemFrom (const juce::ValueTree& t)
{
    Item i;
    i.id = t.getProperty ("id", 0);
    i.cell = juce::jlimit (0, kLiveCell, (int) t.getProperty ("cell", 0));
    i.x = juce::jlimit (0.0f, 1.0f, (float) t.getProperty ("x", 0.5f));
    i.y = juce::jlimit (0.0f, 1.0f, (float) t.getProperty ("y", 0.7f));
    i.s = juce::jlimit (0.0f, 1.0f, (float) t.getProperty ("s", 0.0f));
    i.e = juce::jlimit (i.s, 1.0f, (float) t.getProperty ("e", 1.0f));
    i.effect = juce::jlimit (-1, fx::Count - 1, (int) t.getProperty ("fx", -1));
    i.fzSlide = juce::jlimit (0.0f, 1.0f, (float) t.getProperty ("fzs", 0.3f));
    i.fzDenoise = juce::jlimit (0.0f, 1.0f, (float) t.getProperty ("fzd", 0.0f));
    i.fzEvery = juce::jlimit (0.0f, 1.0f, (float) t.getProperty ("fze", 0.0f));
    i.fzMode = juce::jlimit (0, 1, (int) t.getProperty ("fzm", 0));
    i.fzScan = juce::jlimit (0.0f, 1.0f, (float) t.getProperty ("fzc", 0.6f));
    i.d = juce::jlimit (0.0f, 1.0f, (float) t.getProperty ("d", 0.3f));
    i.motion = juce::jlimit (-1, motion::Count - 1, (int) t.getProperty ("motion", -1));
    i.mrate = juce::jlimit (0.25f, 4.0f, (float) t.getProperty ("mrate", 1.0f));
    i.mseed = juce::jlimit (0.0f, 1.0f, (float) t.getProperty ("mseed", (float) ((i.id * 0.6180339887) - std::floor (i.id * 0.6180339887))));
   #if ! OP_SPATIAL
    if (i.isLive()) i.cell = 0;
   #endif
    return i;
}

juce::ValueTree snapTree (const Snapshot& s)
{
    juce::ValueTree t ("snap");
    t.setProperty ("id", s.id, nullptr);
    t.setProperty ("colour", s.colour, nullptr);
    for (auto& i : s.items) t.appendChild (itemTree (i), nullptr);
    if (s.hasSpace)
    {
        const auto& p = s.space;
        t.setProperty ("sp", juce::String (p.rolloff) + " " + juce::String (p.air) + " " + juce::String (p.spread) + " "
                             + juce::String (p.blur) + " " + juce::String (p.nearR) + " " + juce::String (p.yaw) + " "
                             + juce::String (p.pitch) + " " + juce::String (p.mspeed), nullptr);
    }
    return t;
}

Snapshot snapFrom (const juce::ValueTree& t)
{
    Snapshot s;
    s.id = t.getProperty ("id", 0);
    s.colour = t.getProperty ("colour", 0);
    for (auto c : t) if (c.hasType ("item")) s.items.push_back (itemFrom (c));
    if (t.hasProperty ("sp"))
    {
        juce::StringArray v;
        v.addTokens (t["sp"].toString(), " ", "");
        if (v.size() >= 8)
        {
            s.hasSpace = true;
            auto& p = s.space;
            p.rolloff = v[0].getFloatValue(); p.air = v[1].getFloatValue(); p.spread = v[2].getFloatValue();
            p.blur = v[3].getFloatValue(); p.nearR = v[4].getFloatValue(); p.yaw = v[5].getFloatValue();
            p.pitch = v[6].getFloatValue(); p.mspeed = v[7].getFloatValue();
        }
    }
    return s;
}
} // namespace

juce::ValueTree Model::toTree() const
{
    juce::ValueTree t ("model");
    t.setProperty ("minutes", minutes, nullptr);
    t.setProperty ("playStart", playStart, nullptr);
    t.setProperty ("nextId", nextId, nullptr);
    juce::ValueTree m ("mix"), sn ("snaps"), sg ("segs");
    for (auto& i : mix) m.appendChild (itemTree (i), nullptr);
    for (auto& s : snaps) sn.appendChild (snapTree (s), nullptr);
    for (auto& s : segs)
    {
        juce::ValueTree st ("seg");
        st.setProperty ("id", s.id, nullptr);
        st.setProperty ("start", s.start, nullptr);
        st.setProperty ("len", s.len, nullptr);
        if (s.morph > 0.0f) st.setProperty ("morph", s.morph, nullptr);
        st.appendChild (snapTree (s.snap), nullptr);
        sg.appendChild (st, nullptr);
    }
    t.appendChild (m, nullptr);
    t.appendChild (sn, nullptr);
    t.appendChild (sg, nullptr);
    return t;
}

Model Model::fromTree (const juce::ValueTree& t)
{
    Model m;
    m.minutes = juce::jlimit (kMinMinutes, kMaxMinutes, (float) t.getProperty ("minutes", 1.0f));
    m.playStart = juce::jlimit (0.0f, 1.0f, (float) t.getProperty ("playStart", 0.0f));
    m.nextId = juce::jmax (1, (int) t.getProperty ("nextId", 1));
    for (auto c : t.getChildWithName ("mix")) m.mix.push_back (itemFrom (c));
    for (auto c : t.getChildWithName ("snaps")) m.snaps.push_back (snapFrom (c));
    for (auto c : t.getChildWithName ("segs"))
    {
        Segment s;
        s.id = c.getProperty ("id", 0);
        s.start = juce::jlimit (0.0f, 1.0f, (float) c.getProperty ("start", 0.0f));
        s.len = juce::jlimit (0.0002f, 1.0f, (float) c.getProperty ("len", 0.125f));
        s.morph = juce::jlimit (0.0f, 1.0f, (float) c.getProperty ("morph", 0.0f));
        s.snap = snapFrom (c.getChildWithName ("snap"));
        m.segs.push_back (s);
    }
    int maxId = 0;
    for (auto& i : m.mix) maxId = juce::jmax (maxId, i.id);
    for (auto& s : m.snaps) maxId = juce::jmax (maxId, s.id);
    for (auto& s : m.segs) maxId = juce::jmax (maxId, s.id);
    m.nextId = juce::jmax (m.nextId, maxId + 1);
    m.link();
    return m;
}
