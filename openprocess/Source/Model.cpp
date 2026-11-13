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
        s.snap = snapFrom (c.getChildWithName ("snap"));
        m.segs.push_back (s);
    }
    int maxId = 0;
    for (auto& i : m.mix) maxId = juce::jmax (maxId, i.id);
    for (auto& s : m.snaps) maxId = juce::jmax (maxId, s.id);
    for (auto& s : m.segs) maxId = juce::jmax (maxId, s.id);
    m.nextId = juce::jmax (m.nextId, maxId + 1);
    return m;
}
