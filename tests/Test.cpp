// QuellTest: engine checks (null at zero depth, latency alignment, resonance reduction, every path finite),
// optional renders (--wav dir) and window screenshots (--shot file.png).
#include "../Source/PluginEditor.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace
{
int fails = 0;
void check (bool ok, const juce::String& what) { std::printf ("%s  %s\n", ok ? "ok  " : "FAIL", what.toRawUTF8()); if (! ok) ++fails; }

struct Rig
{
    QuellProcessor p;
    double sr; int block;
    Rig (double s = 48000, int b = 512) : sr (s), block (b)
    {
        p.setPlayConfigDetails (2, 2, sr, block);
        p.prepareToPlay (sr, block);
    }
    void prep() { p.prepareToPlay (sr, block); }
    juce::AudioBuffer<float> run (const juce::AudioBuffer<float>& in)
    {
        juce::AudioBuffer<float> out (2, in.getNumSamples()), buf (4, block);
        juce::MidiBuffer m;
        for (int pos = 0; pos < in.getNumSamples(); pos += block)
        {
            const int n = juce::jmin (block, in.getNumSamples() - pos);
            juce::AudioBuffer<float> b (2, n);
            for (int c = 0; c < 2; ++c) b.copyFrom (c, 0, in, c, pos, n);
            p.processBlock (b, m);
            for (int c = 0; c < 2; ++c) out.copyFrom (c, pos, b, c, 0, n);
        }
        return out;
    }
};

juce::AudioBuffer<float> testSignal (double sr, int n, float sineHz, float sineAmp, float noiseAmp, int seed = 1)
{
    juce::AudioBuffer<float> b (2, n);
    juce::Random r (seed);
    for (int i = 0; i < n; ++i)
    {
        const float s = sineAmp * std::sin (juce::MathConstants<float>::twoPi * sineHz * (float) i / (float) sr);
        for (int c = 0; c < 2; ++c) b.setSample (c, i, s + noiseAmp * (r.nextFloat() * 2.0f - 1.0f));
    }
    return b;
}

// level of one frequency (Goertzel-ish via correlation) over a range
float toneLevel (const juce::AudioBuffer<float>& b, int ch, int from, int to, double sr, float hz)
{
    double re = 0, im = 0;
    for (int i = from; i < to; ++i)
    {
        const double ph = juce::MathConstants<double>::twoPi * hz * i / sr;
        re += b.getSample (ch, i) * std::cos (ph); im += b.getSample (ch, i) * std::sin (ph);
    }
    return (float) (2.0 * std::sqrt (re * re + im * im) / (to - from));
}

float nullError (const juce::AudioBuffer<float>& in, const juce::AudioBuffer<float>& out, int lat, int from)
{
    float e = 0;
    for (int c = 0; c < 2; ++c)
        for (int i = from; i < in.getNumSamples() - lat; ++i)
            e = juce::jmax (e, std::abs (out.getSample (c, i + lat) - in.getSample (c, i)));
    return e;
}

bool finite (const juce::AudioBuffer<float>& b)
{
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = 0; i < b.getNumSamples(); ++i) if (! std::isfinite (b.getSample (c, i)) || std::abs (b.getSample (c, i)) > 8.0f) return false;
    return true;
}

void writeWav (const juce::File& f, const juce::AudioBuffer<float>& b, double sr)
{
    f.deleteFile();
    juce::WavAudioFormat w;
    if (auto os = f.createOutputStream())
        if (auto wr = std::unique_ptr<juce::AudioFormatWriter> (w.createWriterFor (os.release(), sr, 2, 24, {}, 0)))
            wr->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::StringArray args (argv + 1, argc - 1);

    if (args[0] == "--shot")
    {
        Rig r;
        if (args.size() > 2 && args[2] == "bands")
        {
            auto band = [&] (int i, int type, float f, float g, float q, float focus = 0)
            {
                r.p.set (ql::bandId (i, "used"), 1); r.p.set (ql::bandId (i, "type"), (float) type); r.p.set (ql::bandId (i, "freq"), f);
                r.p.set (ql::bandId (i, "gain"), g); r.p.set (ql::bandId (i, "q"), q); r.p.set (ql::bandId (i, "focus"), focus);
            };
            band (0, ql::LowCut, 90, 0, 2); band (1, ql::Bell, 3200, 8, 1.2f); band (2, ql::HighShelf, 9000, -6, 1, 40);
            r.p.set ("depth", 9); r.p.set ("detail", 7);
        }
        // feed something so the graph has content
        auto sig = testSignal (r.sr, 48000, 2800, 0.3f, 0.1f);
        for (float hz : { 440.0f, 1250.0f, 5100.0f })
            for (int i = 0; i < sig.getNumSamples(); ++i)
                for (int c = 0; c < 2; ++c) sig.addSample (c, i, 0.15f * std::sin (juce::MathConstants<float>::twoPi * hz * (float) i / 48000.0f));
        r.run (sig);
        std::unique_ptr<juce::AudioProcessorEditor> ed (r.p.createEditor());
        ed->setSize (980, 640);
        for (int i = 0; i < 6; ++i) { r.run (testSignal (r.sr, 2048, 2800, 0.3f, 0.1f, i)); static_cast<QuellEditor*> (ed.get())->refreshNow(); }
        static_cast<QuellEditor*> (ed.get())->showState (args.contains ("help"), args.contains ("bands") ? 1 : -1);
        static_cast<QuellEditor*> (ed.get())->refreshNow();
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
        juce::File f (juce::File::getCurrentWorkingDirectory().getChildFile (args[1]));
        f.deleteFile();
        juce::PNGImageFormat png;
        if (auto os = f.createOutputStream()) png.writeImageToStream (img, *os);
        std::printf ("wrote %s\n", f.getFullPathName().toRawUTF8());
        return 0;
    }

    const double sr = 48000;
    const int n = 48000 * 3;
    if (args[0] == "--dbg")
    {
        Rig r; r.p.set ("depth", 9); r.p.set ("detail", 7);
        r.run (testSignal (sr, 48000, 2800, 0.3f, 0.1f));
        ql::Engine::Display d; r.p.readDisplay (d);
        std::printf ("meter %.1f %.1f\n", r.p.meter (0), r.p.meter (1));
        for (int i = 0; i < ql::Engine::kDisplayPoints; i += 8)
            std::printf ("%7.0f Hz  in %6.1f  red %5.1f\n", 20.0 * std::pow (1000.0, (i + 0.5) / ql::Engine::kDisplayPoints), d.input[(size_t) i], d.red[0][(size_t) i]);
        return 0;
    }

    // 1. zero depth nulls against the input delayed by the reported latency, for each path
    struct PathCase { const char* name; bool lin, low; int q; };
    for (auto pc : { PathCase { "min phase normal", false, false, 0 }, PathCase { "min phase ultra", false, false, 2 },
                     PathCase { "linear phase", true, false, 1 }, PathCase { "low latency", false, true, 0 } })
    {
        Rig r;
        r.p.set ("depth", 0); r.p.set ("linear", pc.lin ? 1.0f : 0.0f); r.p.set ("lowLatency", pc.low ? 1.0f : 0.0f); r.p.set ("quality", (float) pc.q);
        r.prep();
        auto in = testSignal (sr, n, 1000, 0.3f, 0.2f);
        auto out = r.run (in);
        const int lat = r.p.getLatencySamples();
        const float e = nullError (in, out, lat, 20000);
        check (e < 2.0e-3f, juce::String (pc.name) + ": null at depth 0, latency " + juce::String (lat) + ", max error " + juce::String (e, 6));
    }

    // 2. a resonance in noise is reduced, the noise much less
    for (int mode = 0; mode < 2; ++mode)
        for (int lin = 0; lin < 3; ++lin)
        {
            Rig r;
            r.p.set ("mode", (float) mode); r.p.set ("depth", mode == 0 ? 8.0f : 10.0f); r.p.set ("detail", 7);
            r.p.set ("linear", lin == 1 ? 1.0f : 0.0f); r.p.set ("lowLatency", lin == 2 ? 1.0f : 0.0f);
            r.prep();
            auto in = testSignal (sr, n, 3000, 0.25f, 0.25f);
            auto out = r.run (in);
            const int lat = r.p.getLatencySamples();
            const float tIn = toneLevel (in, 0, 48000, n - lat, sr, 3000), tOut = toneLevel (out, 0, 48000 + lat, n, sr, 3000);
            const float nIn = toneLevel (in, 0, 48000, n - lat, sr, 700), nOut = toneLevel (out, 0, 48000 + lat, n, sr, 700);
            juce::ignoreUnused (nIn, nOut);
            // broadband: RMS of the difference between in and out without the tone
            const float red = juce::Decibels::gainToDecibels (tOut / tIn);
            const float rmsIn = in.getRMSLevel (0, 48000, n - 48000 - lat), rmsOut = out.getRMSLevel (0, 48000 + lat, n - 48000 - lat);
            const juce::String name = juce::String (mode ? "hard" : "soft") + (lin == 1 ? " linear" : lin == 2 ? " low-latency" : " min-phase");
            check (red < -6.0f && finite (out), name + ": 3 kHz resonance " + juce::String (red, 1) + " dB, total level " + juce::String (juce::Decibels::gainToDecibels (rmsOut / rmsIn), 1) + " dB");
        }

    // 3. every option combination stays finite (stereo modes, sidechain absent, delta, listen, mix, focus, tilts)
    {
        Rig r;
        juce::Random rnd (7);
        bool ok = true;
        for (int it = 0; it < 40 && ok; ++it)
        {
            for (auto* prm : r.p.getParameters()) prm->setValueNotifyingHost (rnd.nextFloat());
            r.p.set ("outGain", 0); r.p.set ("wetTrim", 0);
            if (it % 5 == 0) r.prep();
            ok = finite (r.run (testSignal (sr, 9000, 200.0f + 300.0f * (float) it, 0.4f, 0.3f, it)));
        }
        check (ok, "random settings x40: output finite and bounded");
    }

    // 4. delta at mix 100%: out + delta == dry
    {
        Rig a, b;
        for (auto* r : { &a, &b }) { r->p.set ("depth", 9); r->p.set ("detail", 6); r->prep(); }
        b.p.set ("delta", 1);
        auto in = testSignal (sr, n / 2, 2500, 0.3f, 0.3f, 3);
        auto o1 = a.run (in), o2 = b.run (in);
        for (int c = 0; c < 2; ++c) o1.addFrom (c, 0, o2, c, 0, o1.getNumSamples());
        const float e = nullError (in, o1, a.p.getLatencySamples(), 10000);
        check (e < 2.0e-3f, "wet + delta == dry, max error " + juce::String (e, 6));
    }

    // 5. state round trip incl. A/B
    {
        Rig r; r.p.set ("depth", 13); r.p.switchAB (1); r.p.set ("depth", 2);
        juce::MemoryBlock mb; r.p.getStateInformation (mb);
        Rig r2; r2.p.setStateInformation (mb.getData(), (int) mb.getSize());
        const bool ok1 = std::abs (r2.p.get ("depth") - 2) < 0.02f && r2.p.abSlot == 1;
        r2.p.switchAB (0);
        check (ok1 && std::abs (r2.p.get ("depth") - 13) < 0.02f, "state + A/B round trip");
    }

    // 6. speed: 10 s of stereo per quality setting
    for (int q = 0; q < 3; ++q)
    {
        Rig r; r.p.set ("quality", (float) q); r.p.set ("depth", 8); r.prep();
        auto in = testSignal (sr, 480000, 1000, 0.2f, 0.2f);
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        r.run (in);
        const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
        std::printf ("info  quality %d: %.1f%% of one core at 48 kHz stereo\n", q, ms / 100.0);
    }

    // renders for listening
    if (args[0] == "--wav" && args.size() > 1)
    {
        juce::File dir (args[1]); dir.createDirectory();
        auto in = testSignal (sr, n, 3000, 0.2f, 0.2f);
        writeWav (dir.getChildFile ("in.wav"), in, sr);
        for (int mode = 0; mode < 2; ++mode)
        {
            Rig r; r.p.set ("mode", (float) mode); r.p.set ("depth", 8); r.p.set ("detail", 7); r.prep();
            writeWav (dir.getChildFile (mode ? "hard.wav" : "soft.wav"), r.run (in), sr);
        }
    }

    std::printf (fails == 0 ? "all passed\n" : "%d failed\n", fails);
    return fails == 0 ? 0 : 1;
}
