// Quell: the window, in the style of an early-2000s generative audio app: a flat red field (blue in help mode),
// a white play area with grey rails, grids of coloured squares, orange gradient blocks, gradient squares with
// red triangles and small bold lowercase labels. The hovered control's description shows in the status pill.
#include "PluginEditor.h"

using namespace ql;

namespace
{
const juce::Colour cRed (0xffc8251f), cBlue (0xff1fb1e6), cWhite (0xffffffff), cGrey (0xffb7b8ba), cGreyD (0xff7d7e80),
    cInk (0xff1a1a1a), cOrange (0xfff7931e), cOrangeL (0xfffbc26b), cYellow (0xfff2c400), cPink (0xffe58fbf), cLilac (0xff7b6fb8),
    cBrown (0xffa88a1e), cNavy (0xff1d2f6f), cRing (0xff2a7fd0), cTri (0xffd22a26), cGreen (0xff1f8a3c);
const juce::Colour kBandCol[kBands] = { juce::Colour (0xffd22a26), juce::Colour (0xfff2c400), juce::Colour (0xff1f8a3c), juce::Colour (0xffe58fbf),
                                        juce::Colour (0xff2e7dc4), juce::Colour (0xfff7931e), juce::Colour (0xff6f5fb0), juce::Colour (0xff222222) };

juce::Font fnt (float h, bool bold = true)
{
    return juce::Font (juce::FontOptions ("Verdana", h, bold ? juce::Font::bold : juce::Font::plain)).withExtraKerningFactor (0.04f);
}
juce::Font serif (float h) { return juce::Font (juce::FontOptions ("Times New Roman", h, juce::Font::plain)); }
juce::String comma (juce::String s) { return s.replace (".", ","); }

juce::String hzStr (float f) { return comma (f >= 1000.0f ? juce::String (f / 1000.0f, f >= 10000.0f ? 1 : 2) + " khz" : juce::String (juce::roundToInt (f)) + " hz"); }

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
    if (id == "depth" || id == "detail" || id == "attack" || id == "release") return comma (juce::String (v, 1));
    if (id.endsWith ("_freq")) return hzStr (v);
    if (id.endsWith ("_q")) return comma (juce::String (v, 2));
    if (id.endsWith ("_gain") || id == "wetTrim" || id == "outGain") return comma ((v > 0.05f ? "+" : "") + juce::String (std::abs (v) < 0.05f ? 0.0f : v, 1) + " db");
    if (id == "maxCut") return v >= 39.95f ? juce::String ("off") : comma (juce::String (v, 1) + " db");
    if (id.endsWith ("focus") || id.contains ("Tilt")) return (v > 0.05f ? "+" : "") + juce::String (juce::roundToInt (v)) + "%";
    if (id == "link" || id == "mix") return juce::String (juce::roundToInt (v)) + "%";
    return comma (juce::String (v, 2));
}

// ---- drawing helpers -------------------------------------------------------------------------------
void checker (juce::Graphics& g, juce::Rectangle<float> r, float cell, juce::Colour a, juce::Colour b)
{
    if (r.isEmpty()) return;
    juce::Graphics::ScopedSaveState ss (g);
    g.reduceClipRegion (r.toNearestInt());
    for (float y = r.getY(); y < r.getBottom(); y += cell)
        for (float x = r.getX(); x < r.getRight(); x += cell)
        {
            const bool odd = ((int) ((x - r.getX()) / cell) + (int) ((y - r.getY()) / cell)) & 1;
            g.setColour (odd ? b : a); g.fillRect (x, y, cell, cell);
        }
}
void gradSquare (juce::Graphics& g, juce::Rectangle<float> r, bool flip = false)
{
    juce::ColourGradient gr (flip ? cWhite : cInk, r.getX(), r.getY(), flip ? cInk : cWhite, r.getRight(), r.getY(), false);
    g.setGradientFill (gr); g.fillRect (r);
}
void triangle (juce::Graphics& g, juce::Point<float> tip, float size, float angle, juce::Colour c, bool fade = true)
{
    juce::Path p; p.addTriangle (0, 0, -size, -size * 0.6f, -size, size * 0.6f);
    p.applyTransform (juce::AffineTransform::rotation (angle).translated (tip));
    if (fade)
    {
        const auto b = p.getBounds();
        g.setGradientFill (juce::ColourGradient (c, tip.x, tip.y, cGrey, b.getCentreX() - (tip.x - b.getCentreX()), b.getCentreY() - (tip.y - b.getCentreY()), false));
    }
    else g.setColour (c);
    g.fillPath (p);
}
void label (juce::Graphics& g, const juce::String& t, juce::Rectangle<float> r, juce::Colour c, float h = 9.5f, juce::Justification j = juce::Justification::centredLeft)
{
    g.setColour (c); g.setFont (fnt (h)); g.drawText (t, r, j, false);
}
// grey label bar: "try presets"
void greyBar (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& t, juce::Justification j = juce::Justification::centredRight)
{
    g.setColour (cGrey); g.fillRect (r);
    label (g, t, r.reduced (8, 0), cInk, 9.5f, j);
}
void ring (juce::Graphics& g, juce::Rectangle<float> r, bool lit, bool sel)
{
    g.setColour (sel ? cOrange : cNavy); g.fillEllipse (r);
    g.setColour (cRing); g.fillEllipse (r.reduced (r.getWidth() * 0.18f));
    g.setColour (cNavy); g.fillEllipse (r.reduced (r.getWidth() * 0.3f));
    g.setColour (lit ? cWhite : cRing.darker (0.3f)); g.fillEllipse (r.reduced (r.getWidth() * 0.42f));
}
void dotCircle (juce::Graphics& g, juce::Rectangle<float> r, bool on, juce::Colour dot = cInk)
{
    g.setColour (cWhite); g.fillRect (r);
    g.setColour (cInk); g.drawRect (r, 1.5f);
    const auto c = r.reduced (r.getWidth() * 0.18f);
    g.setColour (on ? dot : cWhite); g.fillEllipse (c);
    g.setColour (cInk); g.drawEllipse (c, 2.0f);
}
// symbol inside a grid cell / icon circle: 0 square, 1 slash, 2 diamond, 3 cross
void symbol (juce::Graphics& g, int kind, juce::Rectangle<float> r, juce::Colour c)
{
    g.setColour (c);
    const auto k = r.reduced (r.getWidth() * 0.26f);
    switch (kind)
    {
        case 0: g.fillRect (k); break;
        case 1: g.drawLine (k.getX(), k.getBottom(), k.getRight(), k.getY(), r.getWidth() * 0.16f); break;
        case 2: { juce::Path p; p.addQuadrilateral (k.getCentreX(), k.getY(), k.getRight(), k.getCentreY(), k.getCentreX(), k.getBottom(), k.getX(), k.getCentreY()); g.fillPath (p); break; }
        default: g.drawLine (k.getX(), k.getY(), k.getRight(), k.getBottom(), r.getWidth() * 0.14f); g.drawLine (k.getX(), k.getBottom(), k.getRight(), k.getY(), r.getWidth() * 0.14f); break;
    }
}

//==================================================================================================
struct Look : juce::LookAndFeel_V4
{
    Look()
    {
        setColour (juce::PopupMenu::backgroundColourId, cWhite);
        setColour (juce::PopupMenu::textColourId, cInk);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, cOrange);
        setColour (juce::PopupMenu::highlightedTextColourId, cWhite);
        setColour (juce::PopupMenu::headerTextColourId, cTri);
        setColour (juce::TextEditor::backgroundColourId, cWhite);
        setColour (juce::TextEditor::textColourId, cInk);
        setColour (juce::TextEditor::outlineColourId, cInk);
        setColour (juce::TextEditor::focusedOutlineColourId, cOrange);
        setColour (juce::AlertWindow::backgroundColourId, cGrey);
        setColour (juce::AlertWindow::textColourId, cInk);
        setColour (juce::TextButton::buttonColourId, cWhite);
        setColour (juce::TextButton::textColourOffId, cInk);
    }
    juce::Font getPopupMenuFont() override { return fnt (11.0f); }
    void drawPopupMenuBackground (juce::Graphics& g, int w, int h) override
    {
        g.fillAll (cWhite); g.setColour (cGrey); g.drawRect (0, 0, w, h, 2);
    }
    juce::Font getAlertWindowMessageFont() override { return fnt (11.0f); }
    juce::Font getAlertWindowTitleFont() override { return fnt (13.0f); }
};

//==================================================================================================
// parameter control. Rail: grey bar with orange checker fill. Side: label, black-white strip, red triangle.
// Line: timeline bar with coloured segments and gradient tabs.
struct Ctl : juce::Component, juce::SettableTooltipClient
{
    enum Style { Rail, Side, Line };
    QuellProcessor& p; juce::String id, name; Style style; juce::Colour seg = cYellow;
    float startV = 0; bool dragging = false; float shown = -1.0e9f;
    std::unique_ptr<juce::TextEditor> ed;

    Ctl (QuellProcessor& pr, juce::String i, juce::String n, Style s) : p (pr), id (std::move (i)), name (std::move (n)), style (s) {}
    juce::RangedAudioParameter* par() const { return p.param (id); }
    float value() const { return p.get (id); }
    void refresh() { if (value() != shown) repaint(); }

    void paint (juce::Graphics& g) override
    {
        auto* rp = par(); if (rp == nullptr) return;
        const float v = value(); shown = v;
        const auto range = rp->getNormalisableRange();
        const float n = range.convertTo0to1 (v), zero = range.start < 0 ? range.convertTo0to1 (0.0f) : 0.0f;
        auto r = getLocalBounds().toFloat();
        const auto txt = fmtValue (id, v);
        if (style == Rail)
        {
            const auto bar = r.reduced (0, 3);
            g.setColour (cGrey); g.fillRect (bar);
            const float x0 = bar.getX() + juce::jmin (n, zero) * bar.getWidth(), x1 = bar.getX() + juce::jmax (n, zero) * bar.getWidth();
            checker (g, juce::Rectangle<float>::leftTopRightBottom (x0, bar.getY(), x1, bar.getBottom()), bar.getHeight() / 3.0f, cOrange, cOrangeL);
            const float mx = juce::jlimit (3.0f, r.getWidth() - 3.0f, bar.getX() + n * bar.getWidth());
            gradSquare (g, { mx - 3.0f, r.getY(), 6.0f, r.getHeight() });
            label (g, name + " " + txt, bar.reduced (8, 0), cInk, 9.5f, juce::Justification::centredRight);
        }
        else if (style == Side)
        {
            label (g, name, r.withHeight (12), cWhite, 9.0f);
            label (g, txt, r.withHeight (12), cWhite, 9.0f, juce::Justification::centredRight);
            const auto strip = juce::Rectangle<float> (r.getX() + 4, r.getY() + 21.0f, r.getWidth() - 8.0f, 9.0f);
            gradSquare (g, strip, range.start < 0);
            triangle (g, { strip.getX() + n * strip.getWidth(), strip.getY() }, 8.0f, juce::MathConstants<float>::halfPi, cTri, false);
        }
        else
        {
            const auto bar = juce::Rectangle<float> (r.getX() + 4, r.getY() + 6.0f, r.getWidth() - 8, 12.0f);
            g.setColour (cWhite); g.fillRect (bar);
            const float x0 = bar.getX() + juce::jmin (n, zero) * bar.getWidth(), x1 = bar.getX() + juce::jmax (n, zero) * bar.getWidth();
            g.setColour (seg); g.fillRect (juce::Rectangle<float>::leftTopRightBottom (x0, bar.getY(), x1, bar.getBottom()));
            g.setColour (cPink); g.fillRect (juce::Rectangle<float>::leftTopRightBottom (x1, bar.getY() + 3, juce::jmin (bar.getRight(), x1 + 18), bar.getBottom() - 3));
            const float mx = bar.getX() + n * bar.getWidth();
            gradSquare (g, { mx - 3.0f, bar.getY() - 6.0f, 6.0f, bar.getHeight() + 12.0f }, true);
            g.setColour (cInk); g.fillRect (mx - 0.5f, bar.getY() - 6.0f, 1.0f, bar.getHeight() + 12.0f);
            label (g, name + " " + txt, r.withTop (bar.getBottom() + 6).withTrimmedLeft (4), cWhite, 9.0f);
        }
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
        const float span = juce::jmax (120.0f, (float) getWidth());
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
        ed->setBounds (getLocalBounds().withHeight (juce::jmin (getHeight(), 18)).withTrimmedLeft (getWidth() / 2));
        ed->setFont (fnt (10.0f));
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

// generic button with a custom painter
struct Btn : juce::Component, juce::SettableTooltipClient
{
    using Painter = std::function<void (juce::Graphics&, juce::Rectangle<float>, bool on, bool over)>;
    Painter painter; std::function<bool()> lit; std::function<void (const juce::MouseEvent&)> click;
    bool shown = false;
    Btn (Painter pt, std::function<bool()> l, std::function<void (const juce::MouseEvent&)> c) : painter (std::move (pt)), lit (std::move (l)), click (std::move (c)) {}
    void refresh() { if (lit && lit() != shown) repaint(); }
    void paint (juce::Graphics& g) override { shown = lit && lit(); painter (g, getLocalBounds().toFloat(), shown, isMouseOver()); }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void mouseDown (const juce::MouseEvent& e) override { if (click) click (e); repaint(); }
};

// painters
Btn::Painter orangeBlock (std::function<juce::String()> t)
{
    return [t] (juce::Graphics& g, juce::Rectangle<float> r, bool on, bool over)
    {
        if (on) checker (g, r, r.getHeight() / 3.0f, cOrange, cYellow);
        else { g.setColour (over ? cWhite : cGrey); g.fillRect (r); }
        label (g, t(), r, cInk, 9.5f, juce::Justification::centred);
    };
}
Btn::Painter gradIcon (juce::String t, bool flip)
{
    return [t, flip] (juce::Graphics& g, juce::Rectangle<float> r, bool on, bool over)
    {
        auto sq = juce::Rectangle<float> (r.getX(), r.getY(), r.getHeight() - 12.0f, r.getHeight() - 12.0f);
        if (on) { g.setColour (cInk); g.fillRect (sq); g.setColour (cWhite); g.fillRect (sq.reduced (sq.getWidth() * 0.3f)); }
        else gradSquare (g, sq, flip);
        triangle (g, { sq.getRight() + 14.0f, sq.getCentreY() }, 13.0f, 0.0f, on || over ? cTri : cRing);
        label (g, t, r.withTop (sq.getBottom() + 1), cWhite, 8.0f);
    };
}

std::unique_ptr<Btn> paramToggle (QuellProcessor& p, const juce::String& id, Btn::Painter pt)
{
    return std::make_unique<Btn> (std::move (pt), [&p, id] { return p.get (id) > 0.5f; },
        [&p, id] (const juce::MouseEvent&) { p.undo.beginNewTransaction(); if (auto* rp = p.param (id)) { rp->beginChangeGesture(); rp->setValueNotifyingHost (p.get (id) > 0.5f ? 0.0f : 1.0f); rp->endChangeGesture(); } });
}
std::unique_ptr<Btn> paramChoice (QuellProcessor& p, const juce::String& id, int value, Btn::Painter pt)
{
    return std::make_unique<Btn> (std::move (pt), [&p, id, value] { return (int) p.get (id) == value; },
        [&p, id, value] (const juce::MouseEvent&) { p.undo.beginNewTransaction(); if (auto* rp = p.param (id)) { rp->beginChangeGesture(); rp->setValueNotifyingHost (rp->convertTo0to1 ((float) value)); rp->endChangeGesture(); } });
}

//==================================================================================================
// a grid of coloured squares: each cell may be an action (preset, shape, remove band)
struct Grid : juce::Component
{
    int kind = 0, cols = 8, rows = 6;
    std::function<int()> count;                         // active cells
    std::function<juce::String (int)> name;             // for the status line
    std::function<bool (int)> current;
    std::function<void (int)> pick;
    std::vector<juce::Colour> outer, inner;
    int hover = -1;
    juce::String hoverText() const { return hover >= 0 && hover < (count ? count() : 0) ? name (hover) : juce::String(); }

    explicit Grid (int k, int seed) : kind (k)
    {
        const juce::Colour pal[] = { cTri, cYellow, cGreen, cPink, juce::Colour (0xff2e7dc4), cOrange, cLilac, cInk, cRing, juce::Colour (0xff6d4c9f), cBrown };
        juce::Random r (seed);
        for (int i = 0; i < 64; ++i) { outer.push_back (pal[r.nextInt (11)]); inner.push_back (pal[r.nextInt (11)]); }
    }
    float cell() const { return juce::jmin ((float) getWidth() / cols, (float) getHeight() / rows); }
    int cellAt (juce::Point<float> pt) const
    {
        const float c = cell(); const int x = (int) (pt.x / c), y = (int) (pt.y / c);
        return x >= 0 && x < cols && y >= 0 && y < rows ? y * cols + x : -1;
    }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (cWhite);
        const float c = cell();
        const int active = count ? count() : 0;
        for (int i = 0; i < cols * rows; ++i)
        {
            const auto r = juce::Rectangle<float> ((float) (i % cols) * c, (float) (i / cols) * c, c, c).reduced (1.5f);
            const bool on = i < active;
            const float a = on ? 1.0f : 0.3f;
            g.setColour (outer[(size_t) i].withMultipliedAlpha (a)); g.fillRect (r);
            g.setColour (cWhite); g.fillRect (r.reduced (c * 0.14f));
            symbol (g, kind, r, inner[(size_t) i].withMultipliedAlpha (a));
            if (on && current && current (i)) { g.setColour (cInk); g.drawRect (r.expanded (1.0f), 2.0f); }
            if (i == hover && on) { g.setColour (cInk.withAlpha (0.25f)); g.fillRect (r); }
        }
    }
    void mouseMove (const juce::MouseEvent& e) override { hover = cellAt (e.position); repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        const int i = cellAt (e.position);
        if (i >= 0 && i < (count ? count() : 0) && pick) pick (i);
        repaint();
    }
};

//==================================================================================================
// the reduction graph with the depth curve and the band nodes
struct Graph : juce::Component, juce::SettableTooltipClient
{
    QuellProcessor& p;
    Engine::Display disp; std::array<float, Engine::kDisplayPoints> peak[2] {};
    std::set<int> selected; int focusBand = -1;
    std::function<void()> onSelect;
    float range = 24.0f;
    enum Drag { None, Node, Box } drag = None;
    juce::Point<float> boxA, boxB; bool altListen = false;
    struct Start { float f, g, q; }; std::map<int, Start> starts;
    bool showFreq = true; juce::Point<float> mouse { -1, -1 };
    int pendingType = -1; // shape picked in the grid with nothing selected: used for the next new band

    explicit Graph (QuellProcessor& pr) : p (pr) { setWantsKeyboardFocus (true); }

    juce::Rectangle<float> plot() const { return getLocalBounds().toFloat().withTrimmedLeft (30.0f).withTrimmedRight (8.0f).withTrimmedTop (4.0f).withTrimmedBottom (16.0f); }
    float xOf (float f) const { auto r = plot(); return r.getX() + r.getWidth() * std::log (f / 20.0f) / std::log (1000.0f); }
    float fOf (float x) const { auto r = plot(); return juce::jlimit (20.0f, 20000.0f, 20.0f * std::pow (1000.0f, (x - r.getX()) / r.getWidth())); }
    float curveMid() const { auto r = plot(); return r.getY() + r.getHeight() * 0.5f; }
    float curveScale() const { return plot().getHeight() * 0.42f / 24.0f; }
    float yOfGain (float db) const { return curveMid() - db * curveScale(); }
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
            if (band (i).used && nodePos (i).getDistanceFrom (pt) < 10.0f) return i;
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
        // dotted frequency guides
        for (float f : { 20.f, 50.f, 100.f, 200.f, 500.f, 1000.f, 2000.f, 5000.f, 10000.f, 20000.f })
        {
            const float x = xOf (f);
            g.setColour (cGrey);
            for (float y = r.getY(); y < r.getBottom(); y += 7.0f) g.fillRect (x - 1.0f, y, 2.0f, 2.0f);
            label (g, f >= 1000 ? juce::String ((int) (f / 1000)) + "k" : juce::String ((int) f), { x - 20, r.getBottom() + 2, 40, 12 }, cGreyD, 8.5f, juce::Justification::centred);
        }
        for (float db = 6; db < range; db += 6)
            label (g, "-" + juce::String ((int) db), { r.getX() - 30, yOfRed (db) - 6, 24, 12 }, cGreyD, 8.5f, juce::Justification::centredRight);

        // max cut: grey block
        const float maxCut = p.get ("maxCut");
        if (maxCut < range)
        {
            g.setColour (cGrey.withAlpha (0.45f)); g.fillRect (juce::Rectangle<float>::leftTopRightBottom (r.getX(), yOfRed (maxCut), r.getRight(), r.getBottom()));
            label (g, "max cut", { r.getRight() - 70, yOfRed (maxCut) + 2, 66, 12 }, cGreyD, 8.5f, juce::Justification::centredRight);
        }
        // input: small grey steps
        {
            g.setColour (cGrey);
            const int step = 4;
            for (int i = 0; i < Engine::kDisplayPoints; i += step)
            {
                float mx = -200; for (int j = i; j < i + step; ++j) mx = juce::jmax (mx, disp.input[(size_t) j]);
                const float x = r.getX() + r.getWidth() * (float) i / Engine::kDisplayPoints;
                const float y = r.getBottom() - r.getHeight() * juce::jlimit (0.0f, 1.0f, (mx + 100.0f) / 100.0f);
                g.fillRect (x, y, r.getWidth() * step / Engine::kDisplayPoints, 2.0f);
            }
        }
        // reduction: stepped orange gradient blocks hanging from the top (R/S yellow behind, L/M orange in front)
        const bool two = p.numMainChannels() == 2;
        const int perCol = juce::jmax (1, (int) std::round (Engine::kDisplayPoints * 5.0f / r.getWidth()));
        for (int c = two ? 1 : 0; c >= 0; --c)
            for (int i = 0; i < Engine::kDisplayPoints; i += perCol)
            {
                float v = 0; for (int j = i; j < juce::jmin (Engine::kDisplayPoints, i + perCol); ++j) v = juce::jmax (v, peak[c][(size_t) j]);
                if (v < 0.25f) continue;
                v = std::ceil (v / 1.5f) * 1.5f; // steps
                const float x = r.getX() + r.getWidth() * (float) i / Engine::kDisplayPoints + (c ? 3.0f : 0.0f);
                const float w = r.getWidth() * (float) perCol / Engine::kDisplayPoints + 0.5f;
                const auto block = juce::Rectangle<float>::leftTopRightBottom (x, r.getY(), juce::jmin (r.getRight(), x + w), yOfRed (v));
                const juce::Colour a = c == 0 ? cOrange : cYellow;
                g.setGradientFill (juce::ColourGradient (a, block.getX(), r.getY(), cWhite.withAlpha (0.4f), block.getRight() + w * 0.8f, r.getY(), false));
                g.fillRect (block);
            }
        g.setColour (cInk); g.fillRect (r.getX(), r.getY(), r.getWidth(), 2.0f);

        // depth curve: dotted
        const auto s = p.currentSettings();
        auto dots = [&] (int ch, juce::Colour col, float size)
        {
            g.setColour (col);
            for (float x = r.getX(); x <= r.getRight(); x += 6.0f)
            {
                const float v = depthCurve (s, fOf (x), ch) / (ch == 0 ? juce::jlimit (0.0001f, 1.0f, 1.0f - s.focus) : juce::jlimit (0.0001f, 1.0f, 1.0f + s.focus));
                const float y = juce::jlimit (r.getY(), r.getBottom(), yOfGain (20.0f * std::log10 (juce::jmax (1.0e-4f, v))));
                g.fillRect (x - size * 0.5f, y - size * 0.5f, size, size);
            }
        };
        bool split = false;
        for (const auto& b : s.bands) split = split || (b.used && b.on && std::abs (b.focus) > 0.001f);
        g.setColour (cGrey);
        for (float x = r.getX(); x < r.getRight(); x += 12.0f) g.fillRect (x, curveMid() - 0.5f, 5.0f, 1.0f);
        if (split && two) dots (1, cGreyD, 3.0f);
        dots (0, cTri, 3.5f);
        // more / less markers
        triangle (g, { r.getX() + 10, yOfGain (12) - 8 }, 10.0f, -juce::MathConstants<float>::halfPi, cTri);
        label (g, "more", { r.getX() + 18, yOfGain (12) - 12, 50, 12 }, cInk, 9.0f);
        triangle (g, { r.getX() + 10, yOfGain (-12) + 8 }, 10.0f, juce::MathConstants<float>::halfPi, cTri);
        label (g, "less", { r.getX() + 18, yOfGain (-12), 50, 12 }, cInk, 9.0f);

        // listen region
        const int listen = (int) p.get ("listen") - 1;
        if (listen >= 0 && band (listen).used)
        {
            const auto b = band (listen);
            g.setColour (cPink.withAlpha (0.35f)); g.fillRect (juce::Rectangle<float>::leftTopRightBottom (xOf (b.freq / 1.5f), r.getY(), xOf (b.freq * 1.5f), r.getBottom()));
        }
        // nodes: square-in-square in the band's colour
        for (int i = 0; i < kBands; ++i)
        {
            const auto b = band (i);
            if (! b.used) continue;
            const auto pt = nodePos (i);
            const bool sel = selected.count (i) > 0;
            const float rad = sel ? 10.0f : 8.5f;
            const juce::Rectangle<float> sq (pt.x - rad, pt.y - rad, rad * 2, rad * 2);
            g.setColour (kBandCol[i]); g.fillRect (sq);
            g.setColour (cWhite); g.fillRect (sq.reduced (rad * 0.28f));
            if (b.on) { g.setColour (kBandCol[i]); g.fillRect (sq.reduced (rad * 0.55f)); }
            if (sel) { g.setColour (cInk); g.drawRect (sq.expanded (2.0f), 1.5f); triangle (g, { pt.x, sq.getY() - 4.0f }, 8.0f, juce::MathConstants<float>::halfPi, cTri, false); }
            label (g, juce::String (i + 1), { sq.getRight() + 2, sq.getY() - 4, 14, 10 }, cInk, 8.0f);
        }
        if (drag == Box)
        {
            g.setColour (cInk.withAlpha (0.6f));
            const auto d = boxB - boxA; const int steps = juce::jlimit (1, 20, (int) (d.getDistanceFromOrigin() / 14.0f));
            for (int k = 0; k <= steps; ++k) { const auto c = boxA + d * ((float) k / (float) steps); g.drawRect (c.x - 6, c.y - 6, 12.0f, 12.0f, 1.0f); }
            g.drawRect (juce::Rectangle<float> (boxA, boxB), 1.0f);
        }
        if (showFreq && r.contains (mouse))
            label (g, hzStr (fOf (mouse.x)), { mouse.x + 10, mouse.y - 16, 80, 12 }, cInk, 8.5f);
        label (g, comma ("range " + juce::String ((int) range) + " db"), { r.getX() + 4, r.getBottom() - 14, 120, 12 }, cGreyD, 8.5f);
    }

    void createBand (juce::Point<float> pt, int type)
    {
        int slot = -1;
        for (int i = 0; i < kBands; ++i) if (! band (i).used) { slot = i; break; }
        if (slot < 0) return;
        if (pendingType >= 0) { type = pendingType; pendingType = -1; }
        p.undo.beginNewTransaction();
        p.resetBand (slot);
        setP (slot, "type", (float) type);
        setP (slot, "freq", fOf (pt.x));
        setP (slot, "gain", typeHasGain (type) ? juce::jlimit (-24.0f, 24.0f, (curveMid() - pt.y) / curveScale()) : 0.0f);
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
    void select (int i) { selected = { i }; focusBand = i; if (onSelect) onSelect(); repaint(); }

    void shapeMenu (juce::Point<float> pt)
    {
        juce::PopupMenu m;
        m.addSectionHeader ("add band at " + hzStr (fOf (pt.x)));
        for (int t = 0; t < kNumTypes; ++t) m.addItem (t + 1, juce::String (bandTypeName (t)).toLowerCase());
        m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (juce::Rectangle<int> (1, 1).withPosition (localPointToGlobal (pt.toInt()))),
                         [this, pt] (int r) { if (r > 0) { pendingType = -1; createBand (pt, r - 1); } repaint(); });
    }
    void nodeMenu (int i)
    {
        const auto b = band (i);
        juce::PopupMenu m, types;
        for (int t = 0; t < kNumTypes; ++t) types.addItem (100 + t, juce::String (bandTypeName (t)).toLowerCase(), true, b.type == t);
        m.addSubMenu ("shape", types);
        m.addItem (1, "enabled", true, b.on);
        m.addItem (2, "listen", true, (int) p.get ("listen") - 1 == i);
        m.addItem (4, "reset focus");
        m.addSeparator();
        m.addItem (3, "remove");
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

    void mouseMove (const juce::MouseEvent& e) override { mouse = e.position; repaint(); }
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
        drag = Node; starts.clear();
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

// eight band rings with colour tabs: click selects, double-click enables / bypasses
struct Rings : juce::Component, juce::SettableTooltipClient
{
    Graph& graph; QuellProcessor& p;
    Rings (Graph& g, QuellProcessor& pr) : graph (g), p (pr) { setTooltip ("bands 1-8: click selects, double-click turns on / off"); }
    juce::Rectangle<float> slot (int i) const { const float w = (float) getWidth() / kBands; return { w * (float) i, 0.0f, w, (float) getHeight() }; }
    int at (juce::Point<float> pt) const { return juce::jlimit (0, kBands - 1, (int) (pt.x / ((float) getWidth() / kBands))); }
    void paint (juce::Graphics& g) override
    {
        for (int i = 0; i < kBands; ++i)
        {
            const auto b = graph.band (i);
            const auto s = slot (i);
            g.setColour (b.used ? kBandCol[i] : cGrey.withAlpha (0.5f)); g.fillRect (juce::Rectangle<float> (s.getX() + 6, 0, s.getWidth() - 12, 7));
            const float d = juce::jmin (s.getWidth() - 6.0f, s.getHeight() - 10.0f);
            const auto rr = juce::Rectangle<float> (d, d).withCentre ({ s.getCentreX(), 10.0f + d * 0.5f });
            ring (g, rr, b.used && b.on, graph.focusBand == i);
            if (! b.used) { g.setColour (cRed.withAlpha (0.55f)); g.fillEllipse (rr); }
        }
    }
    void mouseDown (const juce::MouseEvent& e) override { const int i = at (e.position); if (graph.band (i).used) graph.select (i); repaint(); }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const int i = at (e.position);
        if (graph.band (i).used) { p.undo.beginNewTransaction(); graph.toggleOn (i); repaint(); }
    }
};

//==================================================================================================
struct Content : juce::Component
{
    QuellProcessor& p;
    Graph graph;
    Rings rings;
    Grid gFactory { 0, 11 }, gUser { 1, 23 }, gShape { 2, 37 }, gRemove { 3, 41 };
    std::vector<std::unique_ptr<Ctl>> ctls;
    std::vector<std::unique_ptr<Btn>> btns;
    std::vector<Ctl*> mainCtl, side;
    Ctl *mixCtl = nullptr, *outCtl = nullptr;
    std::unique_ptr<Ctl> bFreq, bGain, bQ, bFocus;
    Btn *bType = nullptr, *bOn = nullptr, *bListen = nullptr;
    std::map<juce::String, Btn*> named;
    int bandShown = -2;
    bool help = false;
    juce::String status;
    std::function<void (float)> setScale;
    juce::Rectangle<int> pill, play, meterArea, crescent;

    Ctl* mk (const juce::String& id, const juce::String& name, Ctl::Style st, const juce::String& tip)
    {
        ctls.push_back (std::make_unique<Ctl> (p, id, name, st));
        auto* c = ctls.back().get(); addAndMakeVisible (c); c->setTooltip (tip); return c;
    }
    Btn* add (const juce::String& key, std::unique_ptr<Btn> b, const juce::String& tip)
    {
        btns.push_back (std::move (b)); auto* r = btns.back().get(); addAndMakeVisible (r); r->setTooltip (tip); named[key] = r; return r;
    }
    Btn* addFn (const juce::String& key, Btn::Painter pt, std::function<bool()> lit, std::function<void (const juce::MouseEvent&)> c, const juce::String& tip)
    {
        return add (key, std::make_unique<Btn> (std::move (pt), std::move (lit), std::move (c)), tip);
    }

    explicit Content (QuellProcessor& pr) : p (pr), graph (pr), rings (graph, pr)
    {
        addAndMakeVisible (graph);
        addAndMakeVisible (rings);
        graph.setTooltip ("double-click adds a band. drag nodes. right-click: shapes. scroll: q");
        graph.onSelect = [this] { refreshStrip(); rings.repaint(); };

        // grids
        for (auto* g : { &gFactory, &gUser, &gShape, &gRemove }) addAndMakeVisible (g);
        gFactory.count = [] { return (int) factoryPresets().size(); };
        gFactory.name = [] (int i) { return "preset: " + juce::String (factoryPresets()[(size_t) i].name).toLowerCase(); };
        gFactory.current = [this] (int i) { return p.presetName == factoryPresets()[(size_t) i].name; };
        gFactory.pick = [this] (int i) { p.loadFactory (i); graph.selected.clear(); refreshStrip(); repaint(); };
        gUser.count = [this] { return userFiles().size() + 1; };
        gUser.name = [this] (int i) { auto f = userFiles(); return i < f.size() ? "my preset: " + f[i].getFileNameWithoutExtension() : juce::String ("... and store configurations here."); };
        gUser.current = [this] (int i) { auto f = userFiles(); return i < f.size() && p.presetName == f[i].getFileNameWithoutExtension(); };
        gUser.pick = [this] (int i) { auto f = userFiles(); if (i < f.size()) p.loadPresetFile (f[i]); else saveDialog(); graph.selected.clear(); refreshStrip(); repaint(); };
        gShape.count = [] { return (int) kNumTypes; };
        gShape.name = [this] (int i) { return "shape: " + juce::String (bandTypeName (i)).toLowerCase() + (graph.selected.empty() ? " (for the next band)" : ""); };
        gShape.current = [this] (int i) { return graph.focusBand >= 0 ? (int) p.get (bandId (graph.focusBand, "type")) == i : graph.pendingType == i; };
        gShape.pick = [this] (int i)
        {
            if (graph.selected.empty()) { graph.pendingType = graph.pendingType == i ? -1 : i; return; }
            p.undo.beginNewTransaction();
            for (int s : graph.selected) graph.setP (s, "type", (float) i);
            refreshStrip();
        };
        gRemove.count = [] { return (int) kBands; };
        gRemove.name = [this] (int i) { return graph.band (i).used ? "remove band " + juce::String (i + 1) : "band " + juce::String (i + 1) + " is empty"; };
        gRemove.current = [this] (int i) { return graph.band (i).used; };
        gRemove.pick = [this] (int i) { if (graph.band (i).used) graph.deleteBand (i); };

        // undo / redo
        addFn ("undo", [] (juce::Graphics& g, juce::Rectangle<float> r, bool, bool over)
        {
            const auto c = r.withWidth (r.getHeight()).reduced (1);
            g.setColour (cWhite); g.fillEllipse (c); g.setColour (over ? cTri : cGreyD); g.drawEllipse (c.reduced (4), 4.0f);
            g.setColour (cWhite); g.fillRect (c.getCentreX(), c.getY(), c.getWidth() * 0.5f, c.getHeight() * 0.5f);
            label (g, "undo", r.withTrimmedLeft (c.getWidth() + 3), cWhite, 9.0f);
        }, nullptr, [this] (const juce::MouseEvent&) { p.undo.undo(); }, "undo (cmd-z)");
        addFn ("redo", [] (juce::Graphics& g, juce::Rectangle<float> r, bool, bool over)
        {
            label (g, "redo", r, over ? cYellow : cWhite, 9.0f);
        }, nullptr, [this] (const juce::MouseEvent&) { p.undo.redo(); }, "redo (shift-cmd-z)");

        // mode: the gold bar
        addFn ("mode", [this] (juce::Graphics& g, juce::Rectangle<float> r, bool, bool)
        {
            g.setColour (cBrown); g.fillRect (r);
            g.setColour (cWhite); g.drawRect (r.withWidth (r.getHeight()).reduced (5), 2.0f);
            const bool hard = p.get ("mode") > 0.5f;
            auto t = r.withTrimmedLeft (r.getHeight() + 2);
            g.setFont (fnt (11.0f));
            const juce::String a ("soft"), b (" / "), c ("hard");
            const float wa = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), a), wb = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), b);
            g.setColour (hard ? cWhite.withAlpha (0.45f) : cWhite); g.drawText (a, t, juce::Justification::centredLeft);
            g.setColour (cWhite.withAlpha (0.7f)); g.drawText (b, t.withTrimmedLeft (wa), juce::Justification::centredLeft);
            g.setColour (hard ? cWhite : cWhite.withAlpha (0.45f)); g.drawText (c, t.withTrimmedLeft (wa + wb), juce::Justification::centredLeft);
            label (g, hard ? "level" : "adaptive", t.withTrimmedRight (8), cWhite, 8.5f, juce::Justification::centredRight);
        }, [this] { return p.get ("mode") > 0.5f; }, [this] (const juce::MouseEvent&)
        {
            p.undo.beginNewTransaction(); p.set ("mode", p.get ("mode") > 0.5f ? 0.0f : 1.0f);
        }, "soft: adaptive, follows tonal changes. hard: level-dependent, firmer");

        // top right orange blocks
        add ("a", std::make_unique<Btn> (orangeBlock ([] { return juce::String ("a"); }), [this] { return p.abSlot == 0; }, [this] (const juce::MouseEvent&) { p.switchAB (0); }), "state a");
        add ("b", std::make_unique<Btn> (orangeBlock ([] { return juce::String ("b"); }), [this] { return p.abSlot == 1; }, [this] (const juce::MouseEvent&) { p.switchAB (1); }), "state b");
        add ("copy", std::make_unique<Btn> (orangeBlock ([this] { return juce::String (p.abSlot == 0 ? "a > b" : "b > a"); }), nullptr, [this] (const juce::MouseEvent&) { p.copyAB(); }), "copy this state to the other slot");
        add ("sc", paramToggle (p, "sidechain", orangeBlock ([] { return juce::String ("sc"); })), "external sidechain: reduction follows the key input");
        add ("scl", paramToggle (p, "scListen", orangeBlock ([this] { return juce::String (p.sidechainConnected() ? "listen" : "listen (no input)"); })), "listen to the sidechain input");
        add ("settings", std::make_unique<Btn> (orangeBlock ([] { return juce::String ("settings"); }), nullptr, [this] (const juce::MouseEvent&) { settingsMenu(); }), "settings, window size, reset");

        // main controls on the bottom rail
        mainCtl.push_back (mk ("depth", "depth", Ctl::Rail, "amount of reduction, shaped by the depth curve"));
        mainCtl.push_back (mk ("detail", "detail", Ctl::Rail, "width of the cuts: higher = narrower, deeper"));
        mainCtl.push_back (mk ("attack", "attack", Ctl::Rail, "reaction: higher = slower, lets transients through"));
        mainCtl.push_back (mk ("release", "release", Ctl::Rail, "recovery: higher = slower, steadier"));

        // side panel
        add ("lr", paramChoice (p, "stereoMode", 0, orangeBlock ([] { return juce::String ("l/r"); })), "process left and right");
        add ("ms", paramChoice (p, "stereoMode", 1, orangeBlock ([] { return juce::String ("m/s"); })), "process mid and side");
        side.push_back (mk ("link", "link", Ctl::Side, "0% each channel alone, 100% summed analysis"));
        side.push_back (mk ("focus", "focus", Ctl::Side, "shift processing toward l/m or r/s"));
        side.push_back (mk ("detailTiltLo", "detail low", Ctl::Side, "scales detail below ~500 hz"));
        side.push_back (mk ("detailTiltHi", "detail high", Ctl::Side, "scales detail above ~2 khz"));
        side.push_back (mk ("attackTiltLo", "attack low", Ctl::Side, "scales attack below ~500 hz"));
        side.push_back (mk ("attackTiltHi", "attack high", Ctl::Side, "scales attack above ~2 khz"));
        side.push_back (mk ("releaseTiltLo", "release low", Ctl::Side, "scales release below ~500 hz"));
        side.push_back (mk ("releaseTiltHi", "release high", Ctl::Side, "scales release above ~2 khz"));
        side.push_back (mk ("maxCut", "max cut", Ctl::Side, "limit on the reduction (grey area)"));
        side.push_back (mk ("wetTrim", "wet trim", Ctl::Side, "level of the processed signal before mix"));

        // rail squares: bypass (black dot) and delta (red dot)
        add ("bypass", paramToggle (p, "bypass", [] (juce::Graphics& g, juce::Rectangle<float> r, bool on, bool) { dotCircle (g, r, on); }), "bypass (keeps processing, for comparing)");
        add ("delta", paramToggle (p, "delta", [] (juce::Graphics& g, juce::Rectangle<float> r, bool on, bool) { dotCircle (g, r, on, cTri); }), "delta: hear only what is removed");

        // footer
        mixCtl = mk ("mix", "mix", Ctl::Line, "wet / dry");
        outCtl = mk ("outGain", "out", Ctl::Line, "output gain after mix");
        outCtl->seg = cLilac;
        const juce::StringArray qn { "normal", "high", "ultra" };
        for (int q = 0; q < 3; ++q)
            add ("q" + juce::String (q), paramChoice (p, "quality", q, gradIcon (qn[q], q == 1)), "quality: time resolution (" + qn[q] + ")");
        add ("linear", paramToggle (p, "linear", gradIcon ("linear", true)), "linear phase (more latency)");
        add ("lowlat", paramToggle (p, "lowLatency", gradIcon ("0 lat.", false)), "low latency: no lookahead, minimum phase");
        addFn ("size", gradIcon ("size", true), nullptr, [this] (const juce::MouseEvent&)
        {
            if (setScale) { const float s = (float) getTopLevelComponent()->getWidth() / QuellEditor::baseW; setScale (s < 0.95f ? 1.0f : s < 1.2f ? 1.25f : s < 1.45f ? 1.5f : 0.8f); }
        }, "window size: s / m / l / xl");

        // help
        addFn ("help", [this] (juce::Graphics& g, juce::Rectangle<float> r, bool, bool over)
        {
            label (g, help ? "end help" : "help", r, over ? cWhite : (help ? cWhite : cYellow), 11.0f);
        }, nullptr, [this] (const juce::MouseEvent&) { help = ! help; repaint(); }, "show what everything does");
        addFn ("q?", [] (juce::Graphics& g, juce::Rectangle<float> r, bool, bool)
        {
            for (int i = 0; i < 3; ++i)
            {
                auto c = juce::Rectangle<float> (r.getX() + (float) i * (r.getHeight() + 2), r.getY(), r.getHeight(), r.getHeight());
                if (i == 0) checker (g, c, c.getWidth() / 3, cOrange, cYellow); else { g.setColour (i == 1 ? cGrey : cGreyD); g.fillRect (c); }
                label (g, "?", c, i == 2 ? cWhite : cInk, 11.0f, juce::Justification::centred);
            }
        }, nullptr, [this] (const juce::MouseEvent&) { help = ! help; repaint(); }, "help");

        // band strip
        bType = addFn ("type", [this] (juce::Graphics& g, juce::Rectangle<float> r, bool, bool)
        {
            checker (g, r, r.getHeight() / 3, cOrange, cYellow);
            const int i = graph.focusBand;
            const juce::String t = i >= 0 ? juce::String (bandTypeName ((int) p.get (bandId (i, "type")))).toLowerCase() + "..."
                                 : graph.pendingType >= 0 ? juce::String (bandTypeName (graph.pendingType)).toLowerCase() + "..." : juce::String ("shape...");
            g.setColour (cWhite.withAlpha (0.8f)); g.fillRect (r.reduced (4, 4).withWidth (juce::GlyphArrangement::getStringWidth (fnt (9.5f), t) + 8));
            label (g, t, r.reduced (8, 0), cInk, 9.5f);
        }, nullptr, [this] (const juce::MouseEvent&) { typeMenu(); }, "band shape");
        bOn = addFn ("on", [] (juce::Graphics& g, juce::Rectangle<float> r, bool on, bool) { dotCircle (g, r.withWidth (r.getHeight()), on); label (g, "on", r.withTrimmedLeft (r.getHeight() + 4), cWhite, 9.0f); },
            [this] { return graph.focusBand >= 0 && p.get (bandId (graph.focusBand, "on")) > 0.5f; },
            [this] (const juce::MouseEvent&) { if (graph.focusBand >= 0) { p.undo.beginNewTransaction(); graph.toggleOn (graph.focusBand); } }, "enable / bypass the band (its area is still processed)");
        bListen = addFn ("listen", [] (juce::Graphics& g, juce::Rectangle<float> r, bool on, bool) { ring (g, r.withWidth (r.getHeight()), on, on); label (g, "listen", r.withTrimmedLeft (r.getHeight() + 4), cWhite, 9.0f); },
            [this] { return graph.focusBand >= 0 && (int) p.get ("listen") - 1 == graph.focusBand; },
            [this] (const juce::MouseEvent&) { if (graph.focusBand >= 0) p.set ("listen", (int) p.get ("listen") - 1 == graph.focusBand ? 0.0f : (float) (graph.focusBand + 1)); }, "hear what this band's region removes");

        setSize (QuellEditor::baseW, QuellEditor::baseH);
        tick();
    }

    juce::Array<juce::File> userFiles() const
    {
        auto files = p.userPresetFolder().findChildFiles (juce::File::findFiles, false, "*.quell");
        files.sort();
        if (files.size() > 47) files.resize (47);
        return files;
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
                bFreq = std::make_unique<Ctl> (p, bandId (i, "freq"), "freq", Ctl::Rail);
                bGain = std::make_unique<Ctl> (p, bandId (i, "gain"), "depth", Ctl::Rail);
                bQ = std::make_unique<Ctl> (p, bandId (i, "q"), "q", Ctl::Rail);
                bFocus = std::make_unique<Ctl> (p, bandId (i, "focus"), "focus", Ctl::Rail);
                bFreq->setTooltip ("band frequency (type 6k3, 6.3k, 6300)");
                bGain->setTooltip ("boost = more suppression here, cut = less");
                bQ->setTooltip ("band width / slope");
                bFocus->setTooltip ("per-band stereo balance");
                for (auto* c : { bFreq.get(), bGain.get(), bQ.get(), bFocus.get() }) addAndMakeVisible (c);
            }
            for (auto* b : { bOn, bListen }) b->setVisible (i >= 0);
            layoutStrip();
        }
        if (i >= 0)
        {
            const int t = (int) p.get (bandId (i, "type"));
            if (bQ) bQ->name = typeUsesSlope (t) ? "slope" : "q";
            if (bGain) bGain->setVisible (typeHasGain (t));
        }
        bType->repaint();
        rings.repaint();
    }

    void typeMenu()
    {
        const int i = graph.focusBand;
        juce::PopupMenu m;
        for (int t = 0; t < kNumTypes; ++t) m.addItem (t + 1, juce::String (bandTypeName (t)).toLowerCase(), true, i >= 0 ? (int) p.get (bandId (i, "type")) == t : graph.pendingType == t);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (bType), [this, i] (int r)
        {
            if (r <= 0) return;
            if (i < 0) { graph.pendingType = r - 1; return; }
            p.undo.beginNewTransaction();
            for (int s : graph.selected.empty() ? std::set<int> { i } : graph.selected) graph.setP (s, "type", (float) (r - 1));
            refreshStrip();
        });
    }

    std::unique_ptr<juce::AlertWindow> alert;
    void saveDialog()
    {
        alert = std::make_unique<juce::AlertWindow> ("store configuration", "name:", juce::MessageBoxIconType::NoIcon, this);
        alert->addTextEditor ("name", p.presetName);
        alert->addButton ("store", 1, juce::KeyPress (juce::KeyPress::returnKey));
        alert->addButton ("cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        alert->enterModalState (true, juce::ModalCallbackFunction::create ([this] (int r)
        {
            if (r == 1 && alert != nullptr) { const auto n = alert->getTextEditorContents ("name").trim(); if (n.isNotEmpty()) p.saveUserPreset (n); }
            alert.reset(); repaint();
        }), false);
    }
    void settingsMenu()
    {
        juce::PopupMenu m, size;
        m.addItem (1, "always render on ultra (offline bounces)", true, p.get ("renderUltra") > 0.5f);
        m.addItem (2, "show frequency near cursor", true, graph.showFreq);
        for (int i = 0; i < 4; ++i) size.addItem (10 + i, juce::StringArray { "s", "m", "l", "xl" }[i]);
        m.addSubMenu ("window size", size);
        m.addSeparator();
        m.addItem (3, "reset to default");
        m.addItem (4, "show preset folder");
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (named["settings"]), [this] (int r)
        {
            if (r == 1) p.set ("renderUltra", p.get ("renderUltra") > 0.5f ? 0.0f : 1.0f);
            if (r == 2) graph.showFreq = ! graph.showFreq;
            if (r == 3) { p.loadFactory (0); graph.selected.clear(); refreshStrip(); }
            if (r == 4) { p.userPresetFolder().createDirectory(); p.userPresetFolder().revealToUser(); }
            if (r >= 10 && setScale) setScale (std::array<float, 4> { 0.8f, 1.0f, 1.25f, 1.5f }[(size_t) (r - 10)]);
        });
    }

    juce::String hoverText()
    {
        const auto mp = getMouseXYRelative();
        for (auto* g : { &gFactory, &gUser, &gShape, &gRemove })
            if (g->getBounds().contains (mp)) return g->hoverText();
        if (auto* c = getComponentAt (mp))
            for (auto* x = c; x != nullptr && x != this; x = x->getParentComponent())
                if (auto* t = dynamic_cast<juce::SettableTooltipClient*> (x)) { auto s = t->getTooltip(); if (s.isNotEmpty()) return s; }
        return {};
    }

    void tick()
    {
        graph.update();
        for (auto& c : ctls) c->refresh();
        for (auto* c : { bFreq.get(), bGain.get(), bQ.get(), bFocus.get() }) if (c) c->refresh();
        for (auto& b : btns) b->refresh();
        named["mode"]->repaint(); named["copy"]->repaint(); named["scl"]->repaint(); bType->repaint();
        for (auto* g : { &gFactory, &gUser, &gShape, &gRemove }) g->repaint();
        refreshStrip();
        auto h = hoverText();
        if (h.isEmpty()) h = p.presetName.toLowerCase() + " is loaded...";
        if (h != status) { status = h; repaint (pill.expanded (4)); }
        repaint (meterArea); repaint (crescent.withWidth (150));
    }

    // ---- painting --------------------------------------------------------------------------------
    void paint (juce::Graphics& g) override
    {
        g.fillAll (help ? cBlue : cRed);

        // right edge orange blocks
        g.setColour (cOrange);
        for (int y = 150; y < 560; y += 105) g.fillRect (getWidth() - 7, y, 7, 64);

        // status pill: blue striped capsule
        {
            auto r = pill.toFloat();
            juce::Graphics::ScopedSaveState ss (g);
            juce::Path clip; clip.addRoundedRectangle (r, r.getHeight() * 0.5f);
            g.setColour (cNavy); g.fillPath (clip);
            g.reduceClipRegion (clip);
            g.setColour (cRing);
            for (float x = r.getX(); x < r.getRight(); x += 4) g.fillRect (x, r.getY(), 2.0f, r.getHeight());
            g.setColour (cNavy); g.fillRect (r.reduced (r.getHeight() * 0.5f, 3));
            label (g, status, r.reduced (r.getHeight() * 0.6f, 0), cWhite, 9.0f);
        }
        // the four symbol circles after the pill
        for (int i = 0; i < 4; ++i)
        {
            const juce::Rectangle<float> c ((float) pill.getRight() + 6 + (float) i * 22, (float) pill.getY() - 1, 20, 20);
            g.setColour (cInk); g.fillEllipse (c); g.setColour (cBrown); g.drawEllipse (c, 1.5f);
            symbol (g, i, c.reduced (2), cWhite);
        }

        // white play area with a wavy bottom left, then the grey rails
        {
            const auto r = play.toFloat();
            juce::Path s;
            s.startNewSubPath (r.getX(), r.getY() + 10);
            s.lineTo (r.getX() + 20, r.getY());
            s.lineTo (r.getRight(), r.getY());
            s.lineTo (r.getRight(), r.getBottom());
            s.lineTo (r.getX() + 300, r.getBottom());
            s.cubicTo (r.getX() + 230, r.getBottom(), r.getX() + 190, r.getBottom() + 30, r.getX() + 100, r.getBottom() + 24);
            s.lineTo (r.getX() - 8, r.getBottom() + 20);
            s.closeSubPath();
            g.setColour (cWhite); g.fillPath (s);
            g.setColour (cGrey);
            g.fillRect (r.getX() + 70, r.getY() - 8, r.getWidth() - 50, 14.0f);         // top rail
            g.fillRect (r.getRight() + 6, r.getY() - 8, 14.0f, 340.0f);                   // right rail
            g.fillRect (r.getX() + 440, r.getBottom() + 14, r.getWidth() - 420, 12.0f);   // rail to the right rail
            for (float x = r.getX() + 300; x < r.getX() + 430; x += 26) g.fillRect (x, r.getBottom() + 14, 16.0f, 12.0f); // holes
            g.fillRect (r.getX() - 4, r.getY() + 40, 12.0f, 40.0f);
            g.fillRect (r.getX() - 4, r.getY() + 170, 12.0f, 50.0f);
        }

        // left side: pixel bracket, reduction meters as a lilac bracket + bars, the name
        {
            g.setColour (cWhite);
            const int bx = 28, by = 190, px = 6;
            const int pts[][2] = { {0,0},{1,0},{2,1},{2,2},{2,3},{3,4},{4,5},{3,6},{2,7},{2,8},{2,9},{1,10},{0,10} };
            for (auto& q : pts) g.fillRect (bx + q[0] * px, by + q[1] * px, px, px);
        }
        {
            auto m = meterArea.toFloat();
            g.setColour (cLilac); g.setFont (serif (110.0f));
            g.drawText ("(", m.withWidth (36).translated (-6, -8), juce::Justification::centred);
            const int nm = p.numMainChannels();
            const bool ms = p.get ("stereoMode") > 0.5f;
            for (int c = 0; c < nm; ++c)
            {
                const auto bar = juce::Rectangle<float> (m.getX() + 30 + (float) c * 22, m.getY() + 10, 18, m.getHeight() - 24);
                g.setColour (cLilac.withAlpha (0.35f)); g.fillRect (bar);
                const float lv = juce::jlimit (0.0f, 1.0f, p.meter (c) / 24.0f);
                g.setColour (cLilac); g.fillRect (bar.withHeight (bar.getHeight() * lv));
                label (g, nm == 1 ? "gr" : (ms ? (c ? "s" : "m") : (c ? "r" : "l")), bar.withY (bar.getBottom() + 1).withHeight (12), cWhite, 8.5f, juce::Justification::centred);
            }
        }
        {
            const auto f = serif (36.0f);
            g.setFont (f); g.setColour (cInk);
            g.drawText ("que", 10, 410, 80, 36, juce::Justification::centredLeft);
            g.setColour (help ? cTri : cWhite);
            g.drawText ("ll", 10 + (int) juce::GlyphArrangement::getStringWidth (f, "que"), 410, 40, 36, juce::Justification::centredLeft);
        }

        if (graph.focusBand < 0)
            label (g, "... select bands here.", juce::Rectangle<float> ((float) rings.getRight() + 12, (float) rings.getY() + 14, 200, 16), cWhite, 9.0f);

        // footer: crescent + latency
        {
            auto c = crescent.toFloat();
            const auto disc = c.withWidth (c.getHeight());
            juce::Path cr;
            cr.addPieSegment (disc, juce::MathConstants<float>::pi * 1.0f, juce::MathConstants<float>::pi * 2.0f, 0.62f);
            g.setGradientFill (juce::ColourGradient (cOrange, disc.getX(), disc.getCentreY(), cWhite, disc.getCentreX(), disc.getCentreY(), false));
            g.fillPath (cr);
            label (g, juce::String (p.getLatencySamples()) + " smp.", c.withTrimmedLeft (c.getHeight() * 0.55f).withWidth (100), cWhite, 10.5f);
        }
    }

    void paintOverChildren (juce::Graphics& g) override
    {
        if (! help) return;
        // the help layer: grey label bars with dotted guides
        auto dotsTo = [&] (juce::Point<float> a, juce::Point<float> b)
        {
            g.setColour (cOrange);
            const auto d = b - a; const int n = juce::jmax (1, (int) (d.getDistanceFromOrigin() / 9.0f));
            for (int i = 0; i <= n; ++i) { const auto q = a + d * ((float) i / (float) n); g.fillRect (q.x - 1.5f, q.y - 1.5f, 3.0f, 3.0f); }
        };
        const auto gr = graph.getBounds().toFloat();
        greyBar (g, { gr.getX() - 20, gr.getCentreY() - 46, 260, 18 }, "double-click adds a band");
        greyBar (g, { gr.getRight() - 250, gr.getCentreY() + 26, 270, 18 }, "drag nodes (cmd: q, alt: listen)", juce::Justification::centredLeft);
        greyBar (g, { gr.getCentreX() - 110, gr.getY() + 46, 220, 18 }, "orange = what is removed", juce::Justification::centred);
        greyBar (g, { gr.getCentreX() - 150, gr.getBottom() - 48, 300, 18 }, "red dots: depth curve. right-click: shapes", juce::Justification::centred);
        dotsTo ({ gr.getCentreX(), gr.getY() + 44 }, { gr.getCentreX(), gr.getY() + 10 });
        for (auto [rr, t] : { std::pair { gFactory.getBounds(), "click presets" }, std::pair { gUser.getBounds(), "store here" },
                              std::pair { gShape.getBounds(), "pick shapes" }, std::pair { gRemove.getBounds(), "remove bands" } })
            greyBar (g, rr.toFloat().withY ((float) rr.getBottom() - 18).withHeight (18).reduced (6, 0), t, juce::Justification::centred);
        const auto rl = mainCtl[1]->getBounds().toFloat();
        greyBar (g, { rl.getX(), rl.getY() - 24, 190, 18 }, "drag to set, alt-click resets");
        const auto rg = rings.getBounds().toFloat();
        greyBar (g, { 556, 545, 252, 14 }, "... shape the selected band below.");
        const auto sd = side.front()->getBounds().toFloat();
        greyBar (g, { sd.getX() - 120, sd.getY() + 4, 110, 18 }, "stereo, tilt");
        triangle (g, { sd.getX() - 4, sd.getY() + 13 }, 10, 0, cTri);
        const auto mx = mixCtl->getBounds().toFloat();
        greyBar (g, { 716, 614, 84, 14 }, "mix + out"); triangle (g, { 704, 621 }, 10, juce::MathConstants<float>::pi, cTri);
        juce::ignoreUnused (mx);
        const auto qq = named["q0"]->getBounds().toFloat();
        greyBar (g, { 716, 588, 84, 14 }, "quality"); triangle (g, { qq.getX() - 2, 595 }, 10, 0, cTri);
        for (auto [key, t] : { std::pair { "bypass", "bypass" }, std::pair { "delta", "delta" } })
        {
            const auto b = named[key]->getBounds().toFloat();
            greyBar (g, { b.getX() - 96, b.getY() + 4, 80, 18 }, t);
            triangle (g, { b.getX() - 4, b.getCentreY() }, 10, 0, cTri);
        }
    }

    void layoutStrip()
    {
        int x = 100;
        for (auto* c : { bFreq.get(), bGain.get(), bQ.get(), bFocus.get() })
        {
            if (c) c->setBounds (x, 560, 160, 22);
            x += 172;
        }
    }

    void resized() override
    {
        int gx = 200;
        for (auto* g : { &gFactory, &gUser, &gShape, &gRemove }) { g->setBounds (gx, 4, 136, 102); gx += 146; }
        pill = { 90, 116, 300, 18 };
        named["undo"]->setBounds (500, 113, 60, 24);
        named["redo"]->setBounds (564, 113, 40, 24);
        named["mode"]->setBounds (612, 113, 176, 24);
        named["a"]->setBounds (812, 8, 46, 30); named["b"]->setBounds (862, 8, 46, 30); named["copy"]->setBounds (912, 8, 58, 30);
        named["sc"]->setBounds (812, 46, 46, 26); named["scl"]->setBounds (862, 46, 108, 26);
        named["settings"]->setBounds (812, 80, 158, 24);
        play = { 96, 152, 690, 314 };
        graph.setBounds (play.getX() + 12, play.getY() + 12, play.getWidth() - 20, play.getHeight() - 26);
        named["bypass"]->setBounds (play.getRight() - 6, 392, 26, 26);
        named["delta"]->setBounds (play.getRight() - 6, 428, 26, 26);
        int x = 100;
        for (auto* c : mainCtl) { c->setBounds (x, 476, 160, 24); x += 172; }
        bType->setBounds (100, 518, 120, 24);
        rings.setBounds (240, 508, 300, 42);
        bOn->setBounds (560, 518, 60, 24);
        bListen->setBounds (628, 518, 90, 24);
        layoutStrip();
        named["lr"]->setBounds (818, 150, 60, 22); named["ms"]->setBounds (884, 150, 60, 22);
        int y = 184;
        for (auto* c : side) { c->setBounds (818, y, 152, 32); y += 37; }
        meterArea = { 18, 286, 76, 112 };
        named["q?"]->setBounds (14, 452, 60, 16);
        named["help"]->setBounds (14, 472, 80, 18);
        crescent = { 96, 590, 150, 44 };
        mixCtl->setBounds (260, 594, 210, 40);
        outCtl->setBounds (490, 594, 210, 40);
        named["q0"]->setBounds (806, 570, 56, 34); named["q1"]->setBounds (862, 570, 56, 34); named["q2"]->setBounds (918, 570, 56, 34);
        named["linear"]->setBounds (806, 605, 56, 34); named["lowlat"]->setBounds (862, 605, 56, 34); named["size"]->setBounds (918, 605, 56, 34);
    }
};
} // namespace

//==================================================================================================
QuellEditor::QuellEditor (QuellProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    look.reset (new Look());
    setLookAndFeel (look.get());
    auto* c = new Content (p);
    content.reset (c);
    addAndMakeVisible (*content);
    c->setScale = [this] (float s) { setScale (s); };
    setResizable (true, true);
    constrainer.setFixedAspectRatio ((double) baseW / baseH);
    constrainer.setSizeLimits (baseW * 7 / 10, baseH * 7 / 10, baseW * 2, baseH * 2);
    setConstrainer (&constrainer);
    const int w = p.uiWidth > 0 ? p.uiWidth : baseW, h = p.uiHeight > 0 ? p.uiHeight : baseH;
    setSize (w, h);
    setWantsKeyboardFocus (true);
    startTimerHz (30);
}

QuellEditor::~QuellEditor() { stopTimer(); content.reset(); setLookAndFeel (nullptr); }

void QuellEditor::setScale (float s) { setSize (juce::roundToInt (baseW * s), juce::roundToInt (baseH * s)); }

void QuellEditor::paint (juce::Graphics& g) { g.fillAll (cRed); }

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

void QuellEditor::showState (bool help, int selectBand)
{
    auto* c = static_cast<Content*> (content.get());
    c->help = help;
    if (selectBand >= 0) c->graph.select (selectBand);
    c->repaint();
}

void QuellEditor::timerCallback() { static_cast<Content*> (content.get())->tick(); }

juce::AudioProcessorEditor* QuellProcessor::createEditor() { return new QuellEditor (*this); }
