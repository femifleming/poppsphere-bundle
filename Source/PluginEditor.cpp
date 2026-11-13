#include "PluginEditor.h"
#include <cmath>

namespace
{
const juce::Colour kBlue (0xff3478d4), kBlueTop (0xff3c88e2), kBlueBottom (0xff2c5fc0);
const juce::Colour kFrame (0xffc9cad3), kFrameDark (0xffb5b6c2), kInk (0xff1e1e1e);
const juce::Colour kRed (0xffd42a24), kMustard (0xffc4a232), kOrange (0xffe08a1e), kYellow (0xfff2d41f);
const juce::Colour kTag (0xffc8c9d1);
constexpr float kPi = juce::MathConstants<float>::pi;

const char* kToolNames[6] = { "load sounds", "built-in sounds", "skip again", "capture input", "render to file", "clear mix" };
const juce::uint32 kBracketColours[8] = { 0xff6878d8, 0xff2a3ea8, 0xff4a3aa8, 0xff2a4ac0,
                                          0xff7a58c8, 0xff3048b8, 0xff8090e0, 0xff2438a0 };
const char* kBracketGlyphs[8] = { "}", "}", ")", "}", "(", "[", "o", "[" };
const bool kBracketPixel[8] = { true, false, false, true, false, false, false, true };

// demo script: 0 cell->mix, 1 effect->sound, 2 op length, 3 op position, 4 preset, 5 snapshot,
// 6 reel->timeline, 7 throw away, 8 play/stop timeline, 9 wait
const int kDemo[] = { 4, 0, 0, 1, 2, 3, 5, 6, 0, 1, 7, 3, 5, 6, 8, 9, 9, 8, 7, 7, 4, 2, 5, 6 };
constexpr int kDemoLen = (int) (sizeof (kDemo) / sizeof (kDemo[0]));
constexpr juce::uint32 kDemoStepMs = 1700;

float ease (float t) { t = juce::jlimit (0.0f, 1.0f, t); return t * t * (3.0f - 2.0f * t); }
} // namespace

OpEditor::OpEditor (OpProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    setOpaque (true);
    setResizable (true, true);
    setResizeLimits (600, 432, 1600, 1152);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) W / H);
    setSize (W, H);
    lastInput = juce::Time::getMillisecondCounter();
    timerCallback();
    startTimerHz (30);
}

OpEditor::~OpEditor() { stopTimer(); }

juce::Font OpEditor::labelFont (float size)
{
    return juce::Font (juce::FontOptions ("Verdana", size, juce::Font::bold)).withExtraKerningFactor (0.07f);
}

juce::Colour OpEditor::reelColour (int c)
{
    static const juce::uint32 cols[6] = { 0xfff0c818, 0xff2e9a48, 0xffe878a8, 0xffe07828, 0xff48a8c8, 0xffd83030 };
    return juce::Colour (cols[((c % 6) + 6) % 6]);
}

//== geometry ===================================================================
OpEditor::Pt OpEditor::itemPos (const Item& i) const
{
    const auto m = mixRect();
    return { m.getX() + 24 + i.x * (m.getWidth() - 48), m.getY() + 18 + (1.0f - i.y) * (m.getHeight() - 36) };
}

void OpEditor::posToXY (Pt p, float& x, float& y) const
{
    const auto m = mixRect();
    x = juce::jlimit (0.0f, 1.0f, (p.x - m.getX() - 24) / (m.getWidth() - 48));
    y = juce::jlimit (0.0f, 1.0f, 1.0f - (p.y - m.getY() - 18) / (m.getHeight() - 36));
}

float OpEditor::barWidth (const Item& i) const
{
    const double secs = cellSecs[(size_t) i.cell] * (i.e - i.s);
    return (float) juce::jlimit (26.0, 230.0, secs * 55.0);
}

OpEditor::Rect OpEditor::barRect (int voiceId, const Item& i) const
{
    Pt c = itemPos (i);
    if (auto it = disp.find (voiceId); it != disp.end()) c = it->second;
    const float w = barWidth (i);
    return { c.x - w * 0.5f, c.y - 5, w, 10 };
}

OpEditor::Rect OpEditor::stripRect (int voiceId, const Item& i) const
{
    const auto b = barRect (voiceId, i);
    const float w = juce::jmax (b.getWidth(), 150.0f);
    return { b.getCentreX() - w * 0.5f, b.getBottom() + 9, w, 3 };
}

OpEditor::Rect OpEditor::attachedFxRect (int voiceId, const Item& i) const
{
    const auto b = barRect (voiceId, i);
    return { b.getRight() + 6, b.getCentreY() - 7, 14, 14 };
}

OpEditor::Rect OpEditor::flagRect() const
{
    const auto t = timelineRect();
    const float x = t.getX() + cur.playStart * t.getWidth();
    return { x - 13, t.getY() - 40, 26, 58 };
}

OpEditor::Rect OpEditor::segRect (const Segment& s) const
{
    const auto t = timelineRect();
    return { t.getX() + s.start * t.getWidth(), t.getY() - 6, juce::jmax (4.0f, s.len * t.getWidth()), 22 };
}

int OpEditor::itemAt (Pt p) const
{
    for (auto it = cur.mix.rbegin(); it != cur.mix.rend(); ++it)
        if (barRect (it->id, *it).expanded (2, 8).contains (p)) return it->id;
    return -1;
}

std::vector<OpEditor::DrawItem> OpEditor::visibleItems() const
{
    std::vector<DrawItem> v;
    if (playing)
    {
        const double L = cur.minutes * 60.0;
        for (auto& s : cur.segs)
            if (tlSeconds >= s.start * L && tlSeconds < (s.start + s.len) * L)
                for (size_t k = 0; k < s.snap.items.size(); ++k)
                    v.push_back ({ Engine::timelineVoiceId (s.id, (int) k), s.snap.items[k], false });
    }
    else
        for (auto& i : cur.mix) v.push_back ({ i.id, i, true });
    return v;
}

//== timer ======================================================================
void OpEditor::timerCallback()
{
    cur = proc.getModel();
    playing = proc.timelinePlaying();
    tlSeconds = proc.timelineSeconds();
    for (int c = 0; c < bank::kCells; ++c)
    {
        cellSecs[(size_t) c] = proc.cellSeconds (c);
        cellOk[(size_t) c] = proc.cellReady (c);
    }

    std::map<int, Pt> next;
    for (auto& i : cur.mix)
    {
        const Pt target = itemPos (i);
        auto it = disp.find (i.id);
        if (drag == Drag::Item && dragIndex == i.id && it != disp.end()) next[i.id] = it->second;
        else if (it == disp.end()) next[i.id] = spawnFrom.value_or (target);
        else next[i.id] = it->second + (target - it->second) * 0.28f;
    }
    disp = std::move (next);
    spawnFrom.reset();

    for (auto& f : presetFlash) f *= 0.86f;
    for (auto& f : opFlash) f *= 0.86f;
    snapFlash *= 0.86f;

    if (demo) demoStep();
    else if (proc.isStandalone() && helpPage < 0 && drag == Drag::None && cur.mix.empty() && ! playing
             && juce::Time::getMillisecondCounter() - lastInput > 180000)
        startDemo();

    repaint();
}

//== painting ===================================================================
void OpEditor::resized()
{
    scale = (float) getWidth() / (float) W;
    float ds = 2.0f;
    if (auto* d = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()) ds = (float) juce::jmax (1.0, d->scale);
    staticLayer = juce::Image (juce::Image::RGB, juce::jmax (1, (int) (getWidth() * ds)), juce::jmax (1, (int) (getHeight() * ds)), true);
    juce::Graphics g (staticLayer);
    g.addTransform (juce::AffineTransform::scale (scale * ds));
    renderStatic (g);
}

void OpEditor::renderStatic (juce::Graphics& g)
{
    g.setGradientFill (juce::ColourGradient (kBlueTop, 0, 0, kBlueBottom, 0, (float) H, false));
    g.fillRect (0, 0, W, H);

    // white tab under the browser, and the bowl under the play area
    juce::Path tab;
    tab.startNewSubPath (150, 240);
    tab.lineTo (150, 112);
    tab.cubicTo (215, 96, 268, 94, 318, 116);
    tab.cubicTo (372, 140, 430, 92, 520, 98);
    tab.cubicTo (560, 101, 584, 112, 596, 122);
    tab.lineTo (596, 240);
    tab.closeSubPath();
    juce::Path bowl;
    bowl.startNewSubPath (150, 556);
    bowl.lineTo (150, 598);
    bowl.lineTo (322, 598);
    bowl.cubicTo (372, 598, 396, 574, 448, 574);
    bowl.cubicTo (500, 574, 524, 598, 574, 598);
    bowl.lineTo (574, 556);
    bowl.closeSubPath();
    g.setColour (juce::Colours::white);
    g.fillPath (tab);
    g.fillPath (bowl);

    g.fillRect (browserRect());
    g.setColour (kMustard);
    g.fillRect (640.0f, 206.0f, 238.0f, 8.0f);

    // play area: white, dotted, divided in four
    const auto m = mixRect();
    g.setColour (juce::Colours::white);
    g.fillRect (m);
    g.setColour (juce::Colour (0xffc4c8d8));
    for (float y = m.getY() + 4; y < m.getBottom() - 2; y += 6)
        for (float x = m.getX() + 4; x < m.getRight() - 2; x += 6)
            g.fillRect (x, y, 1.2f, 1.2f);
    g.setColour (kInk.withAlpha (0.55f));
    for (int k = 1; k < 4; ++k)
        g.fillRect (m.getX() + m.getWidth() * k / 4.0f, m.getY(), 1.0f, m.getHeight());

    // frame
    g.setColour (kFrame);
    g.fillRect (250.0f, 218.0f, 644.0f, 14.0f);
    g.fillRect (880.0f, 218.0f, 14.0f, 384.0f);
    g.setColour (kFrameDark);
    for (float x = 156; x < 870; x += 26) g.fillRect (x, 560.0f, 14.0f, 12.0f);
    for (float y = 240; y < 548; y += 34) g.fillRect (138.0f, y, 12.0f, 18.0f);

    // effects column
    for (int i = 0; i < fx::Count; ++i)
    {
        const auto r = fxRect (i);
        juce::Path back, tri;
        back.addTriangle (r.getX() + 46, r.getY() + 8, r.getX() + 92, r.getY() + 32, r.getX() + 46, r.getY() + 56);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffa8dcff), r.getX() + 50, r.getY() + 30,
                                                 juce::Colour (0x0088c8f0), r.getX() + 92, r.getY() + 30, false));
        g.fillPath (back);
        tri.addTriangle (r.getX() + 32, r.getY(), r.getX() + 74, r.getY() + 24, r.getX() + 32, r.getY() + 48);
        g.setColour (kRed);
        g.fillPath (tri);
        drawFxIcon (g, i, { r.getX() + 2, r.getY() + 9, 30, 30 });
    }

    // the oval-style word mark
    const auto lr = logoRect();
    const auto logoFont = juce::Font (juce::FontOptions ("Times New Roman", 31.0f, juce::Font::plain));
    g.setFont (logoFont);
    const float w1 = juce::GlyphArrangement::getStringWidth (logoFont, "open");
    g.setColour (juce::Colour (0xff2f2f2f));
    g.drawText ("open", lr.withWidth (w1 + 2), juce::Justification::centredLeft, false);
    g.setColour (juce::Colour (0xffc8102e));
    g.drawText ("process", lr.withTrimmedLeft (w1), juce::Justification::centredLeft, false);
}

void OpEditor::drawFxIcon (juce::Graphics& g, int t, Rect r)
{
    switch (t)
    {
        case fx::FadeInOut:
            g.setGradientFill (juce::ColourGradient (juce::Colours::white, r.getX(), 0, juce::Colours::white, r.getRight(), 0, false));
            {
                juce::ColourGradient cg (juce::Colours::white, r.getX(), 0, juce::Colours::white, r.getRight(), 0, false);
                cg.addColour (0.5, juce::Colours::black);
                g.setGradientFill (cg);
            }
            g.fillRect (r);
            break;
        case fx::FadeIn:
            g.setGradientFill (juce::ColourGradient (juce::Colours::white, r.getX(), 0, juce::Colours::black, r.getRight(), 0, false));
            g.fillRect (r);
            break;
        case fx::FadeOut:
            g.setGradientFill (juce::ColourGradient (juce::Colours::black, r.getX(), 0, juce::Colours::white, r.getRight(), 0, false));
            g.fillRect (r);
            break;
        case fx::Mute:
            g.setColour (juce::Colour (0xffcfd0d8)); g.fillRect (r);
            g.setColour (juce::Colours::white); g.fillEllipse (r.reduced (r.getWidth() * 0.16f));
            g.setColour (kInk); g.fillEllipse (r.reduced (r.getWidth() * 0.27f));
            break;
        case fx::Solo:
            g.setColour (juce::Colour (0xffa8a9b4)); g.fillRect (r);
            g.setColour (juce::Colours::white); g.fillEllipse (r.reduced (r.getWidth() * 0.22f));
            break;
        default: break;
    }
}

void OpEditor::drawReel (juce::Graphics& g, Rect r, juce::Colour c)
{
    const Pt ctr (r.getX() + 16, r.getY() + 20);
    g.setColour (juce::Colour (0xff1a1a1a));
    g.drawEllipse (ctr.x - 11, ctr.y - 11, 22, 22, 7.0f);
    g.setColour (c);
    g.fillRect (r.getX() + 19, r.getY() + 1, 17.0f, 9.0f);
}

void OpEditor::drawTag (juce::Graphics& g, Rect r, const juce::String& text, juce::Justification j, juce::Colour bg, juce::Colour fg)
{
    g.setColour (bg);
    g.fillRect (r);
    g.setColour (fg);
    g.setFont (labelFont (r.getHeight() * 0.66f));
    g.drawText (text, r.reduced (7, 0), j, false);
}

void OpEditor::drawCellSquare (juce::Graphics& g, int cell, Rect r, bool ready)
{
    g.setColour (bank::outerColour (cell));
    g.fillRect (r);
    g.setColour (juce::Colours::white);
    g.fillRect (r.reduced (r.getWidth() * 0.15f));
    g.setColour (bank::innerColour (cell));
    g.fillRect (r.reduced (r.getWidth() * 0.3f));
    if (! ready)
    {
        g.setColour (juce::Colours::white.withAlpha (0.65f));
        g.fillRect (r);
    }
}

void OpEditor::drawPreset (juce::Graphics& g, int k, Rect r, float flash)
{
    const auto c = juce::Colour (kBracketColours[k]).interpolatedWith (kRed, juce::jlimit (0.0f, 1.0f, flash));
    g.setColour (c);
    if (k == 6)
    {
        for (int i = 0; i < 3; ++i)
            g.drawEllipse (r.getCentreX() - 8, r.getY() + 12 + i * 23.0f, 16, 18, 4.0f);
        return;
    }
    if (kBracketPixel[k])
    {
        auto& img = glyphs[(size_t) k];
        if (! img.isValid())
        {
            img = juce::Image (juce::Image::SingleChannel, 10, 28, true);
            juce::Graphics gg (img);
            gg.setColour (juce::Colours::white);
            gg.setFont (juce::Font (juce::FontOptions ("Times New Roman", 31.0f, juce::Font::plain)));
            gg.drawText (kBracketGlyphs[k], juce::Rectangle<int> (-2, -3, 14, 34), juce::Justification::centred, false);
        }
        g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
        g.drawImage (img, r, juce::RectanglePlacement::stretchToFit, true);
        return;
    }
    g.setFont (juce::Font (juce::FontOptions ("Times New Roman", 96.0f, juce::Font::plain)));
    g.drawText (kBracketGlyphs[k], r.expanded (8, 6), juce::Justification::centred, false);
}

void OpEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBlue);
    if (staticLayer.isValid())
        g.drawImage (staticLayer, getLocalBounds().toFloat(), juce::RectanglePlacement::stretchToFit);
    g.addTransform (juce::AffineTransform::scale (scale));

    drawBrowser (g);
    for (int k = 0; k < 8; ++k) drawPreset (g, k, presetRect (k), presetFlash[k]);
    drawOperators (g);
    drawItems (g);
    drawSnapButton (g);
    drawReels (g);
    drawTimeline (g);
    drawTools (g);

    // help marks beside the word mark
    const juce::Colour qc[3] = { kOrange, juce::Colour (0xffd8b020), juce::Colour (0xff2a2a2a) };
    for (int i = 0; i < 3; ++i)
    {
        const auto r = qRect (i);
        g.setColour (qc[i]);
        g.fillRect (r);
        if (helpPage == i)
        {
            g.setColour (juce::Colours::white);
            g.drawRect (r, 2.0f);
        }
        g.setColour (juce::Colours::white);
        g.setFont (labelFont (14.0f));
        g.drawText ("?", r, juce::Justification::centred, false);
    }
    if (demo)
    {
        g.setColour (juce::Colour (0xff2050e0));
        g.setFont (labelFont (11.0f));
        g.drawText ("end demo", endDemoRect(), juce::Justification::centredLeft, false);
    }

    if (playing)
        drawTag (g, { mixRect().getX() + 8, mixRect().getY() + 8, 330, 16 }, "playing the timeline - click the red flag to stop",
                 juce::Justification::centredLeft, kTag, kInk);

    if (helpPage == 0 || demo) drawGuide (g);
    drawDragGhost (g);
    if (helpPage == 1 || helpPage == 2) drawHelp (g);

    auto st = proc.status();
    if (st.isEmpty() && proc.busy()) st = "making sounds ...";
    if (st.isNotEmpty())
    {
        const float w = juce::GlyphArrangement::getStringWidth (labelFont (11.0f), st) + 40;
        drawTag (g, { 14, 14, w, 18 }, st, juce::Justification::centredLeft, kTag, kInk);
    }
    if (demo) drawCursor (g, demoCursor, demoCarry);
}

void OpEditor::drawBrowser (juce::Graphics& g)
{
    for (int c = 0; c < bank::kCells; ++c)
        drawCellSquare (g, c, cellRect (c), cellOk[(size_t) c]);
    if (selectedCell >= 0)
    {
        g.setColour (kInk);
        g.drawRect (cellRect (selectedCell).expanded (2), 2.0f);
    }
    if (dropCell >= 0)
    {
        g.setColour (kRed);
        g.drawRect (cellRect (dropCell).expanded (2), 2.5f);
    }
    else if (dropCell == -1)
    {
        g.setColour (kRed);
        g.drawRect (browserRect().expanded (2), 3.0f);
    }
}

void OpEditor::drawOperators (juce::Graphics& g)
{
    // left: a scribbled black square (random loop lengths)
    const auto a = opRect (0);
    juce::Random r (7);
    g.setColour (kInk.interpolatedWith (juce::Colours::white, opFlash[0] * 0.8f));
    for (int i = 0; i < 170; ++i)
    {
        const float x1 = a.getX() + r.nextFloat() * a.getWidth(), y1 = a.getY() + r.nextFloat() * a.getHeight();
        const float x2 = juce::jlimit (a.getX(), a.getRight(), x1 + (r.nextFloat() - 0.5f) * 26);
        const float y2 = juce::jlimit (a.getY(), a.getBottom(), y1 + (r.nextFloat() - 0.5f) * 26);
        g.drawLine (x1, y1, x2, y2, 1.1f);
    }
    g.fillRect (a.reduced (10));

    // right: grey grid (random positions)
    const auto b = opRect (1);
    g.setColour (juce::Colour (0xff4a4a50));
    g.fillRect (b);
    g.setColour (juce::Colour (0xffd8d8de).interpolatedWith (kRed, opFlash[1]));
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
            g.fillRect (b.getX() + 5 + x * 12.0f, b.getY() + 5 + y * 12.0f, 8.0f, 8.0f);
}

void OpEditor::drawItems (juce::Graphics& g)
{
    const auto items = visibleItems();
    bool anySolo = false;
    for (auto& d : items) anySolo |= d.item.effect == fx::Solo;

    juce::Graphics::ScopedSaveState ss (g);
    if (drag != Drag::Item) g.reduceClipRegion (mixRect().expanded (40, 30).toNearestInt());

    for (auto& d : items)
    {
        const auto& i = d.item;
        const auto r = barRect (d.voiceId, i);
        const bool silent = i.effect == fx::Mute || (anySolo && i.effect != fx::Solo);
        const float alpha = silent ? 0.35f : 1.0f;

        g.setColour (kInk.withAlpha (alpha));
        g.fillRect (r.getX(), r.getY(), r.getWidth(), 2.0f);
        g.setColour (bank::outerColour (i.cell).withAlpha (alpha));
        g.fillRect (r.getX(), r.getY() + 2, r.getWidth(), 6.0f);
        g.setColour (bank::innerColour (i.cell).withAlpha (alpha));
        g.fillRect (r.getX(), r.getY() + 8, r.getWidth(), 2.0f);
        g.setColour (kRed.withAlpha (alpha));
        g.drawLine (r.getX() - 9, r.getBottom() + 1, r.getX() - 4, r.getY() - 5, 1.6f);

        const float f = proc.voiceFrac (d.voiceId);
        if (f >= 0.0f)
        {
            g.setColour (kInk);
            g.fillRect (r.getX() + f * r.getWidth() - 1, r.getY() - 8, 2.0f, 24.0f);
        }
        if (i.effect != fx::None && ! (drag == Drag::AttachedFx && dragIndex == i.id && moved))
            drawFxIcon (g, i.effect, attachedFxRect (d.voiceId, i));

        if (d.live && i.id == selectedItem)
        {
            const auto s = (drag == Drag::LoopS || drag == Drag::LoopE) ? dragStrip : stripRect (d.voiceId, i);
            g.setColour (kFrameDark);
            g.fillRect (s);
            g.setColour (bank::outerColour (i.cell));
            g.fillRect (s.getX() + i.s * s.getWidth(), s.getY(), (i.e - i.s) * s.getWidth(), s.getHeight());
            g.setColour (kRed);
            for (float v : { i.s, i.e })
            {
                const float x = s.getX() + v * s.getWidth();
                juce::Path t;
                t.addTriangle (x, s.getBottom() + 1, x - 6, s.getBottom() + 11, x + 6, s.getBottom() + 11);
                g.fillPath (t);
            }
            g.setColour (kInk);
            g.setFont (labelFont (10.0f));
            g.drawText (proc.cellName (i.cell), juce::Rectangle<float> (r.getCentreX() - 100, r.getY() - 22, 200, 12),
                        juce::Justification::centred, false);
        }
    }
}

void OpEditor::drawSnapButton (juce::Graphics& g)
{
    const auto r = snapButtonRect();
    const float cx = r.getCentreX(), cy = r.getY() + 6, u = 16;
    const int yellow[][2] = { { -4, 0 }, { 4, 0 }, { -3, 1 }, { 3, 1 }, { -2, 2 }, { 2, 2 }, { -1, 3 }, { 0, 3 }, { 1, 3 }, { -1, 2 }, { 1, 2 } };
    const int grey[][2] = { { -3, 0 }, { 3, 0 }, { -2, 1 }, { 2, 1 }, { 0, 2 } };
    g.setColour (kFrameDark);
    for (auto& q : grey) g.fillRect (cx + q[0] * u - u * 0.5f, cy + q[1] * u, u - 2, u - 2);
    g.setColour (kYellow.interpolatedWith (juce::Colours::white, snapFlash));
    for (auto& q : yellow) g.fillRect (cx + q[0] * u - u * 0.5f, cy + q[1] * u, u - 2, u - 2);
    // icon: a reel with a pointer
    const Pt ic (cx - 4, cy + 22);
    g.setColour (juce::Colours::white);
    g.fillEllipse (ic.x - 11, ic.y - 11, 22, 22);
    g.setColour (kInk);
    g.drawEllipse (ic.x - 8, ic.y - 8, 16, 16, 4.0f);
    g.fillEllipse (ic.x - 2, ic.y - 2, 4, 4);
    juce::Path t;
    t.addTriangle (ic.x + 7, ic.y - 9, ic.x + 17, ic.y - 9, ic.x + 12, ic.y - 2);
    g.fillPath (t);
}

void OpEditor::drawReels (juce::Graphics& g)
{
    for (size_t i = 0; i < cur.snaps.size(); ++i)
    {
        const auto& s = cur.snaps[i];
        juce::Graphics::ScopedSaveState ss (g);
        if (drag == Drag::Reel && dragIndex == s.id && moved) g.setOpacity (0.3f);
        drawReel (g, reelRect ((int) i), reelColour (s.colour));
    }
}

void OpEditor::drawTimeline (juce::Graphics& g)
{
    const auto t = timelineRect();
    g.setColour (juce::Colour (0xffd6d7df));
    g.fillRect (t);
    g.setColour (juce::Colour (0xff8a8b96));
    for (int mnt = 1; mnt < (int) std::ceil (cur.minutes); ++mnt)
        g.fillRect (t.getX() + mnt / cur.minutes * t.getWidth(), t.getBottom(), 1.0f, 5.0f);

    for (auto& s : cur.segs)
    {
        const auto r = segRect (s);
        g.setColour (reelColour (s.snap.colour));
        g.fillRect (r.getX(), t.getY(), r.getWidth(), t.getHeight());
        g.setColour (kInk);
        g.fillRect (r.getX(), r.getY(), 4.0f, r.getHeight());
        g.fillRect (r.getRight() - 4, r.getY(), 4.0f, r.getHeight());
        g.fillRect (r.getX() + 4, t.getY(), juce::jmin (10.0f, r.getWidth() - 8), 4.0f);
    }

    // play pointer (red flag)
    const float fx0 = t.getX() + cur.playStart * t.getWidth();
    g.setColour (juce::Colours::white);
    g.fillRect (fx0 - 1.5f, t.getY() - 34, 3.0f, 50.0f);
    g.setColour (playing && (juce::Time::getMillisecondCounter() / 400) % 2 ? juce::Colour (0xffff6040) : kRed);
    g.fillRect (fx0 - 8, t.getY() - 38, 13.0f, 11.0f);
    if (playing)
    {
        const float px = t.getX() + (float) (tlSeconds / (cur.minutes * 60.0)) * t.getWidth();
        g.setColour (kInk);
        g.fillRect (px - 1, t.getY() - 9, 2.0f, 28.0f);
    }

    // composition length wheel
    const auto w = wheelRect();
    const Pt c = w.getCentre();
    juce::Path cres;
    cres.addEllipse (c.x - 30, c.y - 30, 60, 60);
    cres.addEllipse (c.x - 12, c.y - 21, 42, 42);
    cres.setUsingNonZeroWinding (false);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffd8452e), c.x - 30, c.y, juce::Colours::white, c.x + 8, c.y - 4, false));
    g.fillPath (cres);
    g.setColour (juce::Colour (0xff9a2a1a));
    g.strokePath (cres, juce::PathStrokeType (1.0f));
    const float ang = juce::jmap (cur.minutes, 1.0f, 4.0f, -2.6f, -1.0f);
    g.setColour (kRed);
    g.drawLine (c.x + 18 * std::cos (ang), c.y + 18 * std::sin (ang), c.x + 42 * std::cos (ang), c.y + 42 * std::sin (ang), 2.0f);
    g.setColour (juce::Colours::white);
    g.setFont (labelFont (11.0f));
    g.drawText (juce::String (cur.minutes, 1) + " min.", juce::Rectangle<float> (c.x - 6, c.y - 8, 80, 16),
                juce::Justification::centredLeft, false);
}

void OpEditor::drawTools (juce::Graphics& g)
{
    for (int i = 0; i < 6; ++i)
    {
        const auto r = toolRect (i);
        const auto sq = r.withWidth (22).withSizeKeepingCentre (22, 22).withX (r.getX());
        juce::ColourGradient cg (juce::Colours::black, sq.getX(), 0, juce::Colours::white, sq.getRight(), 0, false);
        if (i % 2) cg = juce::ColourGradient (juce::Colours::white, sq.getX(), 0, juce::Colours::black, sq.getRight(), 0, false);
        g.setGradientFill (cg);
        g.fillRect (sq);
        juce::Path tri;
        tri.addTriangle (sq.getRight(), sq.getY() + 3, sq.getRight() + 9, sq.getCentreY(), sq.getRight(), sq.getBottom() - 3);
        g.setColour (hoverTool == i ? kOrange : kRed);
        g.fillPath (tri);
        // mark
        const auto m = sq.reduced (6);
        g.setColour (i % 2 ? juce::Colours::white : kInk);
        auto inner = juce::Colour (0xffeeeeee);
        juce::ignoreUnused (inner);
        g.setColour (juce::Colours::white);
        g.fillRect (m);
        g.setColour (kInk);
        switch (i)
        {
            case 0: g.fillRect (m.getCentreX() - 1, m.getY() + 1, 2.0f, m.getHeight() - 2); g.fillRect (m.getX() + 1, m.getCentreY() - 1, m.getWidth() - 2, 2.0f); break;
            case 1: for (int q = 0; q < 4; ++q) g.fillRect (m.getX() + 1 + (q % 2) * 5.0f, m.getY() + 1 + (q / 2) * 5.0f, 3.0f, 3.0f); break;
            case 2: { juce::Path z; z.startNewSubPath (m.getX() + 1, m.getBottom() - 2); z.lineTo (m.getX() + 3.5f, m.getY() + 2); z.lineTo (m.getCentreX(), m.getBottom() - 2); z.lineTo (m.getRight() - 3.5f, m.getY() + 2); z.lineTo (m.getRight() - 1, m.getBottom() - 2); g.strokePath (z, juce::PathStrokeType (1.4f)); break; }
            case 3: g.setColour (kRed); g.fillEllipse (m.reduced (1.5f)); break;
            case 4: { juce::Path a; a.addTriangle (m.getX() + 1, m.getCentreY() - 1, m.getRight() - 1, m.getCentreY() - 1, m.getCentreX(), m.getBottom() - 1); g.fillPath (a); g.fillRect (m.getCentreX() - 1, m.getY() + 1, 2.0f, 4.0f); break; }
            default: g.drawLine (m.getX() + 1, m.getY() + 1, m.getRight() - 1, m.getBottom() - 1, 1.6f); g.drawLine (m.getRight() - 1, m.getY() + 1, m.getX() + 1, m.getBottom() - 1, 1.6f); break;
        }
    }
    if (hoverTool >= 0)
    {
        const auto r = toolRect (hoverTool);
        drawTag (g, { 700, r.getCentreY() - 9, 168, 18 }, kToolNames[hoverTool], juce::Justification::centredRight, kTag, kInk);
    }
}

void OpEditor::drawTextOnArc (juce::Graphics& g, Pt c, float radius, float a, const juce::String& text)
{
    const auto font = labelFont (12.5f);
    g.setFont (font);
    for (auto ch : text)
    {
        const juce::String s = juce::String::charToString (ch);
        const float w = juce::GlyphArrangement::getStringWidth (font, s) + 1.2f;
        a += (w * 0.5f) / radius;
        const Pt p (c.x + radius * std::cos (a), c.y + radius * std::sin (a));
        juce::Graphics::ScopedSaveState ss (g);
        g.addTransform (juce::AffineTransform::rotation (a + kPi * 0.5f).translated (p.x, p.y));
        g.drawText (s, juce::Rectangle<float> (-10, -10, 20, 14), juce::Justification::centred, false);
        a += (w * 0.5f) / radius;
    }
}

void OpEditor::drawGuide (juce::Graphics& g)
{
    // "drag samples into play area" on an arc from the browser into the play area
    const Pt ac (500, 268);
    const float rad = 200, a0 = -1.42f, a1 = -0.28f;
    juce::Path arc;
    arc.addCentredArc (ac.x, ac.y, rad + 2, rad + 2, 0, a0 + kPi * 0.5f, a1 + kPi * 0.5f, true);
    g.setColour (kMustard.withAlpha (0.85f));
    g.strokePath (arc, juce::PathStrokeType (13.0f));
    g.setColour (kInk);
    drawTextOnArc (g, ac, rad - 4, a0 + 0.06f, "drag samples into play area");

    g.setFont (labelFont (12.0f));
    g.drawText ("click", juce::Rectangle<float> (opRect (0).getX(), 132, 52, 16), juce::Justification::centred, false);
    g.drawText ("click", juce::Rectangle<float> (opRect (1).getX(), 132, 52, 16), juce::Justification::centred, false);

    // loud / soft / left / right cross
    const auto m = mixRect();
    const float cx = m.getCentreX(), cy = m.getCentreY();
    g.setColour (kMustard);
    for (float x = m.getX() + 60; x < m.getRight() - 60; x += 11) g.fillRect (x, cy, 3.0f, 3.0f);
    for (float y = m.getY() + 30; y < m.getBottom() - 26; y += 11) g.fillRect (cx, y, 3.0f, 3.0f);
    auto arrow = [&] (Pt p, float angle) {
        juce::Path t;
        t.addTriangle (0, -7, 7, 6, -7, 6);
        t.applyTransform (juce::AffineTransform::rotation (angle).translated (p.x, p.y));
        g.setColour (kRed);
        g.fillPath (t);
        g.setColour (juce::Colour (0xff3060c8));
        g.strokePath (t, juce::PathStrokeType (1.0f));
    };
    arrow ({ cx, m.getY() + 16 }, 0);
    arrow ({ cx, m.getBottom() - 14 }, kPi);
    arrow ({ m.getX() + 46, cy + 1 }, -kPi * 0.5f);
    arrow ({ m.getRight() - 46, cy + 1 }, kPi * 0.5f);
    g.setColour (kInk);
    g.setFont (labelFont (12.0f));
    g.drawText ("loud", juce::Rectangle<float> (cx + 12, m.getY() + 8, 60, 16), juce::Justification::centredLeft, false);
    g.drawText ("soft", juce::Rectangle<float> (cx - 72, m.getBottom() - 22, 60, 16), juce::Justification::centredRight, false);
    g.drawText ("left", juce::Rectangle<float> (m.getX() + 58, cy - 7, 60, 16), juce::Justification::centredLeft, false);
    g.drawText ("right", juce::Rectangle<float> (m.getRight() - 118, cy - 7, 60, 16), juce::Justification::centredRight, false);

    drawTag (g, { 12, 405, 300, 18 }, "try presets", juce::Justification::centredRight, kTag, kInk);
    drawTag (g, { 690, 405, 200, 18 }, "drag effects", juce::Justification::centredLeft, kTag, kInk);
    drawTag (g, { 548, 534, 76, 18 }, "drag...", juce::Justification::centredLeft, kOrange, kInk);
    drawTag (g, { 128, 608, 246, 18 }, "... and store configurations here.", juce::Justification::centredRight, kTag, kInk);
    drawTag (g, { 330, 694, 76, 18 }, "drag...", juce::Justification::centredLeft, kOrange, kInk);
    drawTag (g, { 420, 694, 190, 18 }, "... and arrange here.", juce::Justification::centredLeft, kTag, kInk);
}

void OpEditor::drawHelp (juce::Graphics& g)
{
    static const char* page1[] = {
        "browser (top): 8 x 8 loops. drag one into the play area and it starts.",
        "play area: left / right is the panorama, up / down is loud / soft.",
        "every sound loops; the black pointer runs across it.",
        "click a sound: the two red pointers set the part of the sample that loops.",
        "drag an effect from the right onto a sound: fade in+out, fade in,",
        "fade out, mute, solo. drag it off the sound to take it away.",
        "presets (brackets, left): a new mix with each click. shift-click adds.",
        "logical operators (top right): random loop lengths / random positions.",
        "throw a sound out of the play area to stop it.",
    };
    static const char* page2[] = {
        "yellow button: store the play area as a snapshot (a reel).",
        "drag a reel onto the timeline. drag segments to move them, their right",
        "end to scale, alt-drag to copy, drag them away to delete.",
        "drag a reel back into the play area to hear it again.",
        "throw a reel out of the window to delete it.",
        "wheel: length of the composition, 1 - 4 min. red flag: click to play / stop,",
        "drag it to set where playing starts. while it plays nothing can be changed.",
        "drop audio files or folders on the browser to chop them into 64 loops;",
        "one file dropped on one cell replaces only that cell.",
        "tools (bottom right): load, built-in, skip again, capture input, render, clear.",
        "MIDI: C1-G1 presets, G#1 A1 operators, A#1 snapshot, B1 timeline, C2.. cells.",
    };
    const auto r = Rect (150, 118, 700, 400);
    g.setColour (juce::Colour (0xffd2d3db));
    g.fillRect (r);
    g.setColour (kInk);
    g.drawRect (r, 1.5f);
    drawTag (g, { r.getX(), r.getY(), r.getWidth(), 24 }, helpPage == 1 ? "play area" : "snapshots, timeline, sounds",
             juce::Justification::centredLeft, helpPage == 1 ? juce::Colour (0xffd8b020) : juce::Colour (0xff2a2a2a),
             helpPage == 1 ? kInk : juce::Colours::white);
    g.setColour (kInk);
    g.setFont (labelFont (12.0f));
    const int n = helpPage == 1 ? (int) std::size (page1) : (int) std::size (page2);
    for (int i = 0; i < n; ++i)
        g.drawText (helpPage == 1 ? page1[i] : page2[i], juce::Rectangle<float> (r.getX() + 18, r.getY() + 40 + i * 28.0f, r.getWidth() - 30, 20),
                    juce::Justification::centredLeft, false);
    g.setColour (juce::Colour (0xff5a5b66));
    g.drawText ("click anywhere to close", juce::Rectangle<float> (r.getX() + 18, r.getBottom() - 26, r.getWidth() - 36, 18),
                juce::Justification::centredRight, false);
}

void OpEditor::drawDragGhost (juce::Graphics& g)
{
    if (drag == Drag::Cell && moved)
    {
        const int n = (int) trail.size();
        for (int k = 0; k < n; ++k)
        {
            const auto p = trail[(size_t) k];
            g.setColour (bank::outerColour (dragIndex).withAlpha (0.15f + 0.5f * k / (float) juce::jmax (1, n)));
            g.drawRect (Rect (p.x - 10, p.y - 10, 20, 20), 1.0f);
        }
        drawCellSquare (g, dragIndex, { curPt.x - 10, curPt.y - 10, 20, 20 }, true);
    }
    else if (drag == Drag::Effect && moved)
        drawFxIcon (g, dragIndex, { curPt.x - 15, curPt.y - 15, 30, 30 });
    else if (drag == Drag::AttachedFx && moved)
    {
        for (auto& i : cur.mix)
            if (i.id == dragIndex && i.effect != fx::None) drawFxIcon (g, i.effect, { curPt.x - 7, curPt.y - 7, 14, 14 });
    }
    else if (drag == Drag::Reel && moved)
    {
        for (auto& s : cur.snaps)
            if (s.id == dragIndex) drawReel (g, { curPt.x - 16, curPt.y - 20, 40, 34 }, reelColour (s.colour));
    }

    if (demo && demoCarry)
    {
        if (demoKind == 0) drawCellSquare (g, demoArg, { demoCursor.x - 10, demoCursor.y - 10, 20, 20 }, true);
        else if (demoKind == 1) drawFxIcon (g, demoArg, { demoCursor.x - 12, demoCursor.y - 12, 24, 24 });
        else if (demoKind == 6 && ! cur.snaps.empty()) drawReel (g, { demoCursor.x - 16, demoCursor.y - 20, 40, 34 }, reelColour (cur.snaps.back().colour));
    }
}

void OpEditor::drawCursor (juce::Graphics& g, Pt p, bool pressed)
{
    juce::Path a;
    a.startNewSubPath (0, 0);
    a.lineTo (0, 17);
    a.lineTo (4.5f, 13);
    a.lineTo (7.5f, 19.5f);
    a.lineTo (10, 18.5f);
    a.lineTo (7, 12);
    a.lineTo (12.5f, 12);
    a.closeSubPath();
    a.applyTransform (juce::AffineTransform::translation (p.x, p.y));
    g.setColour (pressed ? kRed : kInk);
    g.fillPath (a);
    g.setColour (juce::Colours::white);
    g.strokePath (a, juce::PathStrokeType (1.2f));
}

//== mouse ======================================================================
void OpEditor::mouseDown (const juce::MouseEvent& e)
{
    noteInput();
    const Pt p = toCanvas (e.position);
    downPt = curPt = p;
    moved = false;
    drag = Drag::None;
    trail.clear();

    if (demo) { endDemo(); return; }
    if (helpPage == 1 || helpPage == 2) { helpPage = -1; return; }

    for (int i = 0; i < 3; ++i)
        if (qRect (i).expanded (2).contains (p)) { helpPage = helpPage == i ? -1 : i; return; }
    if (logoRect().contains (p)) { startDemo(); return; }

    for (int i = 0; i < 6; ++i)
        if (toolRect (i).contains (p)) { toolAction (i); return; }

    if (flagRect().contains (p)) { drag = Drag::Flag; dragStartVal = cur.playStart; return; }

    const bool editable = ! playing;
    auto locked = [this] { proc.setStatus ("stop the timeline to change things"); };

    if (wheelRect().contains (p))
    {
        if (! editable) { locked(); return; }
        drag = Drag::Wheel; dragStartVal = cur.minutes; return;
    }
    for (int k = 0; k < 8; ++k)
        if (presetRect (k).contains (p))
        {
            if (! editable) { locked(); return; }
            spawnFrom = presetRect (k).getCentre();
            proc.applyPreset (k, e.mods.isShiftDown());
            presetFlash[k] = 1.0f;
            selectedItem = -1;
            return;
        }
    for (int i = 0; i < 2; ++i)
        if (opRect (i).contains (p))
        {
            if (! editable) { locked(); return; }
            if (i == 0) proc.operatorLength(); else proc.operatorPosition();
            opFlash[i] = 1.0f;
            return;
        }
    if (snapButtonRect().contains (p))
    {
        if (! editable) { locked(); return; }
        if (proc.takeSnapshot() >= 0) snapFlash = 1.0f;
        return;
    }
    for (size_t i = 0; i < cur.snaps.size(); ++i)
        if (reelRect ((int) i).contains (p))
        {
            if (! editable) { locked(); return; }
            drag = Drag::Reel; dragIndex = cur.snaps[i].id; return;
        }
    for (auto it = cur.segs.rbegin(); it != cur.segs.rend(); ++it)
    {
        const auto r = segRect (*it);
        if (! r.expanded (3, 2).contains (p)) continue;
        if (! editable) { locked(); return; }
        int id = it->id;
        const bool resize = p.x > r.getRight() - 7;
        if (e.mods.isAltDown() && ! resize)
        {
            const Segment copy = *it;
            proc.edit ([&] (Model& m) {
                if ((int) m.segs.size() >= Model::kMaxSegs) return;
                Segment s = copy;
                s.id = id = m.nextId++;
                m.segs.push_back (s);
            });
        }
        drag = resize ? Drag::SegResize : Drag::Seg;
        dragIndex = id;
        dragStartVal = resize ? it->len : it->start;
        return;
    }
    for (int i = 0; i < fx::Count; ++i)
        if (fxRect (i).withTrimmedRight (40).contains (p) || fxRect (i).withWidth (34).contains (p))
        {
            if (! editable) { locked(); return; }
            drag = Drag::Effect; dragIndex = i; return;
        }
    for (int c = 0; c < bank::kCells; ++c)
        if (cellRect (c).expanded (2).contains (p))
        {
            if (! editable) { locked(); return; }
            drag = Drag::Cell; dragIndex = c; selectedCell = c; return;
        }

    if (! editable) return;

    // the selected sound's loop pointers
    if (auto* sel = cur.findItem (selectedItem))
    {
        const auto s = stripRect (sel->id, *sel);
        for (int k = 0; k < 2; ++k)
        {
            const float x = s.getX() + (k == 0 ? sel->s : sel->e) * s.getWidth();
            if (Rect (x - 8, s.getY() - 2, 16, 16).contains (p))
            {
                drag = k == 0 ? Drag::LoopS : Drag::LoopE;
                dragIndex = sel->id;
                dragStrip = s;
                return;
            }
        }
    }
    for (auto it = cur.mix.rbegin(); it != cur.mix.rend(); ++it)
        if (it->effect != fx::None && attachedFxRect (it->id, *it).expanded (3).contains (p))
        {
            drag = Drag::AttachedFx; dragIndex = it->id; return;
        }
    const int id = itemAt (p);
    if (id >= 0)
    {
        drag = Drag::Item;
        dragIndex = id;
        selectedItem = id;
        selectedCell = -1;
        if (auto it = disp.find (id); it != disp.end()) grabOffset = it->second - p;
        else grabOffset = {};
        return;
    }
    if (mixRect().contains (p)) selectedItem = -1;
}

void OpEditor::mouseDrag (const juce::MouseEvent& e)
{
    noteInput();
    const Pt p = toCanvas (e.position);
    curPt = p;
    if (p.getDistanceFrom (downPt) > 3.0f) moved = true;
    const auto t = timelineRect();

    switch (drag)
    {
        case Drag::Cell:
            if (trail.empty() || trail.back().getDistanceFrom (p) > 9.0f) trail.push_back (p);
            while (trail.size() > 16) trail.pop_front();
            break;
        case Drag::Item:
        {
            const Pt pos = p + grabOffset;
            disp[dragIndex] = pos;
            if (mixRect().contains (pos))
            {
                float x, y;
                posToXY (pos, x, y);
                proc.edit ([&] (Model& m) { if (auto* i = m.findItem (dragIndex)) { i->x = x; i->y = y; } });
            }
            break;
        }
        case Drag::LoopS:
        case Drag::LoopE:
        {
            const float v = juce::jlimit (0.0f, 1.0f, (p.x - dragStrip.getX()) / dragStrip.getWidth());
            const bool start = drag == Drag::LoopS;
            proc.edit ([&] (Model& m) {
                if (auto* i = m.findItem (dragIndex))
                {
                    const float gap = 0.008f;
                    if (start) i->s = juce::jlimit (0.0f, i->e - gap, v);
                    else i->e = juce::jlimit (i->s + gap, 1.0f, v);
                }
            });
            break;
        }
        case Drag::Wheel:
        {
            const float mins = juce::jlimit (1.0f, 4.0f, std::round ((dragStartVal + (downPt.y - p.y) * 0.02f + (p.x - downPt.x) * 0.01f) * 10.0f) / 10.0f);
            proc.edit ([&] (Model& m) { m.minutes = mins; });
            break;
        }
        case Drag::Flag:
            if (moved && ! playing)
            {
                const float v = juce::jlimit (0.0f, 0.98f, dragStartVal + (p.x - downPt.x) / t.getWidth());
                proc.edit ([&] (Model& m) { m.playStart = v; });
            }
            break;
        case Drag::Seg:
            proc.edit ([&] (Model& m) {
                if (auto* s = m.findSeg (dragIndex))
                    s->start = juce::jlimit (0.0f, 1.0f - s->len, dragStartVal + (p.x - downPt.x) / t.getWidth());
            });
            break;
        case Drag::SegResize:
            proc.edit ([&] (Model& m) {
                if (auto* s = m.findSeg (dragIndex))
                    s->len = juce::jlimit (0.01f, 1.0f - s->start, dragStartVal + (p.x - downPt.x) / t.getWidth());
            });
            break;
        default: break;
    }
}

void OpEditor::mouseUp (const juce::MouseEvent& e)
{
    noteInput();
    const Pt p = toCanvas (e.position);
    const auto t = timelineRect();
    const bool offWindow = ! Rect (0, 0, (float) W, (float) H).contains (p);

    switch (drag)
    {
        case Drag::Cell:
            if (moved && mixRect().contains (p))
            {
                float x, y;
                posToXY (p, x, y);
                const int id = proc.addItem (dragIndex, x, y);
                if (id >= 0) { disp[id] = p; selectedCell = -1; }
                else proc.setStatus ("the play area is full");
            }
            break;
        case Drag::Item:
            if (! mixRect().expanded (6).contains (p + grabOffset))
            {
                proc.edit ([&] (Model& m) {
                    m.mix.erase (std::remove_if (m.mix.begin(), m.mix.end(), [&] (const Item& i) { return i.id == dragIndex; }), m.mix.end());
                });
                disp.erase (dragIndex);
                if (selectedItem == dragIndex) selectedItem = -1;
            }
            break;
        case Drag::Effect:
            if (moved)
                if (const int id = itemAt (p); id >= 0)
                {
                    const int f = dragIndex;
                    proc.edit ([&] (Model& m) { if (auto* i = m.findItem (id)) i->effect = (i->effect == f ? fx::None : f); });
                }
            break;
        case Drag::AttachedFx:
            if (moved)
            {
                const int target = itemAt (p);
                if (target != dragIndex)
                    proc.edit ([&] (Model& m) {
                        auto* from = m.findItem (dragIndex);
                        if (from == nullptr) return;
                        if (auto* to = m.findItem (target)) to->effect = from->effect;
                        from->effect = fx::None;
                    });
            }
            break;
        case Drag::Reel:
            if (moved)
            {
                if (offWindow)
                    proc.edit ([&] (Model& m) {
                        m.snaps.erase (std::remove_if (m.snaps.begin(), m.snaps.end(), [&] (const Snapshot& s) { return s.id == dragIndex; }), m.snaps.end());
                    });
                else if (t.expanded (12, 26).contains (p))
                    addSegmentAt (dragIndex, (p.x - t.getX()) / t.getWidth());
                else if (mixRect().contains (p))
                {
                    proc.loadSnapshot (dragIndex);
                    disp.clear();
                    spawnFrom = p;
                    selectedItem = -1;
                }
            }
            break;
        case Drag::Seg:
        case Drag::SegResize:
            if (moved && (std::abs (p.y - t.getCentreY()) > 50 || offWindow))
                proc.edit ([&] (Model& m) {
                    m.segs.erase (std::remove_if (m.segs.begin(), m.segs.end(), [&] (const Segment& s) { return s.id == dragIndex; }), m.segs.end());
                    if (m.segs.empty()) m.timelinePlaying = false;
                });
            break;
        case Drag::Flag:
            if (! moved) proc.toggleTimeline();
            break;
        default: break;
    }
    drag = Drag::None;
    trail.clear();
    moved = false;
}

void OpEditor::mouseMove (const juce::MouseEvent& e)
{
    const Pt p = toCanvas (e.position);
    hoverTool = -1;
    for (int i = 0; i < 6; ++i)
        if (toolRect (i).contains (p)) hoverTool = i;
}

void OpEditor::mouseExit (const juce::MouseEvent&) { hoverTool = -1; }

void OpEditor::addSegmentAt (int snapId, float centreFrac)
{
    proc.edit ([&] (Model& m) {
        if ((int) m.segs.size() >= Model::kMaxSegs) return;
        for (auto& sn : m.snaps)
            if (sn.id == snapId)
            {
                Segment s;
                s.id = m.nextId++;
                s.len = juce::jmin (1.0f, 20.0f / (m.minutes * 60.0f));
                s.start = juce::jlimit (0.0f, 1.0f - s.len, centreFrac - s.len * 0.5f);
                s.snap = sn;
                m.segs.push_back (s);
                return;
            }
    });
}

//== files ======================================================================
void OpEditor::fileDragMove (const juce::StringArray&, int x, int y)
{
    const Pt p = toCanvas ({ (float) x, (float) y });
    dropCell = -1;
    for (int c = 0; c < bank::kCells; ++c)
        if (cellRect (c).expanded (2).contains (p)) dropCell = c;
}

void OpEditor::filesDropped (const juce::StringArray& names, int x, int y)
{
    noteInput();
    const Pt p = toCanvas ({ (float) x, (float) y });
    juce::Array<juce::File> files;
    for (auto& n : names) files.add (juce::File (n));
    int cell = -1;
    for (int c = 0; c < bank::kCells; ++c)
        if (cellRect (c).expanded (2).contains (p)) cell = c;
    if (files.size() == 1 && files[0].existsAsFile() && cell >= 0)
    {
        proc.loadFileIntoCell (cell, files[0]);
        selectedCell = cell;
    }
    else
        proc.loadFiles (files);
    dropCell = -2;
}

void OpEditor::toolAction (int i)
{
    switch (i)
    {
        case 0:
            chooser = std::make_unique<juce::FileChooser> ("Load sounds (files or folders)", OpProcessor::userFolder().exists()
                                                               ? OpProcessor::userFolder() : juce::File::getSpecialLocation (juce::File::userMusicDirectory));
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                                      | juce::FileBrowserComponent::canSelectDirectories | juce::FileBrowserComponent::canSelectMultipleItems,
                                  [this] (const juce::FileChooser& fc) {
                                      if (! fc.getResults().isEmpty()) proc.loadFiles (fc.getResults());
                                  });
            break;
        case 1: proc.loadBuiltin(); break;
        case 2:
        {
            int cell = selectedCell;
            if (cell < 0)
                if (auto* it = cur.findItem (selectedItem)) cell = it->cell;
            if (cell < 0) proc.setStatus ("click a browser cell first");
            else proc.reskip (cell);
            break;
        }
        case 3:
        {
            const int cell = selectedCell >= 0 ? selectedCell : bank::kCells - 1;
            selectedCell = cell;
            proc.captureInput (cell);
            break;
        }
        case 4:
        {
            const auto dir = OpProcessor::userFolder();
            dir.createDirectory();
            chooser = std::make_unique<juce::FileChooser> ("Render to file",
                dir.getChildFile ("openprocess " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H.%M.%S") + ".wav"), "*.wav");
            chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                                  [this] (const juce::FileChooser& fc) {
                                      const auto f = fc.getResult();
                                      if (f != juce::File()) proc.renderToFile (f.withFileExtension ("wav"));
                                  });
            break;
        }
        default:
            if (playing) proc.setStatus ("stop the timeline to change things");
            else { proc.clearMix(); selectedItem = -1; }
            break;
    }
}

//== demo =======================================================================
void OpEditor::startDemo()
{
    if (demo) return;
    if (proc.timelinePlaying()) proc.toggleTimeline();
    demoSaved = proc.getModel();
    demoSaved.timelinePlaying = false;
    Model fresh;
    fresh.nextId = demoSaved.nextId + 1000;
    proc.setModel (fresh);
    cur = fresh;
    disp.clear();
    selectedItem = -1;
    helpPage = -1;
    demo = true;
    demoIndex = 0;
    demoCursor = { 620, 420 };
    setupDemoStep();
}

void OpEditor::endDemo()
{
    if (! demo) return;
    demo = false;
    demoCarry = false;
    proc.setModel (demoSaved);
    disp.clear();
    selectedItem = -1;
    noteInput();
}

void OpEditor::setupDemoStep()
{
    demoKind = kDemo[demoIndex % kDemoLen];
    demoStepStart = juce::Time::getMillisecondCounter();
    demoActed = false;
    demoFrom = demoCursor;
    const auto m = mixRect();
    auto randomMixPoint = [&] { return Pt (m.getX() + 70 + rng.nextFloat() * (m.getWidth() - 140), m.getY() + 40 + rng.nextFloat() * (m.getHeight() - 90)); };

    if (playing && demoKind != 8 && demoKind != 9) demoKind = 9;
    if ((demoKind == 1 || demoKind == 7) && cur.mix.empty()) demoKind = 0;
    if (demoKind == 7 && cur.mix.size() < 4) demoKind = 3;
    if (demoKind == 6 && cur.snaps.empty()) demoKind = 5;
    if (demoKind == 8 && cur.segs.empty()) demoKind = 9;

    switch (demoKind)
    {
        case 0: demoArg = rng.nextInt (bank::kCells); demoFrom = cellRect (demoArg).getCentre(); demoTo = randomMixPoint(); break;
        case 1:
        {
            const int choices[] = { 0, 1, 2, 0, 1, 2, 3 };
            demoArg = choices[rng.nextInt (7)];
            demoFrom = fxRect (demoArg).withWidth (34).getCentre();
            const auto& it = cur.mix[(size_t) rng.nextInt ((int) cur.mix.size())];
            demoTo = barRect (it.id, it).getCentre();
            break;
        }
        case 2: demoTo = opRect (0).getCentre(); break;
        case 3: demoTo = opRect (1).getCentre(); break;
        case 4: demoArg = rng.nextInt (8); demoTo = presetRect (demoArg).getCentre(); break;
        case 5: demoTo = snapButtonRect().getCentre().translated (-4, -10); break;
        case 6:
        {
            demoFrom = reelRect ((int) cur.snaps.size() - 1).getCentre();
            const auto t = timelineRect();
            demoTo = { t.getX() + t.getWidth() * (0.12f + 0.3f * (float) (cur.segs.size() % 3)), t.getCentreY() };
            break;
        }
        case 7:
        {
            demoArg = cur.mix.front().id;
            demoFrom = barRect (cur.mix.front().id, cur.mix.front()).getCentre();
            demoTo = { demoFrom.x, (float) H + 30 };
            break;
        }
        case 8: demoTo = flagRect().getCentre().translated (0, -14); break;
        default: demoTo = demoFrom; break;
    }
}

void OpEditor::demoStep()
{
    const float t = (float) (juce::Time::getMillisecondCounter() - demoStepStart) / (float) kDemoStepMs;
    const bool carries = demoKind == 0 || demoKind == 1 || demoKind == 6 || demoKind == 7;
    const float mv = ease ((t - 0.15f) / 0.6f);
    demoCursor = demoFrom + (demoTo - demoFrom) * mv;
    demoCarry = carries && t > 0.12f && t < 0.75f;
    if (demoKind == 7 && t > 0.12f && t < 0.75f) disp[demoArg] = demoCursor;

    if (t >= 0.75f && ! demoActed)
    {
        demoActed = true;
        switch (demoKind)
        {
            case 0:
            {
                float x, y;
                posToXY (demoTo, x, y);
                const int id = proc.addItem (demoArg, x, y);
                if (id >= 0) disp[id] = demoTo;
                break;
            }
            case 1:
            {
                const int id = itemAt (demoTo);
                const int f = demoArg;
                proc.edit ([&] (Model& m) { if (auto* i = m.findItem (id)) i->effect = f; });
                break;
            }
            case 2: proc.operatorLength(); opFlash[0] = 1; break;
            case 3: proc.operatorPosition(); opFlash[1] = 1; break;
            case 4: spawnFrom = presetRect (demoArg).getCentre(); proc.applyPreset (demoArg, false); presetFlash[demoArg] = 1; break;
            case 5: if (proc.takeSnapshot() >= 0) snapFlash = 1; break;
            case 6:
            {
                const auto tr = timelineRect();
                if (! cur.snaps.empty()) addSegmentAt (cur.snaps.back().id, (demoTo.x - tr.getX()) / tr.getWidth());
                break;
            }
            case 7:
                proc.edit ([&] (Model& m) {
                    m.mix.erase (std::remove_if (m.mix.begin(), m.mix.end(), [&] (const Item& i) { return i.id == demoArg; }), m.mix.end());
                });
                break;
            case 8: proc.toggleTimeline(); break;
            default: break;
        }
    }
    if (t >= 1.0f)
    {
        ++demoIndex;
        setupDemoStep();
    }
}
