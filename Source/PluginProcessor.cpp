#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace ql;
using APF = juce::AudioParameterFloat;
using APC = juce::AudioParameterChoice;
using APB = juce::AudioParameterBool;
using NR = juce::NormalisableRange<float>;

juce::String ql::bandId (int b, const char* what) { return "b" + juce::String (b + 1) + "_" + what; }

namespace
{
juce::AudioProcessorValueTreeState::ParameterLayout makeLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    auto f = [&] (const char* id, const char* name, NR r, float def, const char* unit = "")
    { l.add (std::make_unique<APF> (juce::ParameterID { id, 1 }, name, r, def, juce::AudioParameterFloatAttributes().withLabel (unit))); };
    auto c = [&] (const char* id, const char* name, juce::StringArray ch, int def) { l.add (std::make_unique<APC> (juce::ParameterID { id, 1 }, name, ch, def)); };
    auto b = [&] (const char* id, const char* name, bool def) { l.add (std::make_unique<APB> (juce::ParameterID { id, 1 }, name, def)); };

    c ("mode", "Mode", { "Soft", "Hard" }, 0);
    f ("depth", "Depth", NR (0, 20, 0.01f), 5.0f);
    f ("detail", "Detail", NR (0, 10, 0.01f), 5.0f);
    f ("attack", "Attack", NR (0, 10, 0.01f), 4.0f);
    f ("release", "Release", NR (0, 10, 0.01f), 5.0f);
    f ("detailTiltLo", "Detail tilt low", NR (-100, 100, 0.1f), 0, "%");
    f ("detailTiltHi", "Detail tilt high", NR (-100, 100, 0.1f), 0, "%");
    f ("attackTiltLo", "Attack tilt low", NR (-100, 100, 0.1f), 0, "%");
    f ("attackTiltHi", "Attack tilt high", NR (-100, 100, 0.1f), 0, "%");
    f ("releaseTiltLo", "Release tilt low", NR (-100, 100, 0.1f), 0, "%");
    f ("releaseTiltHi", "Release tilt high", NR (-100, 100, 0.1f), 0, "%");
    c ("stereoMode", "Stereo mode", { "L/R", "M/S" }, 0);
    f ("link", "Stereo link", NR (0, 100, 0.1f), 100, "%");
    f ("focus", "Stereo focus", NR (-100, 100, 0.1f), 0, "%");
    f ("maxCut", "Max cut", NR (0, 40, 0.1f), 40, "dB");
    f ("wetTrim", "Wet trim", NR (-12, 12, 0.01f), 0, "dB");
    f ("mix", "Mix", NR (0, 100, 0.1f), 100, "%");
    f ("outGain", "Out gain", NR (-24, 24, 0.01f), 0, "dB");
    b ("delta", "Delta", false);
    b ("bypass", "Bypass", false);
    b ("sidechain", "Sidechain", false);
    b ("scListen", "Sidechain listen", false);
    c ("quality", "Quality", { "Normal", "High", "Ultra" }, 0);
    b ("linear", "Linear phase", false);
    b ("lowLatency", "Low latency", false);
    b ("renderUltra", "Ultra when rendering", true);
    c ("listen", "Band listen", { "Off", "1", "2", "3", "4", "5", "6", "7", "8" }, 0);

    juce::StringArray types;
    for (int t = 0; t < kNumTypes; ++t) types.add (bandTypeName (t));
    for (int i = 0; i < kBands; ++i)
    {
        auto pid = [&] (const char* w) { return juce::ParameterID { bandId (i, w), 1 }; };
        const juce::String n = "Band " + juce::String (i + 1) + " ";
        l.add (std::make_unique<APB> (pid ("used"), n + "used", false));
        l.add (std::make_unique<APB> (pid ("on"), n + "on", true));
        l.add (std::make_unique<APC> (pid ("type"), n + "type", types, (int) Bell));
        NR fr (20.0f, 20000.0f, 0.0f, 1.0f);
        fr.setSkewForCentre (632.0f);
        l.add (std::make_unique<APF> (pid ("freq"), n + "freq", fr, 1000.0f, juce::AudioParameterFloatAttributes().withLabel ("Hz")));
        l.add (std::make_unique<APF> (pid ("gain"), n + "depth", NR (-24, 24, 0.01f), 0.0f, juce::AudioParameterFloatAttributes().withLabel ("dB")));
        NR qr (0.1f, 10.0f, 0.0f, 1.0f);
        qr.setSkewForCentre (1.0f);
        l.add (std::make_unique<APF> (pid ("q"), n + "Q/slope", qr, 1.0f));
        l.add (std::make_unique<APF> (pid ("focus"), n + "focus", NR (-100, 100, 0.1f), 0.0f, juce::AudioParameterFloatAttributes().withLabel ("%")));
    }
    return l;
}
} // namespace

QuellProcessor::QuellProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                          .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, &undo, "Quell", makeLayout())
{
    abStates[0] = apvts.copyState();
    abStates[1] = apvts.copyState();
}

bool QuellProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    if (in != out) return false;
    if (in != juce::AudioChannelSet::mono() && in != juce::AudioChannelSet::stereo()) return false;
    if (layouts.inputBuses.size() > 1)
    {
        const auto sc = layouts.getChannelSet (true, 1);
        if (! sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo()) return false;
    }
    return true;
}

void QuellProcessor::set (const juce::String& id, float v)
{
    if (auto* p = apvts.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (v));
}

Settings QuellProcessor::currentSettings() const
{
    Settings s;
    s.mode = (int) get ("mode");
    s.depth = get ("depth"); s.detail = get ("detail"); s.attack = get ("attack"); s.release = get ("release");
    s.detailTilt[0] = get ("detailTiltLo") / 100.0f; s.detailTilt[1] = get ("detailTiltHi") / 100.0f;
    s.attackTilt[0] = get ("attackTiltLo") / 100.0f; s.attackTilt[1] = get ("attackTiltHi") / 100.0f;
    s.releaseTilt[0] = get ("releaseTiltLo") / 100.0f; s.releaseTilt[1] = get ("releaseTiltHi") / 100.0f;
    s.stereoMode = (int) get ("stereoMode");
    s.link = get ("link") / 100.0f; s.focus = get ("focus") / 100.0f;
    s.maxCut = get ("maxCut"); s.wetTrim = get ("wetTrim"); s.mix = get ("mix") / 100.0f; s.outGain = get ("outGain");
    s.delta = get ("delta") > 0.5f; s.bypass = get ("bypass") > 0.5f; s.sidechain = get ("sidechain") > 0.5f;
    s.quality = (int) get ("quality"); s.linearPhase = get ("linear") > 0.5f; s.lowLatency = get ("lowLatency") > 0.5f;
    s.listenBand = (int) get ("listen") - 1;
    for (int i = 0; i < kBands; ++i)
    {
        auto& b = s.bands[(size_t) i];
        b.used = get (bandId (i, "used")) > 0.5f; b.on = get (bandId (i, "on")) > 0.5f;
        b.type = (int) get (bandId (i, "type")); b.freq = get (bandId (i, "freq")); b.gain = get (bandId (i, "gain"));
        b.q = get (bandId (i, "q")); b.focus = get (bandId (i, "focus")) / 100.0f;
    }
    if (s.listenBand >= 0 && ! s.bands[(size_t) s.listenBand].used) s.listenBand = -1;
    return s;
}

void QuellProcessor::updateLatency (const Settings& s)
{
    const int l = engine.latencyFor (s.lowLatency, s.linearPhase);
    if (l != latency) { latency = l; setLatencySamples (l); }
}

void QuellProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const int ch = juce::jmax (1, getMainBusNumInputChannels());
    mainChannels = ch;
    engine.prepare (sampleRate, ch);
    scratch.setSize (2, samplesPerBlock);
    scBuf.setSize (2, samplesPerBlock);
    dryDelay.setSize (2, engine.fftSize() * 2 + 8);
    dryDelay.clear();
    dryPos = 0;
    latency = -1;
    updateLatency (currentSettings());
    bypassMix = get ("bypass") > 0.5f ? 1.0f : 0.0f;
    outGainSm = juce::Decibels::decibelsToGain (get ("outGain"));
}

void QuellProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals nd;
    auto s = currentSettings();
    if (isNonRealtime() && get ("renderUltra") > 0.5f && ! s.lowLatency) s.quality = 2;
    updateLatency (s);

    auto mainIn = getBusBuffer (buffer, true, 0);
    const int ch = juce::jmin (2, mainIn.getNumChannels()), ns = buffer.getNumSamples();
    if (ch <= 0 || ns <= 0) return;
    if (scratch.getNumSamples() < ns) { scratch.setSize (2, ns, false, false, true); scBuf.setSize (2, ns, false, false, true); }

    // sidechain bus (falls back to the main input when it is off or missing)
    const float* scPtr[2] = { nullptr, nullptr };
    bool haveSc = false;
    if (getBusCount (true) > 1 && getBus (true, 1)->isEnabled())
    {
        auto sc = getBusBuffer (buffer, true, 1);
        if (sc.getNumChannels() > 0)
        {
            haveSc = true;
            for (int c = 0; c < ch; ++c) scBuf.copyFrom (c, 0, sc, juce::jmin (c, sc.getNumChannels() - 1), 0, ns);
        }
    }
    scConnected = haveSc;
    if (haveSc)
    {
        float pk = 0;
        for (int c = 0; c < ch; ++c) pk = juce::jmax (pk, scBuf.getMagnitude (c, 0, ns));
        scLevel = juce::jmax (pk, scLevel.load() * 0.8f);
    }
    else scLevel = 0.0f;
    s.sidechain = s.sidechain && haveSc;

    // keep a delayed dry copy for bypass (and sidechain listen timing)
    for (int c = 0; c < ch; ++c) scratch.copyFrom (c, 0, mainIn, c, 0, ns);
    const int dl = dryDelay.getNumSamples();

    // L/R -> M/S
    auto toMS = [&] (juce::AudioBuffer<float>& b)
    {
        for (int i = 0; i < ns; ++i)
        {
            const float l = b.getSample (0, i), r = b.getSample (1, i);
            b.setSample (0, i, 0.5f * (l + r)); b.setSample (1, i, 0.5f * (l - r));
        }
    };
    const bool ms = ch == 2 && s.stereoMode == 1;
    if (ms) { toMS (scratch); if (haveSc) toMS (scBuf); }
    if (haveSc) { scPtr[0] = scBuf.getReadPointer (0); scPtr[1] = scBuf.getReadPointer (ch > 1 ? 1 : 0); }

    float* io[2] = { scratch.getWritePointer (0), scratch.getWritePointer (ch > 1 ? 1 : 0) };
    engine.process (io, s.sidechain ? scPtr : nullptr, ns, s);

    if (ms)
        for (int i = 0; i < ns; ++i)
        {
            const float m = scratch.getSample (0, i), sd = scratch.getSample (1, i);
            scratch.setSample (0, i, m + sd); scratch.setSample (1, i, m - sd);
        }

    const bool scListen = get ("scListen") > 0.5f && haveSc;
    const float targetGain = juce::Decibels::decibelsToGain (s.outGain);
    const float targetBypass = s.bypass ? 1.0f : 0.0f;
    const float step = 1.0f / (0.02f * (float) getSampleRate());
    auto scIn = haveSc ? getBusBuffer (buffer, true, 1) : juce::AudioBuffer<float>();
    for (int i = 0; i < ns; ++i)
    {
        outGainSm += (targetGain - outGainSm) * 0.002f;
        bypassMix = targetBypass > bypassMix ? juce::jmin (targetBypass, bypassMix + step) : juce::jmax (targetBypass, bypassMix - step);
        const int rd = (dryPos - latency + dl) % dl;
        for (int c = 0; c < ch; ++c)
        {
            const float x = mainIn.getSample (c, i);
            dryDelay.setSample (c, dryPos, scListen ? scIn.getSample (juce::jmin (c, scIn.getNumChannels() - 1), i) : x);
            const float dry = dryDelay.getSample (c, latency == 0 ? dryPos : rd);
            const float wet = scratch.getSample (c, i) * outGainSm;
            buffer.setSample (c, i, scListen ? dry : wet + (dry - wet) * bypassMix);
        }
        dryPos = (dryPos + 1) % dl;
    }
    for (int c = ch; c < getTotalNumOutputChannels(); ++c) buffer.clear (c, 0, ns);
}

//==================================================================================================
void QuellProcessor::switchAB (int slot)
{
    if (slot == abSlot) return;
    abStates[abSlot] = apvts.copyState();
    abSlot = slot;
    apvts.replaceState (abStates[abSlot].createCopy());
    undo.clearUndoHistory();
}

void QuellProcessor::copyAB()
{
    abStates[abSlot] = apvts.copyState();
    abStates[1 - abSlot] = abStates[abSlot].createCopy();
}

void QuellProcessor::resetToDefaults()
{
    undo.beginNewTransaction();
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (rp->getParameterID() != "renderUltra" && rp->getParameterID() != "lowLatency")
                rp->setValueNotifyingHost (rp->getDefaultValue());
}

void QuellProcessor::resetBand (int i)
{
    for (auto w : { "on", "type", "freq", "gain", "q", "focus" })
        if (auto* rp = apvts.getParameter (bandId (i, w))) rp->setValueNotifyingHost (rp->getDefaultValue());
}

void QuellProcessor::loadFactory (int i)
{
    const auto& list = factoryPresets();
    if (i < 0 || i >= (int) list.size()) return;
    resetToDefaults();
    list[(size_t) i].apply (apvts);
    presetName = list[(size_t) i].name;
}

juce::File QuellProcessor::userPresetFolder() const
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Quell/Presets");
}

bool QuellProcessor::saveUserPreset (const juce::String& name)
{
    auto dir = userPresetFolder();
    dir.createDirectory();
    auto st = apvts.copyState();
    if (auto xml = st.createXml())
    {
        xml->setAttribute ("presetName", name);
        if (xml->writeTo (dir.getChildFile (juce::File::createLegalFileName (name) + ".quell")))
        {
            presetName = name;
            return true;
        }
    }
    return false;
}

bool QuellProcessor::loadPresetFile (const juce::File& f)
{
    if (auto xml = juce::XmlDocument::parse (f))
        if (xml->hasTagName (apvts.state.getType()))
        {
            undo.beginNewTransaction();
            auto vt = juce::ValueTree::fromXml (*xml);
            // keep the low-latency and render settings of this instance (they are locked like quality)
            for (auto id : { "lowLatency", "renderUltra" })
                for (auto child : vt)
                    if (child.getProperty ("id").toString() == id)
                        child.setProperty ("value", get (id), nullptr);
            apvts.replaceState (vt);
            presetName = xml->getStringAttribute ("presetName", f.getFileNameWithoutExtension());
            return true;
        }
    return false;
}

void QuellProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto st = apvts.copyState();
    abStates[abSlot] = st.createCopy();
    juce::ValueTree root ("QuellState");
    root.setProperty ("ab", abSlot, nullptr);
    root.setProperty ("preset", presetName, nullptr);
    root.setProperty ("w", uiWidth, nullptr);
    root.setProperty ("h", uiHeight, nullptr);
    juce::ValueTree a ("A"), b ("B");
    a.addChild (abStates[0].createCopy(), -1, nullptr);
    b.addChild (abStates[1].createCopy(), -1, nullptr);
    root.addChild (a, -1, nullptr);
    root.addChild (b, -1, nullptr);
    if (auto xml = root.createXml()) copyXmlToBinary (*xml, dest);
}

void QuellProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr) return;
    auto root = juce::ValueTree::fromXml (*xml);
    if (root.hasType ("QuellState"))
    {
        abSlot = juce::jlimit (0, 1, (int) root.getProperty ("ab", 0));
        presetName = root.getProperty ("preset", "Default").toString();
        uiWidth = root.getProperty ("w", 0); uiHeight = root.getProperty ("h", 0);
        for (int i = 0; i < 2; ++i)
        {
            auto slot = root.getChildWithName (i == 0 ? "A" : "B");
            if (slot.isValid() && slot.getNumChildren() > 0) abStates[i] = slot.getChild (0).createCopy();
        }
        apvts.replaceState (abStates[abSlot].createCopy());
    }
    else if (root.hasType (apvts.state.getType())) apvts.replaceState (root);
    undo.clearUndoHistory();
}

//==================================================================================================
const std::vector<FactoryPreset>& ql::factoryPresets()
{
    using A = juce::AudioProcessorValueTreeState;
    auto setv = [] (A& a, const juce::String& id, float v) { if (auto* p = a.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v)); };
    auto band = [setv] (A& a, int i, int type, float f, float g, float q)
    {
        setv (a, bandId (i, "used"), 1); setv (a, bandId (i, "on"), 1); setv (a, bandId (i, "type"), (float) type);
        setv (a, bandId (i, "freq"), f); setv (a, bandId (i, "gain"), g); setv (a, bandId (i, "q"), q);
    };
    static const std::vector<FactoryPreset> list {
        { "Default", "General", [] (A&) {} },
        { "Gentle smoothing", "General", [=] (A& a) { setv (a, "depth", 3); setv (a, "detail", 4); setv (a, "attack", 5); setv (a, "release", 6); } },
        { "Vocal harshness", "Vocals", [=] (A& a) { setv (a, "depth", 6); setv (a, "detail", 6); setv (a, "attack", 3); setv (a, "release", 4);
              band (a, 0, LowCut, 900, 0, 2); band (a, 1, Bell, 3500, 6, 0.8f); band (a, 2, HighShelf, 9000, -4, 1); } },
        { "De-ess", "Vocals", [=] (A& a) { setv (a, "depth", 8); setv (a, "detail", 3); setv (a, "attack", 1.5f); setv (a, "release", 3);
              band (a, 0, BandPass, 7000, 4, 0.9f); } },
        { "Boxy low mids", "Mix", [=] (A& a) { setv (a, "depth", 5); setv (a, "detail", 3); setv (a, "attack", 6); setv (a, "release", 7);
              band (a, 0, LowCut, 120, 0, 2); band (a, 1, HighCut, 1200, 0, 2); } },
        { "Drum overheads", "Drums", [=] (A& a) { setv (a, "depth", 7); setv (a, "detail", 8.5f); setv (a, "attack", 2); setv (a, "release", 3);
              setv (a, "stereoMode", 1); band (a, 0, LowCut, 400, 0, 2); } },
        { "Acoustic guitar", "Instruments", [=] (A& a) { setv (a, "depth", 5); setv (a, "detail", 6); setv (a, "attack", 4); setv (a, "release", 5);
              band (a, 0, LowCut, 150, 0, 1.5f); band (a, 1, Bell, 2500, 3, 1); } },
        { "Bus glue", "Mix", [=] (A& a) { setv (a, "depth", 2.5f); setv (a, "detail", 4); setv (a, "attack", 6); setv (a, "release", 7); setv (a, "mix", 70);
              setv (a, "stereoMode", 1); } },
        { "Sound design (hard)", "FX", [=] (A& a) { setv (a, "mode", 1); setv (a, "depth", 16); setv (a, "detail", 9); setv (a, "attack", 1); setv (a, "release", 2); } },
        { "Sidechain ducking", "FX", [=] (A& a) { setv (a, "sidechain", 1); setv (a, "mode", 1); setv (a, "depth", 8); setv (a, "detail", 5); setv (a, "attack", 2); setv (a, "release", 5); } },
    };
    return list;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new QuellProcessor(); }
