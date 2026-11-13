// Offline checks: bank, engine, processor state, plus screenshots and audio renders.
//   OpTest                      run the checks
//   OpTest --render <dir>       also write example WAVs
//   OpTest --screenshots <dir>  also write PNGs of the editor
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <functional>

static double chRms (const juce::AudioBuffer<float>& b, int ch)
{
    double e = 0;
    for (int i = 0; i < b.getNumSamples(); ++i) e += (double) b.getSample (ch, i) * b.getSample (ch, i);
    return std::sqrt (e / juce::jmax (1, b.getNumSamples()));
}

static int failures = 0;
#define CHECK(cond, msg) do { if (! (cond)) { ++failures; std::printf ("FAIL: %s\n", msg); } else std::printf ("ok:   %s\n", msg); } while (0)

static bool finiteBuf (const juce::AudioBuffer<float>& b, float& peak, double& rms)
{
    peak = 0; rms = 0;
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const float v = b.getSample (ch, i);
            if (! std::isfinite (v)) return false;
            peak = juce::jmax (peak, std::abs (v));
            rms += v * v;
        }
    rms = std::sqrt (rms / juce::jmax (1, b.getNumSamples() * b.getNumChannels()));
    return true;
}

static void writeWav (const juce::File& f, const juce::AudioBuffer<float>& b, double rate)
{
    f.deleteFile();
    std::unique_ptr<juce::OutputStream> os = std::make_unique<juce::FileOutputStream> (f);
    juce::WavAudioFormat wav;
    if (auto w = wav.createWriterFor (os, juce::AudioFormatWriterOptions().withSampleRate (rate).withNumChannels (2).withBitsPerSample (16)))
        w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}

static void waitIdle (OpProcessor& p)
{
    for (int i = 0; i < 600 && p.busy(); ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
}

static juce::AudioBuffer<float> runProcessor (OpProcessor& p, double seconds, double rate = 48000.0,
                                              std::function<float (int, int)> input = nullptr)
{
    const int block = 480;
    const int total = (int) (seconds * rate);
    const int nout = p.getTotalNumOutputChannels(), nio = juce::jmax (nout, p.getTotalNumInputChannels());
    juce::AudioBuffer<float> out (nout, total), io (nio, block);
    juce::MidiBuffer midi;
    for (int pos = 0; pos < total; pos += block)
    {
        io.clear();
        if (input != nullptr)
            for (int ch = 0; ch < p.getTotalNumInputChannels(); ++ch)
                for (int i = 0; i < block; ++i) io.setSample (ch, i, input (ch, pos + i));
        p.processBlock (io, midi);
        const int n = juce::jmin (block, total - pos);
        for (int ch = 0; ch < nout; ++ch) out.copyFrom (ch, pos, io, ch, 0, n);
        if ((pos / block) % 20 == 0) juce::MessageManager::getInstance()->runDispatchLoopUntil (1);
    }
    return out;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    juce::StringArray args;
    for (int i = 1; i < argc; ++i) args.add (argv[i]);
    juce::File renderDir, shotDir;
    if (args.indexOf ("--render") >= 0) renderDir = juce::File::getCurrentWorkingDirectory().getChildFile (args[args.indexOf ("--render") + 1]);
    if (args.indexOf ("--screenshots") >= 0) shotDir = juce::File::getCurrentWorkingDirectory().getChildFile (args[args.indexOf ("--screenshots") + 1]);
    if (renderDir != juce::File()) renderDir.createDirectory();
    if (shotDir != juce::File()) shotDir.createDirectory();

    // 1. bank
    {
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        CellArray cells;
        bool allOk = true;
        for (int c = 0; c < bank::kCells; ++c)
        {
            cells[(size_t) c] = bank::makeBuiltin (c, 0);
            float pk; double rms;
            if (cells[(size_t) c] == nullptr || ! finiteBuf (cells[(size_t) c]->audio, pk, rms) || pk < 0.01f || pk > 1.0f)
            {
                allOk = false;
                std::printf ("  cell %d bad (peak %f)\n", c, cells[(size_t) c] ? pk : -1.0f);
            }
        }
        std::printf ("  bank built in %.0f ms\n", juce::Time::getMillisecondCounterHiRes() - t0);
        CHECK (allOk, "all 64 built-in loops are finite and audible");

        auto a = bank::makeBuiltin (5, 0), b = bank::makeBuiltin (5, 0), c = bank::makeBuiltin (5, 9);
        bool same = a->audio.getNumSamples() == b->audio.getNumSamples();
        for (int i = 0; same && i < a->audio.getNumSamples(); i += 97) same = a->audio.getSample (0, i) == b->audio.getSample (0, i);
        CHECK (same, "built-in loops are deterministic");
        bool differs = false;
        for (int i = 0; i < juce::jmin (a->audio.getNumSamples(), c->audio.getNumSamples()); i += 97) differs |= a->audio.getSample (0, i) != c->audio.getSample (0, i);
        CHECK (differs, "a new seed re-skips the loop");

        if (renderDir != juce::File())
            for (int c2 = 0; c2 < bank::kCells; c2 += 9)
                writeWav (renderDir.getChildFile ("cell" + juce::String (c2).paddedLeft ('0', 2) + "-" + bank::familyName (c2 / 8) + ".wav"),
                          cells[(size_t) c2]->audio, bank::kRate);

        // 2. engine: a mix and a timeline
        Model m;
        const int picks[] = { 2, 13, 27, 36, 50, 61 };
        for (int k = 0; k < 6; ++k)
        {
            Item i; i.id = m.nextId++; i.cell = picks[k]; i.x = k / 5.0f; i.y = 0.6f + 0.06f * k;
            if (k == 3) { i.s = 0.2f; i.e = 0.24f; }
            if (k == 4) i.effect = fx::FadeIn;
            m.mix.push_back (i);
        }
        Engine e;
        e.prepare (48000);
        std::vector<VoiceSpec> vs (Engine::kMaxSpecs);
        juce::AudioBuffer<float> out (2, 48000 * 12);
        out.clear();
        for (int pos = 0; pos < out.getNumSamples(); pos += 256)
        {
            const int ns = Engine::buildMix (m, cells, pos / 48000.0, vs.data(), (int) vs.size());
            e.process (out.getWritePointer (0, pos), out.getWritePointer (1, pos), juce::jmin (256, out.getNumSamples() - pos), vs.data(), ns);
        }
        float pk; double rms;
        CHECK (finiteBuf (out, pk, rms) && rms > 0.02, "mix renders sound");
        std::printf ("  mix peak %.3f rms %.3f\n", pk, rms);
        if (renderDir != juce::File()) writeWav (renderDir.getChildFile ("mix.wav"), out, 48000);

        // solo silences the others
        Model solo = m;
        solo.mix[1].effect = fx::Solo;
        const int ns = Engine::buildMix (solo, cells, 0.0, vs.data(), (int) vs.size());
        int audible = 0;
        for (int k = 0; k < ns; ++k) audible += vs[(size_t) k].gain > 0;
        CHECK (audible == 1, "solo leaves one sound audible");

        // timeline
        Model tm = m;
        Snapshot s1; s1.id = tm.nextId++; s1.items = m.mix;
        Snapshot s2; s2.id = tm.nextId++; s2.items = { m.mix[0], m.mix[3] };
        tm.minutes = 1.0f;
        Segment g1; g1.id = tm.nextId++; g1.start = 0.0f; g1.len = 0.1f; g1.snap = s1;
        Segment g2; g2.id = tm.nextId++; g2.start = 0.1f; g2.len = 0.1f; g2.snap = s2;
        tm.segs = { g1, g2 };
        CHECK (Engine::buildTimeline (tm, cells, 3.0, vs.data(), 256) == 6, "timeline plays segment 1 at 3 s");
        CHECK (Engine::buildTimeline (tm, cells, 8.0, vs.data(), 256) == 2, "timeline plays segment 2 at 8 s");
        CHECK (Engine::buildTimeline (tm, cells, 30.0, vs.data(), 256) == 0, "timeline is silent at 30 s");

        // a 30 minute piece: a segment near the end still plays
        {
            Model lm = tm;
            lm.minutes = 30.0f;
            lm.segs[1].start = 29.0f / 30.0f; lm.segs[1].len = 0.5f / 30.0f;
            const auto lb = Model::fromTree (lm.toTree());
            CHECK (std::abs (lb.minutes - 30.0f) < 1e-4f, "30 minutes survive save / load");
            CHECK (Engine::buildTimeline (lb, cells, 29.0 * 60 + 10, vs.data(), 256) == 2, "timeline plays a segment at 29:10");
        }

       #if OP_SPATIAL
        // Ambisonics: one sound to the left, one overhead, encoded at 3rd order
        {
            auto encode = [&] (float x, float y, float d, int order, int nch) {
                Model sm;
                Item i; i.id = 1; i.cell = 9; i.x = x; i.y = y; i.d = d;
                sm.mix.push_back (i);
                Engine se;
                se.prepare (48000);
                juce::AudioBuffer<float> ab (nch, 48000 * 2);
                ab.clear();
                for (int pos = 0; pos < ab.getNumSamples(); pos += 256)
                {
                    const int n = juce::jmin (256, ab.getNumSamples() - pos);
                    const int nsp = Engine::buildMix (sm, cells, pos / 48000.0, vs.data(), (int) vs.size());
                    float* ptrs[ambi::kCh];
                    for (int c = 0; c < nch; ++c) ptrs[c] = ab.getWritePointer (c, pos);
                    se.process (Engine::Output { ptrs, nch, order }, n, vs.data(), nsp);
                }
                return ab;
            };
            const auto left = encode (0.25f, space::kHorizonY, 0.9f, 3, 16);   // far: nearly a point
            const double W = chRms (left, 0), Y = chRms (left, 1), Z = chRms (left, 2), X = chRms (left, 3);
            std::printf ("  left: W %.3f Y %.3f Z %.3f X %.3f\n", W, Y, Z, X);
            CHECK (W > 0.005 && Y > 0.85 * W && Z < 0.05 * W && X < 0.2 * W, "a sound on the left encodes to +Y");
            const auto up = encode (0.5f, 1.0f, 0.9f, 3, 16);
            const double W2 = chRms (up, 0), Z2 = chRms (up, 2), Y2 = chRms (up, 1);
            CHECK (Z2 > 0.9 * W2 && Y2 < 0.1 * W2, "a sound overhead encodes to +Z");
            const auto hi = encode (0.4f, 0.5f, 0.5f, 7, 64);
            double hiE = 0; for (int c = 49; c < 64; ++c) hiE += chRms (hi, c);
            CHECK (hiE > 0.001, "7th order fills all 64 channels");
            const auto lo = encode (0.4f, 0.5f, 0.5f, 1, 16);
            double loE = 0; for (int c = 4; c < 16; ++c) loE += chRms (lo, c);
            CHECK (loE == 0.0, "1st order leaves the higher channels silent");
            const auto nearS = encode (0.25f, space::kHorizonY, 0.05f, 3, 16), farS = encode (0.25f, space::kHorizonY, 0.95f, 3, 16);
            CHECK (chRms (nearS, 0) > 4.0 * chRms (farS, 0), "near sounds are louder than far ones");
            if (renderDir != juce::File())
            {
                juce::AudioBuffer<float> st (2, left.getNumSamples());
                for (int i = 0; i < st.getNumSamples(); ++i)
                {
                    st.setSample (0, i, 0.7f * (left.getSample (0, i) + left.getSample (1, i)));
                    st.setSample (1, i, 0.7f * (left.getSample (0, i) - left.getSample (1, i)));
                }
                writeWav (renderDir.getChildFile ("sphere-left-preview.wav"), st, 48000);
            }
            // motions move the sound
            Item o; o.id = 3; o.x = 0.5f; o.y = 0.5f; o.motion = motion::Orbit;
            float x0, y0, x1, y1;
            space::moved (o, 0.0, x0, y0);
            space::moved (o, 6.0, x1, y1);
            CHECK (std::abs (x1 - x0) > 0.2f, "orbit turns the sound round you");
        }
       #endif

        // 3. model round trip
        const auto back = Model::fromTree (tm.toTree());
        CHECK (back.segs.size() == 2 && back.segs[1].snap.items.size() == 2 && back.mix.size() == 6
               && std::abs (back.mix[3].e - 0.24f) < 1e-6f && back.mix[4].effect == fx::FadeIn, "model survives save / load");

        // 4. CD-skip on a sine: output length and finiteness
        juce::AudioBuffer<float> sine (2, 44100 * 4);
        for (int i = 0; i < sine.getNumSamples(); ++i) { const float v = std::sin (i * 0.05f) * 0.5f; sine.setSample (0, i, v); sine.setSample (1, i, v); }
        juce::AudioBuffer<float> sk;
        bank::cdSkip (sine, sk, 44100, 2.0, 0.9f, 3);
        CHECK (sk.getNumSamples() == 88200 && finiteBuf (sk, pk, rms) && rms > 0.05, "cdSkip makes a full loop");
    }

    // 5. the processor
    {
        OpProcessor p;
        waitIdle (p);
       #if OP_SPATIAL
        {
            juce::AudioProcessor::BusesLayout l;
            l.inputBuses.add (juce::AudioChannelSet::stereo());
            l.outputBuses.add (juce::AudioChannelSet::ambisonic (3));
            CHECK (p.setBusesLayout (l), "3rd order (16 channel) output accepted");
            l.outputBuses.getReference (0) = juce::AudioChannelSet::ambisonic (7);
            CHECK (p.checkBusesLayoutSupported (l), "7th order (64 channel) output accepted");
        }
       #endif
        p.prepareToPlay (48000, 480);
        bool ready = true;
        for (int c = 0; c < bank::kCells; ++c) ready &= p.cellReady (c);
        CHECK (ready, "processor built the bank");
        p.applyPreset (0, false);
        CHECK ((int) p.getModel().mix.size() >= 3, "preset fills the play area");
        auto out = runProcessor (p, 4.0);
        float pk; double rms;
        CHECK (finiteBuf (out, pk, rms) && rms > (OP_SPATIAL ? 0.002 : 0.01) && pk <= 1.0f, "processor output is audible and bounded");
       #if OP_SPATIAL
        CHECK (out.getNumChannels() == 16 && p.ambisonicOrder() == 3, "processor writes 16 channels at 3rd order");
        {
            // the live input, placed in the space
            Model keep = p.getModel();
            p.edit ([] (Model& m) { m.mix.clear(); });
            p.addItem (kLiveCell, 0.75f, space::kHorizonY);
            auto lv = runProcessor (p, 1.0, 48000.0, [] (int, int i) { return 0.3f * std::sin (i * 0.03f); });
            const double lw = chRms (lv, 0), ly = chRms (lv, 1);
            std::printf ("  live input: W %.4f Y %.4f\n", lw, ly);
            CHECK (lw > 0.01 && ly > 0.5 * lw, "live input is encoded (to the right: Y has energy)");
            p.setModel (keep);
        }
       #endif
        p.operatorLength();
        p.operatorPosition();
        const int snap = p.takeSnapshot();
        CHECK (snap > 0, "snapshot taken");
        p.edit ([&] (Model& m) { Segment s; s.id = m.nextId++; s.start = 0; s.len = 0.2f; s.snap = m.snaps.back(); m.segs.push_back (s); });
        p.toggleTimeline();
        CHECK (p.timelinePlaying(), "timeline plays");
        out = runProcessor (p, 3.0);
        CHECK (finiteBuf (out, pk, rms) && rms > 0.005, "timeline output is audible");
        CHECK (p.timelineSeconds() > 2.5 && p.timelineSeconds() < 3.5, "timeline clock advances");
        if (renderDir != juce::File()) writeWav (renderDir.getChildFile ("timeline.wav"), out, 48000);
        p.toggleTimeline();

        juce::MemoryBlock state;
        p.getStateInformation (state);
        OpProcessor q;
        q.setStateInformation (state.getData(), (int) state.getSize());
        waitIdle (q);
        CHECK (q.getModel().mix.size() == p.getModel().mix.size() && q.getModel().segs.size() == 1, "processor state restores");

        if (renderDir != juce::File())
        {
            p.renderToFile (renderDir.getChildFile ("render.wav"));
            waitIdle (p);
            CHECK (renderDir.getChildFile ("render.wav").getSize() > 100000, "render to file writes a WAV");
            juce::WavAudioFormat wf;
            if (auto rd = std::unique_ptr<juce::AudioFormatReader> (wf.createReaderFor (new juce::FileInputStream (renderDir.getChildFile ("render.wav")), true)))
                CHECK ((int) rd->numChannels == (OP_SPATIAL ? 16 : 2), "render has the right channel count");
        }

        if (shotDir != juce::File())
        {
            auto shot = [&] (juce::Component& c, const juce::String& name) {
                juce::PNGImageFormat png;
                auto img = c.createComponentSnapshot (c.getLocalBounds(), false, 2.0f);
                juce::FileOutputStream os (shotDir.getChildFile (name + ".png"));
                os.setPosition (0); os.truncate();
                png.writeImageToStream (img, os);
            };
            p.edit ([] (Model& m) { m.mix.clear(); });
            p.applyPreset (2, false);
            p.applyPreset (5, true);
            p.takeSnapshot();
            p.applyPreset (4, false);
            p.takeSnapshot();
            p.edit ([] (Model& m) {
                m.mix.front().effect = fx::FadeInOut;
                if (m.mix.size() > 2) m.mix[2].effect = fx::Mute;
                Segment s; s.id = m.nextId++; s.start = 0.35f; s.len = 0.25f; s.snap = m.snaps.back(); m.segs.push_back (s);
                m.minutes = 2.5f;
               #if OP_SPATIAL
                if (m.mix.size() > 1) m.mix[1].motion = motion::Orbit;
                if (m.mix.size() > 3) m.mix[3].motion = motion::Drift;
               #endif
            });
           #if OP_SPATIAL
            p.addItem (kLiveCell, 0.62f, 0.8f);
           #endif
            runProcessor (p, 0.7);
            std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
            auto* oe = dynamic_cast<OpEditor*> (ed.get());
            for (int i = 0; i < 12; ++i) oe->tick();
            oe->selectItem (p.getModel().mix.front().id);
            oe->tick();
            shot (*ed, "main");
            oe->setHelpPage (0); oe->tick();
            shot (*ed, "guide");
            oe->setHelpPage (2); oe->tick();
            shot (*ed, "help");
            oe->setHelpPage (-1);
            ed->setSize (700, 504);
            oe->tick();
            shot (*ed, "small");
            ed->setSize (OpEditor::W, OpEditor::H);
            // a 30 minute piece, zoomed in on its middle
            p.edit ([] (Model& m) {
                m.minutes = 30.0f;
                m.segs.clear();
                for (int k = 0; k < 40; ++k)
                {
                    Segment s; s.id = m.nextId++; s.start = k / 40.0f + 0.004f; s.len = 0.012f + 0.01f * (k % 3);
                    s.snap = m.snaps[(size_t) (k % (int) m.snaps.size())]; s.snap.colour = k % 6; m.segs.push_back (s);
                }
            });
            oe->setTimelineView (0.0f, 1.0f); oe->tick();
            shot (*ed, "timeline30");
            oe->setTimelineView (0.30f, 0.06f); oe->tick();
            shot (*ed, "timeline30zoom");
        }
    }

    std::printf (failures == 0 ? "\nall checks passed\n" : "\n%d check(s) failed\n", failures);
    return failures == 0 ? 0 : 1;
}
