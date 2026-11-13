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
    p.push_back (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "thru", 1 }, "Input Thru",
                                                              juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f));
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
    return { p.begin(), p.end() };
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

// Stereo in. openprocess: stereo out. opensphere: Ambisonics out - 3rd order (16 channels) as an
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
    if (t == juce::AudioProcessor::wrapperType_Standalone)
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
    rebuildAll();
    startTimerHz (30);
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
    bool ok = out == juce::AudioChannelSet::stereo();
    for (int o = 1; o <= ambi::kMaxOrder; ++o)
        ok = ok || out == juce::AudioChannelSet::ambisonic (o)
                || (out == juce::AudioChannelSet::discreteChannels (ambi::chFor (o)) && wrapperType != wrapperType_AudioUnit);
    if (! ok) return false;
   #else
    if (out != juce::AudioChannelSet::stereo()) return false;
   #endif
    const auto in = l.getMainInputChannelSet();
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
    wasPlaying = false;
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
        inPeak = in.getMagnitude (0, n);
    }

    const bool playing = timelinePlayingFlag.load();
    double tlLength = 60.0, startAt = 0.0;
    SpaceParams sp = readSpace();
    sp.flock = &flock;
    {
        const juce::SpinLock::ScopedTryLockType tl (lock);
        if (tl.isLocked())
        {
            tlLength = model.minutes * 60.0;
            startAt = model.playStart * tlLength;
            if (playing && ! wasPlaying) tlTime = startAt;
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
        else if (playing && ! wasPlaying)
            tlTime = startAt;
    }
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
        const int order = juce::jmin (wanted, ambi::orderFor (nch));
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
        for (int i = 0; i < n; ++i)
            dst[i] = softClip (src[i] * master * 0.7f + (inp != nullptr ? thru * inp[i] : 0.0f));
    }
   #endif

    if (playing)
    {
        tlTime += n / hostRate;
        if (tlTime >= tlLength) tlTime = juce::jmin (startAt, tlLength - 0.01);
        tlPos = tlTime;
    }
    motionClock += n / hostRate * sp.mspeed;   // the speed setting runs the clock, so changing it never jumps
    if (motionClock > 86400.0) motionClock = 0.0;
    wasPlaying = playing;
}

//==============================================================================
Model OpProcessor::getModel() const
{
    const juce::SpinLock::ScopedLockType sl (lock);
    return model;
}

void OpProcessor::setModel (const Model& m)
{
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        model = m;
    }
    timelinePlayingFlag = m.timelinePlaying;
}

float OpProcessor::voiceFrac (int voiceId) const
{
    for (auto& p : engine.pub)
        if (p.id.load (std::memory_order_relaxed) == voiceId) return p.frac.load (std::memory_order_relaxed);
    return -1.0f;
}

int OpProcessor::addItem (int cell, float x, float y)
{
    const juce::SpinLock::ScopedLockType sl (lock);
    if ((int) model.mix.size() >= Model::kMaxItems) return -1;
    Item i;
    i.id = model.nextId++;
    i.cell = juce::jlimit (0, OP_SPATIAL ? kLiveCell : bank::kCells - 1, cell);
    i.x = juce::jlimit (0.0f, 1.0f, x);
    i.y = juce::jlimit (0.0f, 1.0f, y);
    i.d = 0.3f;
    model.mix.push_back (i);
    return i.id;
}

void OpProcessor::applyPreset (int k, bool additive)
{
    k = juce::jlimit (0, 7, k);
    const int rows[3] = { k, (k + 3) % 8, (k + 5) % 8 };
    const int count = 3 + rng.nextInt (3);
    const juce::SpinLock::ScopedLockType sl (lock);
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
}

void OpProcessor::operatorLength()
{
    const juce::SpinLock::ScopedLockType sl (lock);
    for (auto& i : model.mix)
    {
        const float len = std::exp (std::log (0.02f) * (1.0f - rng.nextFloat()));
        i.s = (1.0f - len) * rng.nextFloat();
        i.e = i.s + len;
    }
}

void OpProcessor::operatorPosition()
{
    const juce::SpinLock::ScopedLockType sl (lock);
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
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        for (auto& s : model.snaps)
            if (s.id == snapId)
            {
                model.mix = s.items;
                for (auto& i : model.mix) { i.id = model.nextId++; i.motionStart = motionNow.load(); }
                if (s.hasSpace) restore = s.space;
            }
    }
   #if OP_SPATIAL
    // its global settings come back too
    if (restore)
    {
        const float deg = 180.0f / juce::MathConstants<float>::pi;
        const std::pair<const char*, float> vals[] = { { "rolloff", restore->rolloff }, { "air", restore->air }, { "spread", restore->spread },
            { "blur", restore->blur }, { "near", restore->nearR }, { "mspeed", restore->mspeed },
            { "yaw", restore->yaw * deg }, { "pitch", restore->pitch * deg } };
        for (auto& v : vals)
            if (auto* prm = params.getParameter (v.first)) prm->setValueNotifyingHost (prm->convertTo0to1 (v.second));
    }
   #else
    juce::ignoreUnused (restore);
   #endif
}

bool OpProcessor::boidPos (int voiceId, float& x, float& y, float& d) const
{
    const juce::SpinLock::ScopedLockType fl (flockLock);
    if (auto* b = flockShown.find (voiceId)) { x = b->x; y = b->y; d = b->d; return true; }
    return false;
}

void OpProcessor::toggleTimeline()
{
    bool hasSegs;
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        hasSegs = ! model.segs.empty();
        if (hasSegs || model.timelinePlaying) model.timelinePlaying = ! model.timelinePlaying;
        timelinePlayingFlag = model.timelinePlaying;
    }
    if (! hasSegs) setStatus ("drag snapshots onto the timeline first");
}

void OpProcessor::clearMix()
{
    const juce::SpinLock::ScopedLockType sl (lock);
    model.mix.clear();
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
        {
            const juce::SpinLock::ScopedLockType sl (lock);
            for (auto it = model.mix.begin(); it != model.mix.end(); ++it)
                if (it->cell == cell) { model.mix.erase (it); removed = true; break; }
        }
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
        // openprocess 16-bit stereo, opensphere 24-bit AmbiX (ACN / SN3D) at the chosen order
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
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile (JucePlugin_Name);
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
    juce::ValueTree root (JucePlugin_Name);
    root.appendChild (params.copyState(), nullptr);
    auto m = getModel();
    m.timelinePlaying = false;
    root.appendChild (m.toTree(), nullptr);
    juce::ValueTree b ("bank");
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        for (auto& s : sources) b.appendChild (s.toTree(), nullptr);
    }
    root.appendChild (b, nullptr);
    if (auto xml = root.createXml()) copyXmlToBinary (*xml, dest);
}

void OpProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr) return;
    auto root = juce::ValueTree::fromXml (*xml);
    if (! root.hasType (JucePlugin_Name)) return;
    auto p = root.getChildWithName (params.state.getType());
    if (p.isValid()) params.replaceState (p);
    setModel (Model::fromTree (root.getChildWithName ("model")));
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
