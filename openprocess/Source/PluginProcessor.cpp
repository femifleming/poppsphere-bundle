#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <optional>

namespace
{
juce::AudioProcessorValueTreeState::ParameterLayout layout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    p.push_back (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "master", 1 }, "Master",
                                                              juce::NormalisableRange<float> (0.0f, 1.0f), 0.8f));
    // the effect lets its track through by default; the instrument has nothing coming in unless it is routed there
    p.push_back (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "thru", 1 }, "Input Thru",
                                                              juce::NormalisableRange<float> (0.0f, 1.0f),
                                                              OpProcessor::isInstrument() || OP_SPATIAL ? 0.0f : 1.0f));
   #if OP_SPATIAL
    p.push_back (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "order", 1 }, "Ambisonic Order",
                     juce::StringArray { "1st", "2nd", "3rd", "4th", "5th", "6th", "7th" }, 2));
    auto pct = [&] (const char* id, const char* name, float def) {
        p.push_back (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name, juce::NormalisableRange<float> (0.0f, 1.0f), def));
    };
    pct ("rolloff", "Distance Rolloff", 0.5f);
    pct ("air", "Distance Air", 0.5f);
    pct ("spread", "Near Spread", 0.5f);
    pct ("blur", "Distance Blur", 0.0f);
    p.push_back (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "near", 1 }, "Near Field",
                     juce::NormalisableRange<float> (0.0f, 0.4f), 0.12f));
    p.push_back (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "mspeed", 1 }, "Motion Speed",
                     juce::NormalisableRange<float> (0.1f, 4.0f, 0.0f, 0.43f), 1.0f));
    p.push_back (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "yaw", 1 }, "Scene Turn",
                     juce::NormalisableRange<float> (-180.0f, 180.0f), 0.0f));
    p.push_back (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "pitch", 1 }, "Scene Tilt",
                     juce::NormalisableRange<float> (-90.0f, 90.0f), 0.0f));
   #endif

    // host integration (1.4): the timeline's transport, snapshot recall, and one set per sound
    p.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "play", 2 }, "Timeline Play", false));
    p.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "sync", 2 }, "Follow Host", true));
    {
        juce::StringArray sc { "-" };
        for (int k = 1; k <= Model::kMaxSnaps; ++k) sc.add (juce::String (k));
        p.push_back (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "scene", 2 }, "Recall Snapshot", sc, 0));
    }
    using Attr = juce::AudioParameterFloatAttributes;
    auto number = [] (const juce::String& t) { return t.retainCharacters ("0123456789.-").getFloatValue(); };
    const auto percent = Attr().withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; })
                               .withValueFromStringFunction ([number] (const juce::String& t) { return number (t) / 100.0f; });
   #if OP_SPATIAL
    const auto around = Attr().withStringFromValueFunction ([] (float v, int) {
                                  const int deg = juce::roundToInt ((0.5f - v) * 360.0f);
                                  return deg == 0 ? juce::String ("front") : juce::String (std::abs (deg)) + (deg > 0 ? " deg left" : " deg right"); })
                              .withValueFromStringFunction ([number] (const juce::String& t) {
                                  const float deg = std::abs (number (t));
                                  return 0.5f - (t.containsIgnoreCase ("r") ? -deg : deg) / 360.0f; });
    const auto height = Attr().withStringFromValueFunction ([] (float v, int) {
                                  return juce::String (juce::roundToInt (space::kElLow + v * (space::kElHigh - space::kElLow))) + " deg"; })
                              .withValueFromStringFunction ([number] (const juce::String& t) { return space::yForElevationDeg (number (t)); });
    const auto metres = Attr().withStringFromValueFunction ([] (float v, int) { return juce::String (0.25f + 19.75f * v * v, 1) + " m"; })
                              .withValueFromStringFunction ([number] (const juce::String& t) {
                                  return std::sqrt (juce::jmax (0.0f, (number (t) - 0.25f) / 19.75f)); });
   #else
    const auto pan = Attr().withStringFromValueFunction ([] (float v, int) {
                               const int n = juce::roundToInt ((v - 0.5f) * 200.0f);
                               return n == 0 ? juce::String ("C") : juce::String (std::abs (n)) + (n < 0 ? " L" : " R"); })
                           .withValueFromStringFunction ([number] (const juce::String& t) {
                               const float n = std::abs (number (t));
                               return 0.5f + (t.containsIgnoreCase ("l") ? -n : n) / 200.0f; });
   #endif
    for (int k = 0; k < OpProcessor::kSlots; ++k)
    {
        const juce::String name = "Sound " + juce::String (k + 1).paddedLeft ('0', 2) + " ";
        auto add = [&] (int field, const char* what, float def, const Attr& a) {
            p.push_back (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { OpProcessor::slotParamId (k, field), 2 }, name + what,
                                                                      juce::NormalisableRange<float> (0.0f, 1.0f), def, a));
        };
       #if OP_SPATIAL
        add (OpProcessor::fX, "Around", 0.5f, around);
        add (OpProcessor::fY, "Height", space::kHorizonY, height);
        add (OpProcessor::fDist, "Distance", 0.3f, metres);
       #else
        add (OpProcessor::fX, "Pan", 0.5f, pan);
        add (OpProcessor::fY, "Level", 0.7f, percent);
       #endif
        add (OpProcessor::fStart, "Loop Start", 0.0f, percent);
        add (OpProcessor::fLength, "Loop Length", 1.0f, percent);
    }
    return { p.begin(), p.end() };
}

// A VST2 host takes a plug-in's outputs as they are. Where the tracks are stereo (Ableton Live and
// most others) openfield therefore has to be a stereo plug-in, or its first two Ambisonic channels
// (W, Y) would be played as left and right. Only hosts known to have tracks as wide as the plug-in
// get the 64 outputs.
bool stereoOnlyVst2Host()
{
    static const bool wide = [] {
        const juce::PluginHostType h;
        return h.isReaper() || h.isArdour() || h.isJUCEPluginHost() || h.isPluginval();
    }();
    return ! wide;
}

inline float softClip (float x)
{
    const float a = std::abs (x);
    if (a < 0.8f) return x;
    return std::copysign (0.8f + 0.2f * std::tanh ((a - 0.8f) / 0.2f), x);
}

std::unique_ptr<juce::AudioFormatWriter> openWav (const juce::File& f, double rate, int channels, int bits)
{
    f.getParentDirectory().createDirectory();
    f.deleteFile();
    std::unique_ptr<juce::OutputStream> os = std::make_unique<juce::FileOutputStream> (f);
    if (! static_cast<juce::FileOutputStream*> (os.get())->openedOk()) return nullptr;
    juce::WavAudioFormat wav;
    return wav.createWriterFor (os, juce::AudioFormatWriterOptions().withSampleRate (rate)
                                        .withNumChannels (channels).withBitsPerSample (bits));
}

bool writeWav (const juce::File& f, const juce::AudioBuffer<float>& b, double rate)
{
    auto w = openWav (f, rate, b.getNumChannels(), 16);
    return w != nullptr && w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}

} // namespace

// Stereo in. openprocess: stereo out. openfield: Ambisonics out - 3rd order (16 channels) as an
// Audio Unit, up to 7th (64) elsewhere so a track that wide has room; the order setting says how many
// are used. The desktop app plays a stereo preview (and renders full Ambisonic files).
SpaceParams OpProcessor::readSpace() const
{
    SpaceParams sp;
   #if OP_SPATIAL
    auto v = [&] (const char* id) { return params.getRawParameterValue (id)->load(); };
    sp.rolloff = v ("rolloff"); sp.air = v ("air"); sp.spread = v ("spread"); sp.blur = v ("blur"); sp.nearR = v ("near");
    sp.yaw = v ("yaw") * juce::MathConstants<float>::pi / 180.0f;
    sp.pitch = v ("pitch") * juce::MathConstants<float>::pi / 180.0f;
    sp.mspeed = v ("mspeed");
   #endif
    return sp;
}

juce::AudioProcessor::BusesProperties OpProcessor::buses()
{
   #if OP_SPATIAL
    const auto t = juce::PluginHostType::getPluginLoadedAs();
    if (t == juce::AudioProcessor::wrapperType_Standalone
        || (t == juce::AudioProcessor::wrapperType_VST && stereoOnlyVst2Host()))
        return juce::AudioProcessor::BusesProperties()
            .withInput ("Input", juce::AudioChannelSet::stereo(), true)
            .withOutput ("Output", juce::AudioChannelSet::stereo(), true);
    return juce::AudioProcessor::BusesProperties()
        .withInput ("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output", juce::AudioChannelSet::ambisonic (t == juce::AudioProcessor::wrapperType_AudioUnit ? 3 : 7), true);
   #else
    return juce::AudioProcessor::BusesProperties()
        .withInput ("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true);
   #endif
}

OpProcessor::OpProcessor()
    : AudioProcessor (buses()),
      params (*this, nullptr, "params", layout())
{
    formats.registerBasicFormats();
    specs.resize (Engine::kMaxSpecs);
    space::lorenzTable();
    playRaw = params.getRawParameterValue ("play");
    syncRaw = params.getRawParameterValue ("sync");
    sceneRaw = params.getRawParameterValue ("scene");
    for (int k = 0; k < kSlots; ++k)
        for (int f = 0; f < kNumFields; ++f)
            if (auto* prm = params.getParameter (slotParamId (k, f)))
            {
                slotPrm[(size_t) k][(size_t) f] = prm;
                slotRaw[(size_t) k][(size_t) f] = params.getRawParameterValue (slotParamId (k, f));
                slotSeen[(size_t) k][(size_t) f] = slotRaw[(size_t) k][(size_t) f]->load();
            }
   #if OP_SPATIAL
    // the effect is an encoder for its track: it starts with the live input placed straight ahead
    if (! isInstrument())
    {
        Item i;
        i.id = model.nextId++;
        i.cell = kLiveCell;
        i.x = 0.5f; i.y = space::kHorizonY; i.d = 0.3f;
        model.mix.push_back (i);
    }
   #endif
    model.link();     // (its place is the default of the "Sound 01" parameters, so they agree from the start)
    rebuildAll();
    startTimerHz (30);
}

bool OpProcessor::isInstrument()
{
   #if JucePlugin_IsSynth
    return true;
   #else
    return false;
   #endif
}

juce::String OpProcessor::slotParamId (int slot, int field)
{
    static const char* const tags[kNumFields] = { "x", "y", "s", "l", "d" };
    return "s" + juce::String (slot + 1).paddedLeft ('0', 2) + tags[field];
}

OpProcessor::~OpProcessor()
{
    stopTimer();
    pool.removeAllJobs (true, 10000);
}

bool OpProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet();
   #if OP_SPATIAL
    // REAPER asks a VST3 for the track's width on both sides, as plain speaker channels (16 in, 16 out
    // on a 16 channel track), and falls back to stereo if that is refused. Take any width from 4 up,
    // read the first two input channels and send the highest order that fits (see prepareToPlay).
    if (wrapperType == wrapperType_VST3 && out.size() >= 4 && out.size() <= ambi::kCh && l.getMainInputChannelSet() == out) return true;
    bool ok = out == juce::AudioChannelSet::stereo();
    if (! (wrapperType == wrapperType_VST && stereoOnlyVst2Host()))
        for (int o = 1; o <= ambi::kMaxOrder; ++o)
            ok = ok || out == juce::AudioChannelSet::ambisonic (o)
                    || (out == juce::AudioChannelSet::discreteChannels (ambi::chFor (o)) && wrapperType != wrapperType_AudioUnit);
    if (! ok) return false;
   #else
    if (out != juce::AudioChannelSet::stereo()) return false;
   #endif
    const auto in = l.getMainInputChannelSet();
    // outside VST3 a mono input would let the host feed only the left channel of a stereo track
    if (wrapperType != wrapperType_VST3 && in == juce::AudioChannelSet::mono()) return false;
    return in.isDisabled() || in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void OpProcessor::prepareToPlay (double rate, int block)
{
    hostRate = rate > 0 ? rate : 44100.0;
    engine.prepare (hostRate);
    scratchL.assign ((size_t) juce::jmax (block, 512), 0.0f);
    scratchR.assign ((size_t) juce::jmax (block, 512), 0.0f);
    inCopy.setSize (2, juce::jmax (block, 512));
    fold.setSize (4, juce::jmax (block, 512));
    ring.setSize (2, (int) (hostRate * 8.0));
    ring.clear();
    ringPos = 0;
    wasManual = false;
    // A VST3 bus of plain speaker channels (what REAPER gives a track that is not 36 or 64 wide): the
    // wrapper keeps the first 19 of them in order, so up to 3rd order (16 channels) arrives as written.
    // Ambisonic and discrete buses carry every order.
    orderCap = ambi::kMaxOrder;
    if (wrapperType == wrapperType_VST3)
        if (auto* bus = getBus (false, 0))
        {
            const auto& set = bus->getCurrentLayout();
            if (set.size() >= 4 && set.getAmbisonicOrder() < 0 && ! set.isDiscreteLayout()) orderCap = 3;
        }
}

void OpProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (n <= 0) return;

    for (const auto m : midi)
    {
        const auto msg = m.getMessage();
        if (msg.isNoteOn())
        {
            const auto w = midiFifo.write (1);
            if (w.blockSize1 > 0) midiNotes[(size_t) w.startIndex1] = msg.getNoteNumber();
        }
    }
    midi.clear();

    auto in = getBusBuffer (buffer, true, 0);
    const int inCh = in.getNumChannels();
    if (inCh > 0 && ring.getNumSamples() > 0)
    {
        int pos = ringPos.load();
        const int R = ring.getNumSamples();
        for (int i = 0; i < n; ++i)
        {
            ring.setSample (0, pos, in.getSample (0, i));
            ring.setSample (1, pos, in.getSample (juce::jmin (1, inCh - 1), i));
            if (++pos >= R) pos = 0;
        }
        ringPos = pos;
        inPeak = juce::jmax (in.getMagnitude (0, 0, n), in.getMagnitude (juce::jmin (1, inCh - 1), 0, n));   // (a wide bus: the first pair)
    }

    // the transport: the red flag / the Timeline Play parameter start it by hand; with Follow Host
    // the host's transport runs it, at the host's position
    if (const bool pp = playRaw->load() > 0.5f; pp != seenPlay.load())
    {
        seenPlay = pp;
        timelinePlayingFlag = pp;
    }
    bool hostPlaying = false;
    double hostSeconds = 0.0;
    if (syncRaw->load() > 0.5f && wrapperType != wrapperType_Standalone)
        if (auto* ph = getPlayHead())
            if (const auto pos = ph->getPosition(); pos.hasValue() && (pos->getIsPlaying() || pos->getIsRecording()))
            {
                if (const auto t = pos->getTimeInSeconds()) { hostPlaying = true; hostSeconds = *t; }
                else if (const auto smp = pos->getTimeInSamples()) { hostPlaying = true; hostSeconds = (double) *smp / hostRate; }
            }
    const bool manual = timelinePlayingFlag.load();

    bool playing = (hostPlaying || manual) && hasSegs;
    double tlLength = 60.0, startAt = 0.0;
    SpaceParams sp = readSpace();
    sp.flock = &flock;
    {
        const juce::SpinLock::ScopedTryLockType tl (lock);
        if (tl.isLocked())
        {
            hasSegs = ! model.segs.empty();
            playing = (hostPlaying || manual) && hasSegs;
            tlLength = model.minutes * 60.0;
            startAt = model.playStart * tlLength;
            if (hostPlaying) tlTime = hostSeconds < 0.0 ? hostSeconds : std::fmod (hostSeconds, tlLength);
            else if (manual && ! wasManual) tlTime = startAt;

            // the host's parameters: a snapshot to recall, and where the sounds are
            sceneFromHost (! playing);
            applySlotParams (model);

           #if OP_SPATIAL
            Engine::feedFlock (flock, model, playing, tlTime, (float) (n / hostRate), sp);
            if (const juce::SpinLock::ScopedTryLockType fl (flockLock); fl.isLocked()) flockShown = flock;
           #endif
            if (playing)
            {
                numSpecs = Engine::buildTimeline (model, cells, tlTime, specs.data(), (int) specs.size(), sp);
            }
            else
                numSpecs = Engine::buildMix (model, cells, motionClock, specs.data(), (int) specs.size(), sp);
        }
        else if (hostPlaying)
            tlTime += n / hostRate;       // the message thread holds the model for a moment: keep the last voices
        else if (manual && ! wasManual)
            tlTime = startAt;
    }
    wasManual = manual && ! hostPlaying;
    playingNow = playing;
    hostDriving = hostPlaying && playing;
    ++blockCount;
    samplesDone += n;
    motionNow = playing ? tlTime : motionClock;

    const float master = params.getRawParameterValue ("master")->load();
    auto out = getBusBuffer (buffer, false, 0);
    const int numOut = out.getNumChannels();

   #if OP_SPATIAL
    // the live input, kept before the output overwrites it (they can share a buffer)
    if (inCopy.getNumSamples() < n) inCopy.setSize (2, n, false, false, true);
    for (int ch = 0; ch < 2; ++ch)
        if (inCh > 0) inCopy.copyFrom (ch, 0, in, juce::jmin (ch, inCh - 1), 0, n);
        else inCopy.clear (ch, 0, n);
    const float* liveL = inCopy.getReadPointer (0);
    const float* liveR = inCopy.getReadPointer (1);

    const int wanted = 1 + (int) params.getRawParameterValue ("order")->load();
    if (numOut >= 4)
    {
        const int nch = juce::jmin (numOut, ambi::kCh);
        const int order = juce::jmin (wanted, ambi::orderFor (nch), orderCap);
        out.clear();
        engine.process (Engine::Output { out.getArrayOfWritePointers(), nch, order, &sp }, n, specs.data(), numSpecs, liveL, liveR);
        for (int c = 0; c < ambi::chFor (order); ++c)
        {
            float* d = out.getWritePointer (c);
            for (int i = 0; i < n; ++i) d[i] *= master * 0.7f;
        }
        orderNow = order;
    }
    else
    {
        // stereo: encode at 1st order and fold to left / right (W + Y, W - Y) so there is something to hear
        if (fold.getNumSamples() < n) fold.setSize (4, n, false, false, true);
        fold.clear();
        engine.process (Engine::Output { fold.getArrayOfWritePointers(), 4, 1, &sp }, n, specs.data(), numSpecs, liveL, liveR);
        const float* w = fold.getReadPointer (0);
        const float* y = fold.getReadPointer (1);
        for (int ch = 0; ch < numOut; ++ch)
        {
            float* d = out.getWritePointer (ch);
            const float sgn = ch == 0 ? 1.0f : -1.0f;
            for (int i = 0; i < n; ++i) d[i] = softClip ((w[i] + sgn * y[i]) * 0.7071f * master * 0.7f);
        }
        orderNow = 0;
    }
   #else
    if ((int) scratchL.size() < n) { scratchL.resize ((size_t) n); scratchR.resize ((size_t) n); }
    std::fill (scratchL.begin(), scratchL.begin() + n, 0.0f);
    std::fill (scratchR.begin(), scratchR.begin() + n, 0.0f);
    engine.process (scratchL.data(), scratchR.data(), n, specs.data(), numSpecs);
    const float thru = params.getRawParameterValue ("thru")->load();
    for (int ch = 0; ch < numOut; ++ch)
    {
        const float* src = ch == 0 ? scratchL.data() : scratchR.data();
        float* dst = out.getWritePointer (ch);
        const float* inp = inCh > 0 ? in.getReadPointer (juce::jmin (ch, inCh - 1)) : nullptr;
        // the instrument limits everything softly; the effect leaves its track's own audio untouched
        if (isInstrument())
            for (int i = 0; i < n; ++i)
                dst[i] = softClip (src[i] * master * 0.7f + (inp != nullptr ? thru * inp[i] : 0.0f));
        else
            for (int i = 0; i < n; ++i)
                dst[i] = softClip (src[i] * master * 0.7f) + (inp != nullptr ? thru * inp[i] : 0.0f);
    }
   #endif

    if (playing)
    {
        tlPos = juce::jmax (0.0, tlTime);
        if (! hostPlaying)
        {
            tlTime += n / hostRate;
            if (tlTime >= tlLength) tlTime = juce::jmin (startAt, tlLength - 0.01);
        }
    }
    motionClock += n / hostRate * sp.mspeed;   // the speed setting runs the clock, so changing it never jumps
    if (motionClock > 86400.0) motionClock = 0.0;
}

//==============================================================================
Model OpProcessor::getModel() const
{
    const juce::SpinLock::ScopedLockType sl (lock);
    return model;
}

void OpProcessor::setModel (const Model& m)
{
    edit ([&] (Model& mine) { mine = m; });
    timelinePlayingFlag = m.timelinePlaying;
    setPlayParam (m.timelinePlaying);
}

//== the host's parameters =========================================================
OpProcessor::SlotValues OpProcessor::slotValues (const Model& m)
{
    SlotValues v;
    for (auto& i : m.mix)
    {
        if (i.slot < 0 || i.slot >= kSlots) continue;
        v.has[i.slot] = true;
        auto& f = v.v[i.slot];
        f[fX] = i.x; f[fY] = i.y; f[fDist] = i.d;
        f[fStart] = i.s; f[fLength] = juce::jlimit (0.0f, 1.0f, i.e - i.s);
    }
    return v;
}

void OpProcessor::report (Reported& r, float value, bool notified) const
{
    r.latest = value;
    if (! notified) return;
    r.old[r.count++ & 3] = value;
    r.at = samplesDone.load();
}

bool OpProcessor::isStaleEcho (Reported& r, float value) const
{
    if (wrapperType != wrapperType_VST3 || r.count == 0) return false;
    if (samplesDone.load() - r.at > (juce::int64) (hostRate * 0.5)) { r.count = 0; return false; }   // half a second of audio later: forgotten
    if (std::abs (value - r.latest) < 1.0e-6f) return false;
    for (int i = 0; i < juce::jmin (r.count, 4); ++i)
        if (std::abs (value - r.old[i]) < 1.0e-6f) return true;
    return false;
}

// play area -> parameters (any thread but the audio thread). The audio thread then sees the
// parameters change and writes the same values back, which changes nothing.
void OpProcessor::pushSlots (const SlotValues& v)
{
    struct Change { juce::RangedAudioParameter* prm; float value; };
    std::array<Change, (size_t) (kSlots * kNumFields)> changes;
    int num = 0;
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        for (int k = 0; k < kSlots; ++k)
        {
            if (! v.has[k]) continue;
            for (int f = 0; f < kNumFields; ++f)
                if (auto* prm = slotPrm[(size_t) k][(size_t) f])
                {
                    const float n = juce::jlimit (0.0f, 1.0f, v.v[k][f]);
                    const bool differs = std::abs (prm->getValue() - n) > 1.0e-6f;
                    report (slotReported[(size_t) k][(size_t) f], n, differs);
                    if (differs) changes[(size_t) num++] = { prm, n };
                }
        }
    }
    for (int i = 0; i < num; ++i) changes[(size_t) i].prm->setValueNotifyingHost (changes[(size_t) i].value);
}

// parameters -> play area: whatever the host has changed since the last look. Lock held.
void OpProcessor::applySlotParams (Model& m)
{
    bool changed[kSlots][kNumFields];
    bool any = false;
    for (int k = 0; k < kSlots; ++k)
        for (int f = 0; f < kNumFields; ++f)
        {
            changed[k][f] = false;
            if (auto* raw = slotRaw[(size_t) k][(size_t) f])
            {
                const float p = raw->load (std::memory_order_relaxed);
                if (p == slotSeen[(size_t) k][(size_t) f]) continue;
                slotSeen[(size_t) k][(size_t) f] = p;
                auto& r = slotReported[(size_t) k][(size_t) f];
                if (isStaleEcho (r, p)) { repush = true; continue; }
                r.latest = p;
                changed[k][f] = true;
                any = true;
            }
        }
    if (! any) return;
    for (auto& i : m.mix)
    {
        if (i.slot < 0 || i.slot >= kSlots) continue;
        const auto& c = changed[i.slot];
        auto value = [&] (int f) { return slotRaw[(size_t) i.slot][(size_t) f] != nullptr ? slotSeen[(size_t) i.slot][(size_t) f] : 0.0f; };
        if (c[fX]) i.x = juce::jlimit (0.0f, 1.0f, value (fX));
        if (c[fY]) i.y = juce::jlimit (0.0f, 1.0f, value (fY));
        if (c[fDist]) i.d = juce::jlimit (0.0f, 1.0f, value (fDist));
        if (c[fStart] || c[fLength])
        {
            // (the one that did not change is taken from the sound, not from a parameter that may lag behind it)
            const float start = c[fStart] ? juce::jlimit (0.0f, 1.0f, value (fStart)) : i.s;
            const float length = c[fLength] ? juce::jlimit (0.0f, 1.0f, value (fLength)) : juce::jmax (0.0f, i.e - i.s);
            i.s = start;
            i.e = juce::jlimit (i.s, 1.0f, i.s + length);
        }
    }
}

// The Recall Snapshot parameter, as the host has set it. Lock held.
void OpProcessor::sceneFromHost (bool canRecall)
{
    const int sc = juce::roundToInt (sceneRaw->load());
    if (sc == seenScene) return;
    seenScene = sc;
    if (sc == sceneWanted) return;      // the plug-in set it itself: a reel dragged into the play area, or a session
    if (isStaleEcho (sceneReported, (float) sc)) { repush = true; return; }
    sceneWanted = sc;
    sceneReported.latest = (float) sc;
    if (sc >= 1 && canRecall) recallOnAudioThread (sc - 1);
}

void OpProcessor::setSceneParam (int value)
{
    auto* prm = params.getParameter ("scene");
    if (prm == nullptr) return;
    const bool differs = juce::roundToInt (sceneRaw->load()) != value;
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        sceneWanted = value;
        report (sceneReported, (float) value, differs);
    }
    if (differs) prm->setValueNotifyingHost (prm->convertTo0to1 ((float) value));
}

void OpProcessor::slotGesture (int itemId, std::initializer_list<int> fields, bool begin)
{
    int slot = -1;
    if (begin)
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        if (auto* i = model.findItem (itemId)) slot = i->slot;
    }
    for (int k = 0; k < kSlots; ++k)
        for (int f : fields)
        {
            auto* prm = slotPrm[(size_t) k][(size_t) f];
            if (prm == nullptr) continue;
            auto& held = slotHeld[(size_t) k][(size_t) f];
            if (begin && k == slot && ! held) { held = true; prm->beginChangeGesture(); }
            else if (! begin && held) { held = false; prm->endChangeGesture(); }
        }
}

bool OpProcessor::followsHost() const { return ! isStandalone() && syncRaw->load() > 0.5f; }

void OpProcessor::setFollowsHost (bool on)
{
    if (auto* prm = params.getParameter ("sync")) prm->setValueNotifyingHost (on ? 1.0f : 0.0f);
}

void OpProcessor::setPlayParam (bool on)
{
    if (auto* prm = params.getParameter ("play"))
        if ((prm->getValue() > 0.5f) != on) prm->setValueNotifyingHost (on ? 1.0f : 0.0f);
}

void OpProcessor::restoreSpace (const SpaceParams& sp)
{
   #if OP_SPATIAL
    const float deg = 180.0f / juce::MathConstants<float>::pi;
    const std::pair<const char*, float> vals[] = { { "rolloff", sp.rolloff }, { "air", sp.air }, { "spread", sp.spread },
        { "blur", sp.blur }, { "near", sp.nearR }, { "mspeed", sp.mspeed }, { "yaw", sp.yaw * deg }, { "pitch", sp.pitch * deg } };
    for (auto& v : vals)
        if (auto* prm = params.getParameter (v.first)) prm->setValueNotifyingHost (prm->convertTo0to1 (v.second));
   #else
    juce::ignoreUnused (sp);
   #endif
}

// The Recall Snapshot parameter, on the audio thread so that automation lands on the sample block
// it was written for (also in an offline bounce). The play area has room reserved (Model::link),
// so nothing is allocated; the message thread then brings the parameters into line.
void OpProcessor::recallOnAudioThread (int index)
{
    if (index < 0 || index >= (int) model.snaps.size()) return;
    const auto& src = model.snaps[(size_t) index].items;
    const size_t count = juce::jmin (src.size(), model.mix.capacity(), (size_t) Model::kMaxItems);
    model.mix.assign (src.begin(), src.begin() + (std::ptrdiff_t) count);
    for (auto& i : model.mix) { i.id = model.nextId++; i.motionStart = motionNow.load(); }
    model.fixSlots();
    recalled = index;
}

float OpProcessor::voiceFrac (int voiceId) const
{
    for (auto& p : engine.pub)
        if (p.id.load (std::memory_order_relaxed) == voiceId) return p.frac.load (std::memory_order_relaxed);
    return -1.0f;
}

int OpProcessor::addItem (int cell, float x, float y)
{
    int id = -1;
    edit ([&] (Model& m) {
        if ((int) m.mix.size() >= Model::kMaxItems) return;
        Item i;
        i.id = id = m.nextId++;
        i.cell = juce::jlimit (0, OP_SPATIAL ? kLiveCell : bank::kCells - 1, cell);
        i.x = juce::jlimit (0.0f, 1.0f, x);
        i.y = juce::jlimit (0.0f, 1.0f, y);
        i.d = 0.3f;
        m.mix.push_back (i);
    });
    return id;
}

void OpProcessor::applyPreset (int k, bool additive)
{
    k = juce::jlimit (0, 7, k);
    const int rows[3] = { k, (k + 3) % 8, (k + 5) % 8 };
    const int count = 3 + rng.nextInt (3);
    edit ([&] (Model& model) {
    if (! additive) model.mix.clear();
    for (int j = 0; j < count && (int) model.mix.size() < Model::kMaxItems; ++j)
    {
        const int row = (j == 0 || rng.nextFloat() < 0.5f) ? rows[0] : rows[1 + rng.nextInt (2)];
        Item i;
        i.id = model.nextId++;
        i.cell = row * bank::kCols + rng.nextInt (bank::kCols);
       #if OP_SPATIAL
        // spread round you, mostly a little above the horizon
        i.x = rng.nextFloat();
        i.y = space::yForElevationDeg (-15.0f + 55.0f * rng.nextFloat());
        i.d = 0.12f + 0.6f * rng.nextFloat();
        i.motion = rng.nextFloat() < 0.3f ? rng.nextInt (motion::Count) : motion::None;
        i.motionStart = motionNow.load();
        i.mseed = rng.nextFloat();
       #else
        i.x = 0.1f + 0.8f * rng.nextFloat();
        i.y = 0.4f + 0.55f * rng.nextFloat();
       #endif
        const float len = (k % 2 == 1) ? 0.03f + 0.22f * rng.nextFloat() : 0.4f + 0.6f * rng.nextFloat();
        i.s = (1.0f - len) * rng.nextFloat();
        i.e = i.s + len;
        i.effect = rng.nextFloat() < 0.25f ? rng.nextInt (3) : fx::None;
        model.mix.push_back (i);
    }
    });
}

void OpProcessor::operatorLength()
{
    edit ([&] (Model& m) {
        for (auto& i : m.mix)
        {
            const float len = std::exp (std::log (0.02f) * (1.0f - rng.nextFloat()));
            i.s = (1.0f - len) * rng.nextFloat();
            i.e = i.s + len;
        }
    });
}

void OpProcessor::operatorPosition()
{
    edit ([&] (Model& model) {
    for (auto& i : model.mix)
    {
        i.x = rng.nextFloat();
       #if OP_SPATIAL
        i.y = space::yForElevationDeg (-30.0f + 110.0f * rng.nextFloat());
        i.d = 0.08f + 0.75f * rng.nextFloat();
       #else
        i.y = 0.2f + 0.8f * rng.nextFloat();
       #endif
    }
    });
}

int OpProcessor::takeSnapshot()
{
    int id = -1;
    juce::String msg;
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        if (model.mix.empty()) msg = "the mix window is empty";
        else if ((int) model.snaps.size() >= Model::kMaxSnaps) msg = "no room - throw a snapshot away first";
        else
        {
            Snapshot s;
            s.id = id = model.nextId++;
            int c = 0;
            for (auto& o : model.snaps) c = juce::jmax (c, o.colour + 1);
            s.colour = model.snaps.empty() ? 0 : c % 6;
            s.items = model.mix;
           #if OP_SPATIAL
            s.hasSpace = true;
            s.space = readSpace();
           #endif
            model.snaps.push_back (s);
        }
    }
    if (msg.isNotEmpty()) setStatus (msg);
    return id;
}

void OpProcessor::loadSnapshot (int snapId)
{
    std::optional<SpaceParams> restore;
    int index = -1;
    edit ([&] (Model& m) {
        for (size_t k = 0; k < m.snaps.size(); ++k)
            if (m.snaps[k].id == snapId)
            {
                const auto s = m.snaps[k];
                index = (int) k;
                m.mix = s.items;
                for (auto& i : m.mix) { i.id = m.nextId++; i.motionStart = motionNow.load(); }
                if (s.hasSpace) restore = s.space;
            }
    });
    // its global settings come back too
    if (restore) restoreSpace (*restore);
    // the Recall Snapshot parameter shows it (the recall itself is done)
    if (index >= 0 && index < Model::kMaxSnaps) setSceneParam (index + 1);
}

bool OpProcessor::boidPos (int voiceId, float& x, float& y, float& d) const
{
    const juce::SpinLock::ScopedLockType fl (flockLock);
    if (auto* b = flockShown.find (voiceId)) { x = b->x; y = b->y; d = b->d; return true; }
    return false;
}

void OpProcessor::toggleTimeline()
{
    if (hostDriving.load())
    {
        setStatus ("the host is playing the timeline (click 'host' to play it by hand)");
        return;
    }
    bool hasSegments, on;
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        hasSegments = ! model.segs.empty();
        if (hasSegments || model.timelinePlaying) model.timelinePlaying = ! model.timelinePlaying;
        timelinePlayingFlag = on = model.timelinePlaying;
        playingNow = on && hasSegments;
    }
    setPlayParam (on);
    if (! hasSegments) setStatus ("drag snapshots onto the timeline first");
}

void OpProcessor::clearAll()
{
    edit ([&] (Model& m) {
        m.mix.clear();
        m.snaps.clear();
        m.segs.clear();
        m.playStart = 0.0f;
        m.timelinePlaying = false;
        timelinePlayingFlag = false;
        playingNow = false;
    });
    setPlayParam (false);
    setStatus ("cleared: a new piece");
}

void OpProcessor::clearMix()
{
    edit ([] (Model& m) { m.mix.clear(); });
}

void OpProcessor::handleMidi (int note)
{
    if (note >= 36 && note <= 43) { if (! timelinePlaying()) applyPreset (note - 36, false); }
    else if (note == 44) { if (! timelinePlaying()) operatorLength(); }
    else if (note == 45) { if (! timelinePlaying()) operatorPosition(); }
    else if (note == 46) { if (! timelinePlaying()) takeSnapshot(); }
    else if (note == 47) toggleTimeline();
    else if (note >= 48 && note < 48 + bank::kCells && ! timelinePlaying())
    {
        const int cell = note - 48;
        bool removed = false;
        edit ([&] (Model& m) {
            for (auto it = m.mix.begin(); it != m.mix.end(); ++it)
                if (it->cell == cell) { m.mix.erase (it); removed = true; break; }
        });
       #if OP_SPATIAL
        if (! removed) addItem (cell, rng.nextFloat(), space::yForElevationDeg (-10.0f + 50.0f * rng.nextFloat()));
       #else
        if (! removed) addItem (cell, rng.nextFloat(), 0.45f + 0.5f * rng.nextFloat());
       #endif
    }
}

void OpProcessor::timerCallback()
{
    const auto r = midiFifo.read (midiFifo.getNumReady());
    for (int i = 0; i < r.blockSize1; ++i) handleMidi (midiNotes[(size_t) (r.startIndex1 + i)]);
    for (int i = 0; i < r.blockSize2; ++i) handleMidi (midiNotes[(size_t) (r.startIndex2 + i)]);

    // the Timeline Play parameter and the model agree (the audio thread reads the parameter)
    {
        const bool on = timelinePlayingFlag.load();
        const juce::SpinLock::ScopedLockType sl (lock);
        model.timelinePlaying = on;
    }
    // a snapshot the audio thread recalled for the host: its parameters, and its global settings
    if (const int k = recalled.exchange (-1); k >= 0)
    {
        SlotValues v;
        std::optional<SpaceParams> restore;
        {
            const juce::SpinLock::ScopedLockType sl (lock);
            v = slotValues (model);
            if (k < (int) model.snaps.size() && model.snaps[(size_t) k].hasSpace) restore = model.snaps[(size_t) k].space;
        }
        pushSlots (v);
        if (restore) restoreSpace (*restore);
    }
    // nothing is being processed (the host's audio is off): the host's parameter changes still arrive
    if (const int bc = blockCount.load(); bc == lastBlockCount)
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        if (++idleTicks >= 4)
        {
            // really idle (not just between two long blocks): nothing is playing, and Timeline Play is read here
            if (const bool pp = playRaw->load() > 0.5f; pp != seenPlay.load())
            {
                seenPlay = pp;
                timelinePlayingFlag = pp;
            }
            playingNow = timelinePlayingFlag.load() && ! model.segs.empty();
            hostDriving = false;
        }
        sceneFromHost (! playingNow.load());
        applySlotParams (model);
    }
    else
    {
        lastBlockCount = bc;
        idleTicks = 0;
    }
    // the host sent back something the plug-in had reported earlier: say again how things are
    if (repush.exchange (false))
    {
        SlotValues v;
        int scene;
        {
            const juce::SpinLock::ScopedLockType sl (lock);
            v = slotValues (model);
            scene = sceneWanted;
        }
        pushSlots (v);
        setSceneParam (scene);
    }

    std::vector<std::shared_ptr<Sound>> dead;
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        const auto now = juce::Time::getMillisecondCounter();
        for (auto it = graveyard.begin(); it != graveyard.end();)
            if (now - it->second > 2000) { dead.push_back (std::move (it->first)); it = graveyard.erase (it); }
            else ++it;
    }
}

//== sounds =====================================================================
bool OpProcessor::cellReady (int cell) const
{
    const juce::SpinLock::ScopedLockType sl (lock);
    if (cell < 0 || cell >= bank::kCells) return true;
    return cells[(size_t) cell] != nullptr;
}

juce::String OpProcessor::cellName (int cell) const
{
    const juce::SpinLock::ScopedLockType sl (lock);
    if (cell < 0 || cell >= bank::kCells) return "input";
    return cells[(size_t) cell] != nullptr ? cells[(size_t) cell]->name : juce::String ("...");
}

double OpProcessor::cellSeconds (int cell) const
{
    const juce::SpinLock::ScopedLockType sl (lock);
    if (cell < 0 || cell >= bank::kCells) return 1.0;
    return cells[(size_t) cell] != nullptr ? cells[(size_t) cell]->seconds() : 1.0;
}

void OpProcessor::rebuildCell (int cell, const CellSource& src)
{
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        sources[(size_t) cell] = src;
    }
    ++pending;
    pool.addJob ([this, cell, src]
    {
        auto snd = bank::make (cell, src, formats);
        if (snd == nullptr) setStatus ("could not read " + juce::File (src.path).getFileName());
        else
        {
            const juce::SpinLock::ScopedLockType sl (lock);
            if (cells[(size_t) cell] != nullptr) graveyard.emplace_back (std::move (cells[(size_t) cell]), juce::Time::getMillisecondCounter());
            cells[(size_t) cell] = std::move (snd);
        }
        --pending;
    });
}

void OpProcessor::rebuildAll()
{
    for (int c = 0; c < bank::kCells; ++c) rebuildCell (c, CellSource());
}

void OpProcessor::loadBuiltin()
{
    rebuildAll();
    setStatus ("built-in sounds");
}

void OpProcessor::loadFiles (const juce::Array<juce::File>& files)
{
    const auto seed = (juce::uint32) rng.nextInt();
    ++pending;
    pool.addJob ([this, files, seed]
    {
        const auto audio = bank::collectAudioFiles (files, formats);
        const auto plan = bank::planFromFiles (audio, formats, seed);
        if (plan.empty()) setStatus ("no audio files found");
        else
        {
            setStatus ("chopping " + juce::String (audio.size()) + (audio.size() == 1 ? " file" : " files") + " ...");
            for (int c = 0; c < bank::kCells; ++c)
            {
                auto snd = bank::make (c, plan[(size_t) c], formats);
                if (snd == nullptr) continue;
                const juce::SpinLock::ScopedLockType sl (lock);
                sources[(size_t) c] = plan[(size_t) c];
                if (cells[(size_t) c] != nullptr) graveyard.emplace_back (std::move (cells[(size_t) c]), juce::Time::getMillisecondCounter());
                cells[(size_t) c] = std::move (snd);
            }
            setStatus ("loaded " + juce::String (audio.size()) + (audio.size() == 1 ? " file" : " files"));
        }
        --pending;
    });
}

void OpProcessor::loadFileIntoCell (int cell, const juce::File& f)
{
    CellSource c;
    c.kind = CellSource::File;
    c.path = f.getFullPathName();
    c.length = 30.0;
    rebuildCell (cell, c);
    setStatus (f.getFileName());
}

void OpProcessor::reskip (int cell)
{
    CellSource c;
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        c = sources[(size_t) cell];
    }
    c.seed = (juce::uint32) rng.nextInt() | 1u;
    if (c.kind == CellSource::File)
    {
        c.kind = CellSource::Skipped;
        c.length = bank::loopSeconds (cell);
        c.offset = 0;
    }
    c.intensity = 0.3f + 0.6f * rng.nextFloat();
    rebuildCell (cell, c);
    setStatus ("skipped again");
}

void OpProcessor::captureInput (int cell)
{
    const int R = ring.getNumSamples();
    if (R == 0) { setStatus ("no input"); return; }
    juce::AudioBuffer<float> b (2, R);
    const int pos = ringPos.load();
    for (int ch = 0; ch < 2; ++ch)
    {
        b.copyFrom (ch, 0, ring, ch, pos, R - pos);
        if (pos > 0) b.copyFrom (ch, R - pos, ring, ch, 0, pos);
    }
    int first = R, last = -1;
    for (int i = 0; i < R; ++i)
        if (std::abs (b.getSample (0, i)) > 0.001f || std::abs (b.getSample (1, i)) > 0.001f)
        {
            first = juce::jmin (first, i);
            last = i;
        }
    if (last - first < (int) (hostRate * 0.05)) { setStatus ("no input to capture"); return; }
    juce::AudioBuffer<float> t (2, last - first + 1);
    for (int ch = 0; ch < 2; ++ch) t.copyFrom (ch, 0, b, ch, first, t.getNumSamples());
    const auto f = userFolder().getChildFile ("captures")
                       .getChildFile ("capture-" + juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S") + ".wav");
    if (! writeWav (f, t, hostRate)) { setStatus ("could not write " + f.getFullPathName()); return; }
    loadFileIntoCell (cell, f);
    setStatus ("captured " + juce::String (t.getNumSamples() / hostRate, 1) + " s");
}

void OpProcessor::renderToFile (const juce::File& file)
{
    Model m;
    CellArray cs;
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        m = model;
        cs = cells;
    }
    const float master = params.getRawParameterValue ("master")->load();
   #if OP_SPATIAL
    const int order = 1 + (int) params.getRawParameterValue ("order")->load();
   #else
    const int order = -1;
   #endif
    const SpaceParams sp = readSpace();
    for (auto& i : m.mix) i.motionStart = 0;   // a render starts every motion from the beginning
    auto renderFlock = std::make_shared<Flock>();
    ++pending;
    setStatus ("rendering ...");
    pool.addJob ([this, m, cs, file, master, order, sp, renderFlock]
    {
        // written as it goes (a 30 minute piece in 64 channels would not fit in memory):
        // openprocess 16-bit stereo, openfield 24-bit AmbiX (ACN / SN3D) at the chosen order
        constexpr double rate = 48000.0;
        const double L = m.minutes * 60.0;
        const bool useTimeline = ! m.segs.empty();
        const double start = useTimeline ? m.playStart * L : 0.0;
        const juce::int64 total = (juce::int64) ((L - start) * rate);
        const int nch = order < 0 ? 2 : ambi::chFor (order);
        auto w = openWav (file, rate, nch, order < 0 ? 16 : 24);
        if (w == nullptr) { setStatus ("could not write " + file.getFullPathName()); --pending; return; }
        Engine e;
        e.prepare (rate);
        std::vector<VoiceSpec> vs (Engine::kMaxSpecs);
        constexpr int block = 256, chunk = 48000;
        juce::AudioBuffer<float> buf (nch, chunk);
        bool ok = true;
        int lastPct = -1;
        for (juce::int64 pos = 0; pos < total && ok; pos += chunk)
        {
            const int len = (int) juce::jmin ((juce::int64) chunk, total - pos);
            buf.clear();
            for (int k = 0; k < len; k += block)
            {
                const int n = juce::jmin (block, len - k);
                const double t = start + (double) (pos + k) / rate;
                SpaceParams bsp = sp;
                bsp.flock = renderFlock.get();
               #if OP_SPATIAL
                Engine::feedFlock (*renderFlock, m, useTimeline, t, (float) n / (float) rate, sp);
               #endif
                const int ns = useTimeline ? Engine::buildTimeline (m, cs, t, vs.data(), (int) vs.size(), bsp)
                                           : Engine::buildMix (m, cs, t * sp.mspeed, vs.data(), (int) vs.size(), bsp);
                float* ptrs[ambi::kCh];
                for (int c = 0; c < nch; ++c) ptrs[c] = buf.getWritePointer (c, k);
                e.process (Engine::Output { ptrs, nch, order, &sp }, n, vs.data(), ns);
            }
            for (int c = 0; c < nch; ++c)
            {
                auto* d = buf.getWritePointer (c);
                for (int i = 0; i < len; ++i) d[i] = order < 0 ? softClip (d[i] * master * 0.7f) : juce::jlimit (-1.0f, 1.0f, d[i] * master * 0.7f);
            }
            ok = w->writeFromAudioSampleBuffer (buf, 0, len);
            const int pct = (int) (100 * (pos + len) / juce::jmax ((juce::int64) 1, total));
            if (pct / 5 != lastPct / 5) { setStatus ("rendering ... " + juce::String (pct) + " %"); lastPct = pct; }
        }
        w.reset();
        setStatus (ok ? "rendered " + file.getFileName() : "could not write " + file.getFullPathName());
        --pending;
    });
}

juce::File OpProcessor::userFolder()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile (productName());
}

juce::String OpProcessor::status() const
{
    const juce::ScopedLock sl (statusLock);
    return juce::Time::getMillisecondCounter() - statusTime < 3500 ? statusText : juce::String();
}

void OpProcessor::setStatus (const juce::String& s)
{
    const juce::ScopedLock sl (statusLock);
    statusText = s;
    statusTime = juce::Time::getMillisecondCounter();
}

//==============================================================================
void OpProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::ValueTree root (productName());
    auto p = params.copyState();
    // a session never comes back with the timeline already running by hand
    if (auto play = p.getChildWithProperty ("id", "play"); play.isValid()) play.setProperty ("value", 0.0f, nullptr);
    root.appendChild (p, nullptr);
    auto m = getModel();
    m.timelinePlaying = false;
    root.appendChild (m.toTree(), nullptr);
    juce::ValueTree b ("bank");
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        for (auto& s : sources) b.appendChild (s.toTree(), nullptr);
    }
    root.appendChild (b, nullptr);
    if (const int w = uiW.load(); w > 0) root.setProperty ("uiWidth", w, nullptr);
    if (auto xml = root.createXml()) copyXmlToBinary (*xml, dest);
}

void OpProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr) return;
    auto root = juce::ValueTree::fromXml (*xml);
    if (! root.hasType (productName()) && ! (OP_SPATIAL && root.hasType ("opensphere"))) return;
    auto p = root.getChildWithName (params.state.getType());
    if (p.isValid())
    {
        if (auto play = p.getChildWithProperty ("id", "play"); play.isValid()) play.setProperty ("value", 0.0f, nullptr);
        // the snapshot named by Recall Snapshot is not recalled again: the play area is in the session as it was left
        if (auto sc = p.getChildWithProperty ("id", "scene"); sc.isValid())
        {
            const juce::SpinLock::ScopedLockType sl (lock);
            sceneWanted = juce::roundToInt ((float) sc.getProperty ("value", 0.0f));
            sceneReported.latest = (float) sceneWanted;
        }
        params.replaceState (p);
    }
    // the play area as saved; the "Sound nn" parameters are brought into line with it (sessions from
    // before 1.4 have none)
    auto m = Model::fromTree (root.getChildWithName ("model"));
    m.timelinePlaying = false;
    setModel (m);
    uiW = juce::jlimit (0, 4000, (int) root.getProperty ("uiWidth", 0));
    auto b = root.getChildWithName ("bank");
    for (int c = 0; c < juce::jmin (bank::kCells, b.getNumChildren()); ++c)
    {
        const auto src = CellSource::fromTree (b.getChild (c));
        CellSource cur;
        {
            const juce::SpinLock::ScopedLockType sl (lock);
            cur = sources[(size_t) c];
        }
        if (src.kind != cur.kind || src.path != cur.path || src.seed != cur.seed || src.offset != cur.offset
            || src.length != cur.length || src.intensity != cur.intensity)
            rebuildCell (c, src);
    }
}

juce::AudioProcessorEditor* OpProcessor::createEditor() { return new OpEditor (*this); }

#if ! OP_TEST_BUILD
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new OpProcessor(); }
#endif
