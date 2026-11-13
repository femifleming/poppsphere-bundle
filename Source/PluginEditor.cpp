// Quell: the window. Plain and dark: toolbar, the four main controls, a reduction graph with the
// depth curve and its bands, the selected band's controls, a collapsible side panel and a footer.
#include "PluginEditor.h"

using namespace ql;

namespace
{
const juce::Colour cBg (0xff121214), cPanel (0xff18181b), cFg (0xffe9e7e1), cDim (0xff8d8d90), cLine (0xff2b2b2e),
    cTrack (0xff333337), cHot (0xffff6a4d), cCh1 (0xff6d6d73), cShade (0xff232326);

juce::Font fnt (float h, bool bold = false)
{
   #if JUCE_MAC
    const juce::String face ("Helvetica Neue");
   #elif JUCE_WINDOWS
    const juce::String face ("Segoe UI");
   #else
    const juce::String face (juce::Font::getDefaultSansSerifFontName());
   #endif
    return juce::Font (juce::FontOptions (face, h, bold ? juce::Font::bold : juce::Font::plain));
}
juce::String U (const char* s) { return juce::String (juce::CharPointer_UTF8 (s)); }
const char* kDown = " \xe2\x96\xbe";

juce::String hzStr (float f) { return f >= 1000.0f ? juce::String (f / 1000.0f, f >= 10000.0f ? 1 : 2) + " kHz" : juce::String (juce::roundToInt (f)) + " Hz"; }

float parseHz (juce::String t)
{
    t = t.trim().toLowerCase().replace (",", ".").removeCharacters (" hz");
    const int k = t.indexOfChar ('k');
    if (k >= 0)
    {
        const auto a = t.substring (0, k), b = t.substring (k + 1);
        if (b.isNotEmpty() && b.containsOnly ("0123456789")) return (a.getFloatValue() + b.getFloatValue() / std::pow (10.0f, (float) b.length())) * 1000.0f;
        return a.getFloatValue() * 1000.0f;
    }
    return t.getFloatValue();
}

juce::String fmtValue (const juce::String& id, float v)
{
    if (id == "depth" || id == "detail" || id == "attack" || id == "release") return juce::String (v, 1);
    if (id.endsWith ("_freq")) return hzStr (v);
    if (id.endsWith ("_q")) return juce::String (v, 2);
    if (id.endsWith ("_gain") || id == "wetTrim" || id == "outGain") return (v > 0.05f ? "+" : "") + juce::String (std::abs (v) < 0.05f ? 0.0f : v, 1) + " dB";
    if (id == "maxCut") return v >= 39.95f ? juce::String ("off") : juce::String (v, 1) + " dB";
    if (id.endsWith ("focus") || id.contains ("Tilt")) return (v > 0.05f ? "+" : "") + juce::String (juce::roundToInt (v)) + "%";
    if (id == "link" || id == "mix") return juce::String (juce::roundToInt (v)) + "%";
    return juce::String (v, 2);
}

//==================================================================================================
// a parameter bound control: label, thin bar, value. Drag (shift = fine), alt-click reset, double-click types.
struct Ctl : juce::Component, juce::SettableTooltipClient
{
    QuellProcessor& p; juce::String id, label; float fs; int labelW, valueW; bool big = false;
    std::function<juce::String (float)> fmt;
    float startV = 0; bool dragging = false; float shown = -1.0e9f;
    std::unique_ptr<juce::TextEditor> ed;

    Ctl (QuellProcessor& pr, juce::String i, juce::String l, float fontSize = 11.0f, int lw = 62, int vw = 58)
        : p (pr), id (std::move (i)), label (std::move (l)), fs (fontSize), labelW (lw), valueW (vw) {}

    juce::RangedAudioParameter* par() const { return p.param (id); }
    float value() const { return p.get (id); }
    void refresh() { if (value() != shown) repaint(); }

    void paint (juce::Graphics& g) override
    {
        auto* rp = par(); if (rp == nullptr) return;
        const float v = value(); shown = v;
        const auto range = rp->getNormalisableRange();
        const int w = getWidth(), h = getHeight();
        g.setFont (fnt (fs)); g.setColour (cDim);
        if (big)
        {
            g.drawText (label, 0, 0, w, (int) (fs + 4), juce::Justification::centredLeft);
            g.setColour (cFg); g.setFont (fnt (fs * 2.0f));
            g.drawText (fmt ? fmt (v) : fmtValue (id, v), 0, (int) (fs + 2), w, (int) (fs * 2.2f), juce::Justification::centredLeft);
            const int by = h - 6;
            g.setColour (cTrack); g.fillRect (0, by, w, 3);
            const float n = range.convertTo0to1 (v);
            g.setColour (cFg); g.fillRect (0.0f, (float) by, n * (float) w, 3.0f);
            g.fillRect (n * (float) w - 1.0f, (float) by - 4.0f, 2.0f, 11.0f);
            return;
        }
        g.drawText (label, 0, 0, labelW, h, juce::Justification::centredLeft);
        const int bx = labelW, bw = juce::jmax (4, w - labelW - valueW - 6), cy = h / 2;
        g.setColour (cTrack); g.fillRect (bx, cy - 1, bw, 3);
        const float n = range.convertTo0to1 (v), zero = range.start < 0 ? range.convertTo0to1 (0.0f) : 0.0f;
        g.setColour (cFg);
        const float x0 = (float) bx + juce::jmin (n, zero) * (float) bw, x1 = (float) bx + juce::jmax (n, zero) * (float) bw;
        g.fillRect (x0, (float) cy - 1.0f, x1 - x0, 3.0f);
        g.fillRect ((float) bx + n * (float) bw - 1.0f, (float) cy - 5.0f, 2.0f, 11.0f);
        g.drawText (fmt ? fmt (v) : fmtValue (id, v), w - valueW, 0, valueW, h, juce::Justification::centredRight);
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        auto* rp = par(); if (rp == nullptr) return;
        p.undo.beginNewTransaction();
        if (e.mods.isAltDown()) { rp->beginChangeGesture(); rp->setValueNotifyingHost (rp->getDefaultValue()); rp->endChangeGesture(); repaint(); return; }
        startV = rp->getValue(); dragging = true; rp->beginChangeGesture();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragging) return;
        const float span = big ? 260.0f : (float) juce::jmax (60, getWidth() - labelW - valueW - 6);
        const float d = ((float) e.getDistanceFromDragStartX() - (float) e.getDistanceFromDragStartY()) / span * (e.mods.isShiftDown() ? 0.1f : 1.0f);
        par()->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, startV + d));
        repaint();
    }
    void mouseUp (const juce::MouseEvent&) override { if (dragging) { dragging = false; par()->endChangeGesture(); } }
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        auto* rp = par(); if (rp == nullptr) return;
        p.undo.beginNewTransaction();
        rp->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, rp->getValue() + w.deltaY * (e.mods.isShiftDown() ? 0.01f : 0.06f)));
        repaint();
    }
    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        ed = std::make_unique<juce::TextEditor>();
        addAndMakeVisible (*ed);
        ed->setBounds (big ? juce::Rectangle<int> (0, (int) fs + 2, getWidth(), (int) (fs * 2.2f)) : juce::Rectangle<int> (getWidth() - valueW, 0, valueW, getHeight()));
        ed->setFont (fnt (big ? fs * 1.6f : fs));
        ed->setColour (juce::TextEditor::backgroundColourId, cBg); ed->setColour (juce::TextEditor::textColourId, cFg);
        ed->setColour (juce::TextEditor::outlineColourId, cDim);
        ed->setText (fmtValue (id, value()).upToFirstOccurrenceOf (" ", false, false).trimCharactersAtStart ("+"), false);
        ed->selectAll(); ed->grabKeyboardFocus();
        auto done = [this]
        {
            if (ed == nullptr) return;
            auto t = ed->getText();
            float v = id.endsWith ("_freq") ? parseHz (t) : t.retainCharacters ("-0123456789.,").replace (",", ".").getFloatValue();
            if (t.trim().isNotEmpty() && par() != nullptr)
            {
                p.undo.beginNewTransaction();
                auto* rp = par();
                rp->beginChangeGesture(); rp->setValueNotifyingHost (rp->convertTo0to1 (v)); rp->endChangeGesture();
            }
            juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<Ctl> (this)] { if (sp != nullptr) { sp->ed.reset(); sp->repaint(); } });
        };
        ed->onReturnKey = done; ed->onFocusLost = done;
        ed->onEscapeKey = [this] { juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<Ctl> (this)] { if (sp != nullptr) sp->ed.reset(); }); };
    }
};

// text button: plain label, lit when on
struct Btn : juce::Component, juce::SettableTooltipClient
{
    juce::String text; std::function<bool()> lit; std::function<void (const juce::MouseEvent&)> click; float fs = 11.0f; bool frame = true;
    Btn (juce::String t, std::function<bool()> l, std::function<void (const juce::MouseEvent&)> c) : text (std::move (t)), lit (std::move (l)), click (std::move (c)) {}
    bool shown = false;
    void refresh() { if (lit && lit() != shown) repaint(); }
    void paint (juce::Graphics& g) override
    {
        const bool on = lit && lit(); shown = on;
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        if (on) { g.setColour (cFg); g.fillRoundedRectangle (r, 3.0f); }
        else if (frame) { g.setColour (cTrack); g.drawRoundedRectangle (r, 3.0f, 1.0f); }
        g.setColour (on ? cBg : (isMouseOver() ? cFg : cDim));
        g.setFont (fnt (fs));
        g.drawText (text, getLocalBounds(), juce::Justification::centred);
    }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void mouseDown (const juce::MouseEvent& e) override { if (click) click (e); repaint(); }
};

// bool parameter toggled by a Btn
std::unique_ptr<Btn> toggle (QuellProcessor& p, const juce::String& id, const juce::String& text)
{
    return std::make_unique<Btn> (text, [&p, id] { return p.get (id) > 0.5f; },
        [&p, id] (const juce::MouseEvent&) { p.undo.beginNewTransaction(); if (auto* rp = p.param (id)) { rp->beginChangeGesture(); rp->setValueNotifyingHost (p.get (id) > 0.5f ? 0.0f : 1.0f); rp->endChangeGesture(); } });
}
std::unique_ptr<Btn> choice (QuellProcessor& p, const juce::String& id, int value, const juce::String& text)
{
    return std::make_unique<Btn> (text, [&p, id, value] { return (int) p.get (id) == value; },
        [&p, id, value] (const juce::MouseEvent&) { p.undo.beginNewTransaction(); if (auto* rp = p.param (id)) { rp->beginChangeGesture(); rp->setValueNotifyingHost (rp->convertTo0to1 ((float) value)); rp->endChangeGesture(); } });
}

//==================================================================================================
// the reduction graph with the depth curve and the band nodes
struct Graph : juce::Component, juce::SettableTooltipClient
{
    QuellProcessor& p;
    Engine::Display disp; std::array<float, Engine::kDisplayPoints> peak[2] {};
    std::set<int> selected; int focusBand = -1; int hover = -1;
    std::function<void()> onSelect;
    float range = 24.0f; // dB shown below the top line
    // drag state
    enum Drag { None, Node, Box } drag = None;
    juce::Point<float> boxA, boxB; int dragBand = -1; bool altListen = false;
    struct Start { float f, g, q; }; std::map<int, Start> starts;
    bool showFreq = true; juce::Point<float> mouse { -1, -1 };

    explicit Graph (QuellProcessor& pr) : p (pr) { setWantsKeyboardFocus (true); }

    juce::Rectangle<float> plot() const { return getLocalBounds().toFloat().reduced (8.0f, 6.0f).withTrimmedBottom (14.0f).withTrimmedLeft (24.0f); }
    float xOf (float f) const { auto r = plot(); return r.getX() + r.getWidth() * std::log (f / 20.0f) / std::log (1000.0f); }
    float fOf (float x) const { auto r = plot(); return juce::jlimit (20.0f, 20000.0f, 20.0f * std::pow (1000.0f, (x - r.getX()) / r.getWidth())); }
    // depth curve area: centre line at 45% of the height, +-24 dB
    float curveMid() const { auto r = plot(); return r.getY() + r.getHeight() * 0.45f; }
    float curveScale() const { return plot().getHeight() * 0.4f / 24.0f; }
    float yOfGain (float db) const { return curveMid() - db * curveScale(); }
    float gainOfY (float y) const { return juce::jlimit (-24.0f, 24.0f, (curveMid() - y) / curveScale()); }
    float yOfRed (float db) const { auto r = plot(); return r.getY() + r.getHeight() * juce::jlimit (0.0f, 1.0f, db / range); }

    Band band (int i) const
    {
        Band b;
        b.used = p.get (bandId (i, "used")) > 0.5f; b.on = p.get (bandId (i, "on")) > 0.5f; b.type = (int) p.get (bandId (i, "type"));
        b.freq = p.get (bandId (i, "freq")); b.gain = p.get (bandId (i, "gain")); b.q = p.get (bandId (i, "q")); b.focus = p.get (bandId (i, "focus")) / 100.0f;
        return b;
    }
    juce::Point<float> nodePos (int i) const
    {
        const auto b = band (i);
        float y = curveMid();
        if (typeHasGain (b.type) && b.type != Tilt) { CurvePoint cp; bandAt (b, b.freq, cp); y = yOfGain (cp.db); }
        return { xOf (b.freq), y };
    }
    int nodeAt (juce::Point<float> pt) const
    {
        for (int i = kBands; --i >= 0;)
            if (band (i).used && nodePos (i).getDistanceFrom (pt) < 9.0f) return i;
        return -1;
    }
    void setP (int i, const char* w, float v) { if (auto* rp = p.param (bandId (i, w))) rp->setValueNotifyingHost (rp->convertTo0to1 (v)); }
    void gesture (int i, bool begin)
    {
        for (auto w : { "freq", "gain", "q", "focus" })
            if (auto* rp = p.param (bandId (i, w))) { if (begin) rp->beginChangeGesture(); else rp->endChangeGesture(); }
    }

    void update()
    {
        if (p.readDisplay (disp))
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < Engine::kDisplayPoints; ++i)
                    peak[c][(size_t) i] = juce::jmax (disp.red[c][(size_t) i], peak[c][(size_t) i] - 0.6f);
        else
            for (int c = 0; c < 2; ++c)
                for (auto& v : peak[c]) v = juce::jmax (0.0f, v - 0.3f);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto r = plot();
        g.setColour (cPanel); g.fillRect (getLocalBounds());
        // grid
        g.setFont (fnt (10.0f));
        for (float f : { 20.f, 50.f, 100.f, 200.f, 500.f, 1000.f, 2000.f, 5000.f, 10000.f, 20000.f })
        {
            const float x = xOf (f);
            g.setColour (cLine); g.drawVerticalLine ((int) x, r.getY(), r.getBottom());
            g.setColour (cDim);
            g.drawText (f >= 1000 ? juce::String ((int) (f / 1000)) + "k" : juce::String ((int) f), (int) x - 20, (int) r.getBottom() + 1, 40, 12, juce::Justification::centred);
        }
        for (float db = 6; db < range; db += 6)
        {
            const float y = yOfRed (db);
            g.setColour (cLine.withAlpha (0.6f)); g.drawHorizontalLine ((int) y, r.getX(), r.getRight());
            g.setColour (cDim); g.drawText ("-" + juce::String ((int) db), (int) r.getX() - 26, (int) y - 6, 22, 12, juce::Justification::centredRight);
        }
        // max cut shading
        const float maxCut = p.get ("maxCut");
        if (maxCut < range)
        {
            g.setColour (cShade); g.fillRect (juce::Rectangle<float>::leftTopRightBottom (r.getX(), yOfRed (maxCut), r.getRight(), r.getBottom()));
            g.setColour (cDim); g.drawText ("max cut", (int) r.getRight() - 60, (int) yOfRed (maxCut) + 2, 56, 12, juce::Justification::centredRight);
        }
        // input level (faint) for orientation
        {
            juce::Path in;
            for (int i = 0; i < Engine::kDisplayPoints; ++i)
            {
                const float x = r.getX() + r.getWidth() * ((float) i + 0.5f) / Engine::kDisplayPoints;
                const float y = r.getBottom() - r.getHeight() * juce::jlimit (0.0f, 1.0f, (disp.input[(size_t) i] + 100.0f) / 100.0f);
                if (i == 0) in.startNewSubPath (x, y); else in.lineTo (x, y);
            }
            g.setColour (cDim.withAlpha (0.25f)); g.strokePath (in, juce::PathStrokeType (1.0f));
        }
        // reduction: second channel (R/S) lighter, drawn first; first channel (L/M) on top
        const bool two = p.numMainChannels() == 2;
        for (int c = two ? 1 : 0; c >= 0; --c)
        {
            juce::Path fill; fill.startNewSubPath (r.getX(), r.getY());
            for (int i = 0; i < Engine::kDisplayPoints; ++i)
            {
                const float x = r.getX() + r.getWidth() * ((float) i + 0.5f) / Engine::kDisplayPoints;
                fill.lineTo (x, yOfRed (peak[c][(size_t) i]));
            }
            fill.lineTo (r.getRight(), r.getY()); fill.closeSubPath();
            g.setColour ((c == 0 ? cFg : cCh1).withAlpha (c == 0 ? 0.55f : 0.7f)); g.fillPath (fill);
        }
        g.setColour (cDim); g.drawHorizontalLine ((int) r.getY(), r.getX(), r.getRight());

        // depth curve(s): white = L/M, dark = R/S when they differ
        const auto s = p.currentSettings();
        auto curvePath = [&] (int ch)
        {
            juce::Path path;
            for (int x = (int) r.getX(); x <= (int) r.getRight(); x += 2)
            {
                const float v = depthCurve (s, fOf ((float) x), ch) / (ch == 0 ? juce::jlimit (0.0001f, 1.0f, 1.0f - s.focus) : juce::jlimit (0.0001f, 1.0f, 1.0f + s.focus));
                const float y = juce::jlimit (r.getY(), r.getBottom(), yOfGain (20.0f * std::log10 (juce::jmax (1.0e-4f, v))));
                if (x == (int) r.getX()) path.startNewSubPath ((float) x, y); else path.lineTo ((float) x, y);
            }
            return path;
        };
        bool split = false;
        for (const auto& b : s.bands) split = split || (b.used && b.on && std::abs (b.focus) > 0.001f);
        g.setColour (cTrack); g.drawHorizontalLine ((int) curveMid(), r.getX(), r.getRight());
        g.setColour (cHot.withAlpha (0.7f)); g.setFont (fnt (10.0f));
        for (float db : { 12.0f, 0.0f, -12.0f })
            g.drawText ((db > 0 ? "+" : "") + juce::String ((int) db), (int) r.getRight() - 30, (int) yOfGain (db) - 13, 28, 12, juce::Justification::centredRight);
        if (split && two) { g.setColour (cCh1); g.strokePath (curvePath (1), juce::PathStrokeType (1.6f)); }
        g.setColour (cHot); g.strokePath (curvePath (0), juce::PathStrokeType (1.8f));

        // listen highlight
        const int listen = (int) p.get ("listen") - 1;
        // nodes
        for (int i = 0; i < kBands; ++i)
        {
            const auto b = band (i);
            if (! b.used) continue;
            const auto pt = nodePos (i);
            const bool sel = selected.count (i) > 0;
            if (i == listen) { g.setColour (cHot.withAlpha (0.12f)); g.fillRect (juce::Rectangle<float>::leftTopRightBottom (xOf (b.freq / 1.5f), r.getY(), xOf (b.freq * 1.5f), r.getBottom())); }
            const float rad = sel ? 8.0f : 7.0f;
            if (b.on) { g.setColour (sel ? cHot : cFg); g.fillEllipse (pt.x - rad, pt.y - rad, rad * 2, rad * 2); }
            else { g.setColour (cPanel); g.fillEllipse (pt.x - rad, pt.y - rad, rad * 2, rad * 2); }
            g.setColour (sel ? cHot : cFg); g.drawEllipse (pt.x - rad, pt.y - rad, rad * 2, rad * 2, 1.4f);
            g.setColour (b.on ? cBg : cFg); g.setFont (fnt (10.0f, true));
            g.drawText (juce::String (i + 1), juce::Rectangle<float> (pt.x - rad, pt.y - rad, rad * 2, rad * 2), juce::Justification::centred);
        }
        if (drag == Box)
        {
            g.setColour (cFg.withAlpha (0.1f)); g.fillRect (juce::Rectangle<float> (boxA, boxB));
            g.setColour (cDim); g.drawRect (juce::Rectangle<float> (boxA, boxB));
        }
        if (showFreq && r.contains (mouse))
        {
            g.setColour (cDim); g.setFont (fnt (10.0f));
            g.drawText (hzStr (fOf (mouse.x)), (int) mouse.x + 8, (int) mouse.y - 16, 70, 12, juce::Justification::centredLeft);
        }
        g.setColour (cDim); g.setFont (fnt (10.0f));
        g.drawText ("reduction " + juce::String ((int) range) + " dB", (int) r.getX() + 4, (int) r.getBottom() - 14, 120, 12, juce::Justification::centredLeft);
    }

    void createBand (juce::Point<float> pt, int type)
    {
        int slot = -1;
        for (int i = 0; i < kBands; ++i) if (! band (i).used) { slot = i; break; }
        if (slot < 0) return;
        p.undo.beginNewTransaction();
        if (auto* rp = p.param (bandId (slot, "used"))) { p.resetBand (slot); juce::ignoreUnused (rp); }
        setP (slot, "type", (float) type);
        setP (slot, "freq", fOf (pt.x));
        setP (slot, "gain", typeHasGain (type) ? gainOfY (pt.y) : 0.0f);
        setP (slot, "q", typeUsesSlope (type) ? 2.0f : 1.0f);
        setP (slot, "on", 1);
        setP (slot, "used", 1);
        selected = { slot }; focusBand = slot;
        if (onSelect) onSelect();
    }
    void deleteBand (int i)
    {
        p.undo.beginNewTransaction();
        setP (i, "used", 0);
        if ((int) p.get ("listen") - 1 == i) p.set ("listen", 0);
        selected.erase (i);
        if (focusBand == i) focusBand = selected.empty() ? -1 : *selected.begin();
        if (onSelect) onSelect();
    }
    void toggleOn (int i) { setP (i, "on", p.get (bandId (i, "on")) > 0.5f ? 0.0f : 1.0f); }

    void shapeMenu (juce::Point<float> pt)
    {
        juce::PopupMenu m;
        m.addSectionHeader ("Add band at " + hzStr (fOf (pt.x)));
        for (int t = 0; t < kNumTypes; ++t) m.addItem (t + 1, bandTypeName (t));
        m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (juce::Rectangle<int> (1, 1).withPosition (localPointToGlobal (pt.toInt()))),
                         [this, pt] (int r) { if (r > 0) createBand (pt, r - 1); repaint(); });
    }
    void nodeMenu (int i)
    {
        const auto b = band (i);
        juce::PopupMenu m, types;
        for (int t = 0; t < kNumTypes; ++t) types.addItem (100 + t, bandTypeName (t), true, b.type == t);
        m.addSubMenu ("Shape", types);
        m.addItem (1, "Enabled", true, b.on);
        m.addItem (2, "Listen", true, (int) p.get ("listen") - 1 == i);
        m.addItem (4, "Reset focus");
        m.addSeparator();
        m.addItem (3, "Delete");
        m.showMenuAsync (juce::PopupMenu::Options(), [this, i] (int r)
        {
            p.undo.beginNewTransaction();
            if (r >= 100) setP (i, "type", (float) (r - 100));
            else if (r == 1) toggleOn (i);
            else if (r == 2) p.set ("listen", (int) p.get ("listen") - 1 == i ? 0.0f : (float) (i + 1));
            else if (r == 3) deleteBand (i);
            else if (r == 4) setP (i, "focus", 0);
            repaint();
        });
    }

    void mouseMove (const juce::MouseEvent& e) override { mouse = e.position; const int h = nodeAt (e.position); if (h != hover) hover = h; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { mouse = { -1, -1 }; repaint(); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        grabKeyboardFocus();
        const int i = nodeAt (e.position);
        if (e.mods.isPopupMenu()) { if (i >= 0) nodeMenu (i); else if (plot().contains (e.position)) shapeMenu (e.position); return; }
        if (i < 0)
        {
            if (! e.mods.isShiftDown()) selected.clear();
            drag = Box; boxA = boxB = e.position;
            if (onSelect) onSelect();
            return;
        }
        if (e.mods.isShiftDown()) { if (selected.count (i)) selected.erase (i); else selected.insert (i); }
        else if (! selected.count (i)) selected = { i };
        focusBand = i;
        if (onSelect) onSelect();
        p.undo.beginNewTransaction();
        drag = Node; dragBand = i; starts.clear();
        for (int s : selected) { const auto b = band (s); starts[s] = { b.freq, b.gain, b.q }; gesture (s, true); }
        if (e.mods.isAltDown()) { altListen = true; p.set ("listen", (float) (i + 1)); }
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        mouse = e.position;
        if (drag == Box)
        {
            boxB = e.position;
            const juce::Rectangle<float> box (boxA, boxB);
            for (int i = 0; i < kBands; ++i)
                if (band (i).used && box.contains (nodePos (i))) selected.insert (i);
            if (onSelect) onSelect();
            repaint(); return;
        }
        if (drag != Node) return;
        const float fine = e.mods.isShiftDown() ? 0.15f : 1.0f;
        const auto d = e.getOffsetFromDragStart().toFloat() * fine;
        for (auto& [i, st] : starts)
        {
            if (e.mods.isCommandDown())
                setP (i, "q", juce::jlimit (0.1f, 10.0f, st.q * std::pow (2.0f, -d.y / 60.0f)));
            else
            {
                const float ratio = std::pow (1000.0f, d.x / plot().getWidth());
                setP (i, "freq", juce::jlimit (20.0f, 20000.0f, st.f * ratio));
                if (typeHasGain (band (i).type)) setP (i, "gain", juce::jlimit (-24.0f, 24.0f, st.g - d.y / curveScale()));
            }
        }
        repaint();
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (drag == Node) for (auto& [i, st] : starts) gesture (i, false);
        if (altListen) { p.set ("listen", 0); altListen = false; }
        drag = None; repaint();
    }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const int i = nodeAt (e.position);
        if (i >= 0)
        {
            p.undo.beginNewTransaction();
            if (selected.count (i)) { const bool on = band (i).on; for (int s : selected) setP (s, "on", on ? 0.0f : 1.0f); }
            else toggleOn (i);
            repaint(); return;
        }
        if (! plot().contains (e.position)) return;
        const float f = fOf (e.position.x);
        int type = Bell;
        if (e.mods.isCommandDown()) type = BandPass;
        else if (f < 40.0f) type = LowCut;
        else if (f > 14000.0f) type = HighCut;
        createBand (e.position, type);
    }
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        const int i = nodeAt (e.position);
        if (i < 0)
        {
            range = juce::jlimit (6.0f, 48.0f, range * (w.deltaY > 0 ? 0.8f : 1.25f));
            range = std::round (range / 6.0f) * 6.0f; repaint(); return;
        }
        p.undo.beginNewTransaction();
        const float fine = e.mods.isShiftDown() ? 0.2f : 1.0f;
        if (e.mods.isCommandDown()) setP (i, "focus", juce::jlimit (-100.0f, 100.0f, p.get (bandId (i, "focus")) + w.deltaY * 100.0f * fine));
        else setP (i, "q", juce::jlimit (0.1f, 10.0f, p.get (bandId (i, "q")) * std::pow (2.0f, w.deltaY * 2.0f * fine)));
        repaint();
    }
    bool keyPressed (const juce::KeyPress& k) override
    {
        if ((k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey) && ! selected.empty())
        {
            auto sel = selected;
            for (int i : sel) deleteBand (i);
            return true;
        }
        return false;
    }
};

//==================================================================================================
struct Content : juce::Component, juce::SettableTooltipClient
{
    QuellProcessor& p;
    Graph graph;
    std::vector<std::unique_ptr<Ctl>> ctls;
    std::vector<std::unique_ptr<Btn>> btns;
    Ctl *depth, *detail, *attack, *release;
    std::vector<Ctl*> side, footerCtls;
    // band strip
    std::unique_ptr<Btn> bType, bOn, bListen, bDel;
    std::unique_ptr<Ctl> bFreq, bGain, bQ, bFocus;
    int bandShown = -2;
    bool sideOpen = true;
    std::function<void (float)> setScale;
    Btn *presetBtn = nullptr, *sideBtn = nullptr;
    juce::Rectangle<int> scMeter, sideArea, topArea, mainArea, stripArea, footArea;
    std::vector<Btn*> top, mainBtns, sideBtns, foot;

    Ctl* mk (const juce::String& id, const juce::String& label, float fs = 11.0f, int lw = 62, int vw = 58, const juce::String& tip = {})
    {
        ctls.push_back (std::make_unique<Ctl> (p, id, label, fs, lw, vw));
        auto* c = ctls.back().get(); addAndMakeVisible (c); c->setTooltip (tip); return c;
    }
    Btn* add (std::unique_ptr<Btn> b, std::vector<Btn*>& group, const juce::String& tip = {})
    {
        btns.push_back (std::move (b)); auto* r = btns.back().get(); addAndMakeVisible (r); r->setTooltip (tip); group.push_back (r); return r;
    }
    Btn* addFn (const juce::String& t, std::function<bool()> lit, std::function<void (const juce::MouseEvent&)> c, std::vector<Btn*>& group, const juce::String& tip = {})
    {
        return add (std::make_unique<Btn> (t, std::move (lit), std::move (c)), group, tip);
    }

    explicit Content (QuellProcessor& pr) : p (pr), graph (pr)
    {
        addAndMakeVisible (graph);
        graph.setTooltip ("Double-click: add band (near the ends: cuts, cmd: band pass). Right-click: choose a shape. Drag nodes (cmd: Q, shift: fine, alt: listen). Scroll a node: Q (cmd: focus). Scroll empty space: graph range.");
        graph.onSelect = [this] { refreshStrip(); };

        // toolbar
        add (toggle (p, "sidechain", "SC"), top, "External sidechain: the reduction follows the sidechain input");
        add (toggle (p, "scListen", U ("\xe2\x97\x8f listen")), top, "Listen to the sidechain input");
        presetBtn = addFn ("", nullptr, [this] (const juce::MouseEvent&) { presetMenu(); }, top, "Presets");
        presetBtn->frame = true;
        addFn (U ("\xe2\x97\x80"), nullptr, [this] (const juce::MouseEvent&) { stepPreset (-1); }, top, "Previous preset");
        addFn (U ("\xe2\x96\xb6"), nullptr, [this] (const juce::MouseEvent&) { stepPreset (1); }, top, "Next preset");
        addFn ("undo", nullptr, [this] (const juce::MouseEvent&) { p.undo.undo(); }, top, "Undo (cmd-Z)");
        addFn ("redo", nullptr, [this] (const juce::MouseEvent&) { p.undo.redo(); }, top, "Redo (shift-cmd-Z)");
        addFn ("A", [this] { return p.abSlot == 0; }, [this] (const juce::MouseEvent&) { p.switchAB (0); }, top, "State A");
        addFn ("B", [this] { return p.abSlot == 1; }, [this] (const juce::MouseEvent&) { p.switchAB (1); }, top, "State B");
        addFn (U ("copy \xe2\x86\x92"), nullptr, [this] (const juce::MouseEvent&) { p.copyAB(); }, top, "Copy the current state to the other slot");
        addFn ("settings", nullptr, [this] (const juce::MouseEvent&) { settingsMenu(); }, top, "Settings and window size");

        // main
        add (choice (p, "mode", 0, "Soft"), mainBtns, "Soft: adaptive threshold, follows relative tonal changes; low distortion");
        add (choice (p, "mode", 1, "Hard"), mainBtns, "Hard: level-dependent; firmer control, can be driven hard");
        depth = mk ("depth", "Depth", 12.0f); depth->big = true; depth->setTooltip ("Amount of reduction. Shaped per frequency by the depth curve.");
        detail = mk ("detail", "Detail", 12.0f); detail->big = true; detail->setTooltip ("Width of the cuts: higher = narrower, deeper notches");
        attack = mk ("attack", "Attack", 12.0f); attack->big = true; attack->setTooltip ("How fast reduction reacts (higher = slower, lets transients through)");
        release = mk ("release", "Release", 12.0f); release->big = true; release->setTooltip ("How fast notches recover (higher = slower, steadier)");
        sideBtn = addFn ("", [this] { return false; }, [this] (const juce::MouseEvent&) { sideOpen = ! sideOpen; resized(); repaint(); }, mainBtns, "Show / hide the side panel");

        // side panel
        add (choice (p, "stereoMode", 0, "L/R"), sideBtns, "Process left and right");
        add (choice (p, "stereoMode", 1, "M/S"), sideBtns, "Process mid and side");
        side.push_back (mk ("link", "Link", 11, 70, 46, "0% = each channel on its own, 100% = analysis summed, same processing"));
        side.push_back (mk ("focus", "Focus", 11, 70, 46, "Shift processing toward L/M (left) or R/S (right)"));
        side.push_back (mk ("detailTiltLo", "Detail low", 11, 70, 46, "Scales Detail below ~500 Hz"));
        side.push_back (mk ("detailTiltHi", "Detail high", 11, 70, 46, "Scales Detail above ~2 kHz"));
        side.push_back (mk ("attackTiltLo", "Attack low", 11, 70, 46, "Scales Attack below ~500 Hz"));
        side.push_back (mk ("attackTiltHi", "Attack high", 11, 70, 46, "Scales Attack above ~2 kHz"));
        side.push_back (mk ("releaseTiltLo", "Release low", 11, 70, 46, "Scales Release below ~500 Hz"));
        side.push_back (mk ("releaseTiltHi", "Release high", 11, 70, 46, "Scales Release above ~2 kHz"));
        side.push_back (mk ("maxCut", "Max cut", 11, 70, 46, "Limit on the reduction (grey area on the graph)"));
        side.push_back (mk ("wetTrim", "Wet trim", 11, 70, 46, "Level of the processed signal before Mix"));

        // footer
        add (toggle (p, "bypass", "Bypass"), foot, "Soft bypass for comparison (keeps processing)");
        add (toggle (p, "delta", "Delta"), foot, "Hear only what is removed");
        footerCtls.push_back (mk ("mix", "Mix", 11, 30, 40, "Wet / dry"));
        footerCtls.push_back (mk ("outGain", "Out", 11, 30, 52, "Output gain (after Mix)"));
        addFn ("", nullptr, [this] (const juce::MouseEvent&) { qualityMenu(); }, foot, "Time resolution and phase. Linear phase adds latency.");
        add (toggle (p, "lowLatency", "Low latency"), foot, "Zero latency, minimum phase, no lookahead");

        // band strip
        bType = std::make_unique<Btn> ("", nullptr, [this] (const juce::MouseEvent&) { typeMenu(); });
        bOn = std::make_unique<Btn> (U ("on"), [this] { return graph.focusBand >= 0 && p.get (bandId (graph.focusBand, "on")) > 0.5f; },
            [this] (const juce::MouseEvent&) { if (graph.focusBand >= 0) { p.undo.beginNewTransaction(); graph.toggleOn (graph.focusBand); } });
        bListen = std::make_unique<Btn> (U ("listen"), [this] { return graph.focusBand >= 0 && (int) p.get ("listen") - 1 == graph.focusBand; },
            [this] (const juce::MouseEvent&) { if (graph.focusBand >= 0) p.set ("listen", (int) p.get ("listen") - 1 == graph.focusBand ? 0.0f : (float) (graph.focusBand + 1)); });
        bDel = std::make_unique<Btn> (U ("delete"), nullptr, [this] (const juce::MouseEvent&) { if (graph.focusBand >= 0) graph.deleteBand (graph.focusBand); });
        bOn->setTooltip ("Enable / bypass the band (the area is still processed)");
        bListen->setTooltip ("Hear what this band's region removes");
        for (auto* b : { bType.get(), bOn.get(), bListen.get(), bDel.get() }) addChildComponent (b);
        setSize (QuellEditor::baseW, QuellEditor::baseH);
        tick();
    }

    void refreshStrip()
    {
        const int i = graph.focusBand >= 0 && p.get (bandId (graph.focusBand, "used")) > 0.5f ? graph.focusBand : -1;
        if (i != graph.focusBand) graph.focusBand = i;
        if (i != bandShown)
        {
            bandShown = i;
            for (auto* c : { &bFreq, &bGain, &bQ, &bFocus }) if (*c) { removeChildComponent (c->get()); c->reset(); }
            if (i >= 0)
            {
                bFreq = std::make_unique<Ctl> (p, bandId (i, "freq"), "Freq", 11.0f, 34, 62);
                bGain = std::make_unique<Ctl> (p, bandId (i, "gain"), "Depth", 11.0f, 38, 52);
                bQ = std::make_unique<Ctl> (p, bandId (i, "q"), "Q", 11.0f, 40, 40);
                bFocus = std::make_unique<Ctl> (p, bandId (i, "focus"), "Focus", 11.0f, 38, 44);
                bGain->setTooltip ("Boost = more suppression here, cut = less");
                bFocus->setTooltip ("Per-band stereo balance (L/M <-> R/S)");
                for (auto* c : { bFreq.get(), bGain.get(), bQ.get(), bFocus.get() }) addAndMakeVisible (c);
            }
            for (auto* b : { bType.get(), bOn.get(), bListen.get(), bDel.get() }) b->setVisible (i >= 0);
            layoutStrip();
        }
        if (i >= 0)
        {
            const int t = (int) p.get (bandId (i, "type"));
            bType->text = juce::String (i + 1) + "  " + bandTypeName (t) + U (kDown);
            if (bQ) { bQ->label = typeUsesSlope (t) ? "Slope" : "Q"; bQ->setVisible (true); }
            if (bGain) bGain->setVisible (typeHasGain (t));
            bType->repaint();
        }
        repaint (stripArea);
    }

    void typeMenu()
    {
        const int i = graph.focusBand; if (i < 0) return;
        juce::PopupMenu m;
        for (int t = 0; t < kNumTypes; ++t) m.addItem (t + 1, bandTypeName (t), true, (int) p.get (bandId (i, "type")) == t);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (bType.get()), [this, i] (int r)
        {
            if (r <= 0) return;
            p.undo.beginNewTransaction();
            for (int s : graph.selected.empty() ? std::set<int> { i } : graph.selected) graph.setP (s, "type", (float) (r - 1));
            refreshStrip();
        });
    }

    void presetMenu()
    {
        juce::PopupMenu m, user;
        const auto& list = factoryPresets();
        std::map<juce::String, juce::PopupMenu> cats;
        for (size_t i = 0; i < list.size(); ++i) cats[list[i].category].addItem ((int) i + 1, list[i].name, true, p.presetName == list[i].name);
        for (auto& [name, sub] : cats) m.addSubMenu (name, sub);
        auto files = p.userPresetFolder().findChildFiles (juce::File::findFiles, false, "*.quell");
        files.sort();
        for (int i = 0; i < files.size(); ++i) user.addItem (1000 + i, files[i].getFileNameWithoutExtension(), true, p.presetName == files[i].getFileNameWithoutExtension());
        m.addSubMenu ("User", user, files.size() > 0);
        m.addSeparator();
        m.addItem (1, "Reset to default");
        m.addItem (2000, "Save as...");
        m.addItem (2001, "Show preset folder");
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (presetBtn), [this, files] (int r)
        {
            if (r >= 1000 && r < 2000) p.loadPresetFile (files[r - 1000]);
            else if (r == 2000) saveDialog();
            else if (r == 2001) { p.userPresetFolder().createDirectory(); p.userPresetFolder().revealToUser(); }
            else if (r > 0) p.loadFactory (r - 1);
            graph.selected.clear(); refreshStrip(); repaint();
        });
    }
    std::unique_ptr<juce::AlertWindow> alert;
    void saveDialog()
    {
        alert = std::make_unique<juce::AlertWindow> ("Save preset", "Name:", juce::MessageBoxIconType::NoIcon, this);
        alert->addTextEditor ("name", p.presetName);
        alert->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
        alert->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        alert->enterModalState (true, juce::ModalCallbackFunction::create ([this] (int r)
        {
            if (r == 1 && alert != nullptr) { const auto n = alert->getTextEditorContents ("name").trim(); if (n.isNotEmpty()) p.saveUserPreset (n); }
            alert.reset(); repaint();
        }), false);
    }
    void stepPreset (int dir)
    {
        // factory list followed by user presets
        juce::StringArray names; const auto& list = factoryPresets();
        for (auto& f : list) names.add (f.name);
        auto files = p.userPresetFolder().findChildFiles (juce::File::findFiles, false, "*.quell");
        files.sort();
        for (auto& f : files) names.add (f.getFileNameWithoutExtension());
        int i = names.indexOf (p.presetName);
        i = (i + dir + names.size()) % names.size();
        if (i < (int) list.size()) p.loadFactory (i); else p.loadPresetFile (files[i - (int) list.size()]);
        graph.selected.clear(); refreshStrip(); repaint();
    }
    void qualityMenu()
    {
        juce::PopupMenu m;
        const int q = (int) p.get ("quality");
        m.addItem (1, "Normal", true, q == 0); m.addItem (2, "High", true, q == 1); m.addItem (3, "Ultra", true, q == 2);
        m.addSeparator();
        m.addItem (10, "Linear phase", true, p.get ("linear") > 0.5f);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (foot[2]), [this] (int r)
        {
            p.undo.beginNewTransaction();
            if (r >= 1 && r <= 3) p.set ("quality", (float) (r - 1));
            if (r == 10) p.set ("linear", p.get ("linear") > 0.5f ? 0.0f : 1.0f);
        });
    }
    void settingsMenu()
    {
        juce::PopupMenu m, size;
        m.addItem (1, "Always render on ultra (offline bounces)", true, p.get ("renderUltra") > 0.5f);
        m.addItem (2, "Show frequency near cursor", true, graph.showFreq);
        for (int i = 0; i < 4; ++i) size.addItem (10 + i, juce::StringArray { "S", "M", "L", "XL" }[i]);
        m.addSubMenu ("Window size", size);
        m.addSeparator();
        m.addItem (3, "Latency: " + juce::String (p.getLatencySamples()) + " samples", false, false);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (top.back()), [this] (int r)
        {
            if (r == 1) p.set ("renderUltra", p.get ("renderUltra") > 0.5f ? 0.0f : 1.0f);
            if (r == 2) graph.showFreq = ! graph.showFreq;
            if (r >= 10 && setScale) setScale (std::array<float, 4> { 0.8f, 1.0f, 1.25f, 1.5f }[(size_t) (r - 10)]);
        });
    }

    void tick()
    {
        graph.update();
        for (auto& c : ctls) c->refresh();
        for (auto* c : { bFreq.get(), bGain.get(), bQ.get(), bFocus.get() }) if (c) c->refresh();
        for (auto& b : btns) b->refresh();
        for (auto* b : { bOn.get(), bListen.get() }) b->refresh();
        presetBtn->text = p.presetName + U (kDown);
        sideBtn->text = sideOpen ? U ("\xe2\x96\xb6") : U ("\xe2\x97\x80");
        const int q = (int) p.get ("quality");
        foot[2]->text = juce::String (q == 0 ? "Normal" : q == 1 ? "High" : "Ultra") + (p.get ("linear") > 0.5f ? " / linear" : "") + U (kDown);
        if (p.get ("lowLatency") > 0.5f) foot[2]->text = juce::String ("Normal (low latency)");
        for (auto* b : { presetBtn, sideBtn, foot[2] }) b->repaint();
        refreshStrip();
        repaint (scMeter);
        repaint (footArea.withLeft (footArea.getRight() - 200));
        foot[3]->setTooltip ("Zero latency, minimum phase, no lookahead. Current latency: " + juce::String (p.getLatencySamples()) + " samples");
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (cBg);
        g.setColour (cFg); g.setFont (fnt (16.0f, true));
        g.drawText ("Quell", topArea.withWidth (70), juce::Justification::centredLeft);
        g.setColour (cLine);
        g.drawHorizontalLine (topArea.getBottom(), 0, (float) getWidth());
        g.drawHorizontalLine (footArea.getY() - 1, 0, (float) getWidth());
        // sidechain meter
        g.setColour (cTrack); g.fillRect (scMeter);
        if (p.sidechainConnected())
        {
            const float lv = juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (p.sidechainLevel(), -60.0f) + 60.0f) / 60.0f);
            g.setColour (cFg); g.fillRect (scMeter.withWidth ((int) (lv * (float) scMeter.getWidth())));
        }
        if (sideOpen && ! sideArea.isEmpty())
        {
            g.setColour (cPanel); g.fillRect (sideArea);
            g.setColour (cDim); g.setFont (fnt (10.0f, true));
            auto head = [&] (const char* t, int y) { g.drawText (t, sideArea.getX() + 12, y, 120, 14, juce::Justification::centredLeft); };
            head ("STEREO", sideArea.getY() + 8);
            head ("TILT", side[2]->getY() - 20);
            head ("SCALE", side[8]->getY() - 20);
        }
        // band strip label
        g.setColour (cDim); g.setFont (fnt (11.0f));
        if (graph.focusBand < 0)
            g.drawText ("Double-click the graph to add a band (up to 8). Right-click to pick a shape.", stripArea.reduced (14, 0), juce::Justification::centredLeft);
        // footer: reduction meters + latency
        const auto meterR = footArea.withLeft (footArea.getRight() - 196).reduced (14, 8);
        g.setFont (fnt (10.0f));
        const int nm = p.numMainChannels();
        const bool ms = p.get ("stereoMode") > 0.5f;
        for (int c = 0; c < nm; ++c)
        {
            auto row = meterR.withHeight (meterR.getHeight() / 2).translated (0, c * meterR.getHeight() / 2).reduced (0, 2);
            g.setColour (cDim); g.drawText (nm == 1 ? "GR" : (ms ? (c == 0 ? "M" : "S") : (c == 0 ? "L" : "R")), row.removeFromLeft (14), juce::Justification::centredLeft);
            g.drawText (juce::String (p.meter (c), 1), row.removeFromRight (34), juce::Justification::centredRight);
            row.removeFromRight (6);
            g.setColour (cTrack); g.fillRect (row);
            const float m = juce::jlimit (0.0f, 1.0f, p.meter (c) / 24.0f);
            g.setColour (cHot); g.fillRect (row.withWidth ((int) (m * (float) row.getWidth())));
        }
    }

    void layoutStrip()
    {
        auto r = stripArea.reduced (10, 6);
        if (graph.focusBand < 0) return;
        bType->setBounds (r.removeFromLeft (130)); r.removeFromLeft (14);
        bFreq->setBounds (r.removeFromLeft (190)); r.removeFromLeft (14);
        bGain->setBounds (r.removeFromLeft (180)); r.removeFromLeft (14);
        bQ->setBounds (r.removeFromLeft (170)); r.removeFromLeft (14);
        bFocus->setBounds (r.removeFromLeft (170));
        bDel->setBounds (r.removeFromRight (56)); r.removeFromRight (6);
        bListen->setBounds (r.removeFromRight (56)); r.removeFromRight (6);
        bOn->setBounds (r.removeFromRight (40));
    }

    void resized() override
    {
        auto r = getLocalBounds();
        topArea = r.removeFromTop (40).reduced (14, 0);
        footArea = r.removeFromBottom (44);
        // toolbar
        {
            auto t = topArea.withTrimmedLeft (80).reduced (0, 9);
            top[0]->setBounds (t.removeFromLeft (34)); t.removeFromLeft (4);
            top[1]->setBounds (t.removeFromLeft (56)); t.removeFromLeft (6);
            scMeter = t.removeFromLeft (60).withSizeKeepingCentre (60, 4); t.removeFromLeft (24);
            presetBtn->setBounds (t.removeFromLeft (220)); t.removeFromLeft (4);
            top[3]->setBounds (t.removeFromLeft (24)); t.removeFromLeft (2);
            top[4]->setBounds (t.removeFromLeft (24)); t.removeFromLeft (16);
            top[5]->setBounds (t.removeFromLeft (44)); t.removeFromLeft (4);
            top[6]->setBounds (t.removeFromLeft (44)); t.removeFromLeft (16);
            top[7]->setBounds (t.removeFromLeft (26)); t.removeFromLeft (2);
            top[8]->setBounds (t.removeFromLeft (26)); t.removeFromLeft (4);
            top[9]->setBounds (t.removeFromLeft (56));
            top[10]->setBounds (t.removeFromRight (70));
        }
        // side panel
        sideArea = sideOpen ? r.removeFromRight (230) : juce::Rectangle<int>();
        for (auto* c : side) c->setVisible (sideOpen);
        for (auto* b : sideBtns) b->setVisible (sideOpen);
        if (sideOpen)
        {
            auto s = sideArea.reduced (12, 0).withTrimmedTop (28);
            auto modeRow = s.removeFromTop (22);
            sideBtns[0]->setBounds (modeRow.removeFromLeft (50)); modeRow.removeFromLeft (4);
            sideBtns[1]->setBounds (modeRow.removeFromLeft (50));
            s.removeFromTop (6);
            for (int i = 0; i < (int) side.size(); ++i)
            {
                if (i == 2 || i == 8) s.removeFromTop (26);
                side[(size_t) i]->setBounds (s.removeFromTop (24));
            }
        }
        // main controls
        mainArea = r.removeFromTop (78).reduced (14, 10);
        {
            auto m = mainArea;
            auto modes = m.removeFromLeft (58);
            mainBtns[0]->setBounds (modes.removeFromTop (26)); modes.removeFromTop (6);
            mainBtns[1]->setBounds (modes.removeFromTop (26));
            m.removeFromLeft (26);
            sideBtn->setBounds (m.removeFromRight (24).withSizeKeepingCentre (24, 24));
            m.removeFromRight (20);
            const int w = (m.getWidth() - 3 * 26) / 4;
            for (auto* c : { depth, detail, attack, release }) { c->setBounds (m.removeFromLeft (w)); m.removeFromLeft (26); }
        }
        stripArea = r.removeFromBottom (36);
        graph.setBounds (r.reduced (8, 0));
        layoutStrip();
        // footer
        {
            auto f = footArea.reduced (14, 10);
            foot[0]->setBounds (f.removeFromLeft (64)); f.removeFromLeft (6);
            foot[1]->setBounds (f.removeFromLeft (56)); f.removeFromLeft (20);
            footerCtls[0]->setBounds (f.removeFromLeft (150)); f.removeFromLeft (16);
            footerCtls[1]->setBounds (f.removeFromLeft (160)); f.removeFromLeft (16);
            foot[2]->setBounds (f.removeFromLeft (140)); f.removeFromLeft (6);
            foot[3]->setBounds (f.removeFromLeft (90));
        }
    }
};
} // namespace

//==================================================================================================
QuellEditor::QuellEditor (QuellProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    auto* c = new Content (p);
    content.reset (c);
    addAndMakeVisible (*content);
    c->setScale = [this] (float s) { setScale (s); };
    tips = std::make_unique<juce::TooltipWindow> (this, 900);
    setResizable (true, true);
    constrainer.setFixedAspectRatio ((double) baseW / baseH);
    constrainer.setSizeLimits (baseW * 7 / 10, baseH * 7 / 10, baseW * 2, baseH * 2);
    setConstrainer (&constrainer);
    const int w = p.uiWidth > 0 ? p.uiWidth : baseW, h = p.uiHeight > 0 ? p.uiHeight : baseH;
    setSize (w, h);
    setWantsKeyboardFocus (true);
    startTimerHz (30);
}

QuellEditor::~QuellEditor() { stopTimer(); }

void QuellEditor::setScale (float s) { setSize (juce::roundToInt (baseW * s), juce::roundToInt (baseH * s)); }

void QuellEditor::paint (juce::Graphics& g) { g.fillAll (cBg); }

void QuellEditor::resized()
{
    const float s = (float) getWidth() / (float) baseW;
    content->setTransform (juce::AffineTransform::scale (s));
    content->setBounds (0, 0, baseW, baseH);
    proc.uiWidth = getWidth(); proc.uiHeight = getHeight();
}

bool QuellEditor::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0)) { proc.undo.undo(); return true; }
    if (k == juce::KeyPress ('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0)) { proc.undo.redo(); return true; }
    return false;
}

void QuellEditor::timerCallback() { static_cast<Content*> (content.get())->tick(); }

juce::AudioProcessorEditor* QuellProcessor::createEditor() { return new QuellEditor (*this); }
