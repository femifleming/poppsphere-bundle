// The ovalprocess screen: browser on top, mix window in the middle, effects on the
// right, presets on the left, snapshots and timeline at the bottom.
#pragma once
#include "PluginProcessor.h"
#include <deque>
#include <map>
#include <optional>

class OpEditor : public juce::AudioProcessorEditor,
                 public juce::FileDragAndDropTarget,
                 private juce::Timer
{
public:
    explicit OpEditor (OpProcessor&);
    ~OpEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMagnify (const juce::MouseEvent&, float) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void fileDragMove (const juce::StringArray&, int, int) override;
    void fileDragExit (const juce::StringArray&) override { dropCell = -2; }
    void filesDropped (const juce::StringArray&, int, int) override;

    static constexpr int W = 1000, H = 720;

    // for the test tool
    void setHelpPage (int p) { helpPage = p; }
    void startDemo();
    void tick() { timerCallback(); }
    void selectItem (int id) { selectedItem = id; }
    void setGlobeView (float yaw, float pitch) { viewYaw = yaw; viewPitch = pitch; }
    void setTimelineView (float start, float len) { viewStart = start; viewLen = len; clampView(); }

private:
    using Pt = juce::Point<float>;
    using Rect = juce::Rectangle<float>;

    struct DrawItem { int voiceId; Item item; bool live; };

    void timerCallback() override;
    Pt toCanvas (Pt p) const { return p / scale; }
    void noteInput() { lastInput = juce::Time::getMillisecondCounter(); }

    // geometry (canvas units)
    static Rect mixRect() { return { 150, 232, 730, 326 }; }
    static Rect cellRect (int c) { return { 300.0f + (c % 8) * 24.0f, 20.0f + (c / 8) * 24.0f, 20, 20 }; }
    static Rect browserRect() { return { 296, 16, 196, 196 }; }
    static Rect fxRect (int i) { return { 892, 238.0f + i * 64.0f, 96, 56 }; }
    static Rect presetRect (int k) { return { 12.0f + (k % 4) * 33.0f, 286.0f + (k / 4) * 144.0f, 31, 88 }; }
    static Rect opRect (int i) { return { i == 0 ? 700.0f : 786.0f, 150, 52, 52 }; }
    static Rect snapButtonRect() { return { 636, 516, 130, 74 }; }
    static Rect reelRect (int i) { return { 380.0f + i * 46.0f, 598, 40, 34 }; }
    static Rect wheelRect() { return { 232, 634, 66, 66 }; }
    static Rect timelineRect() { return { 330, 668, 540, 10 }; }
    static Rect timelineZone() { return { 318, 618, 566, 96 }; }       // where the mouse wheel zooms the timeline
    static Rect overviewRect() { return { 330, 703, 540, 6 }; }        // the whole composition, when zoomed in
    static Rect qRect (int i) { return { 196.0f + i * 22.0f, 560, 18, 18 }; }
    static Rect logoRect() { return { 16, 552, 178, 34 }; }
    static Rect endDemoRect() { return { 176, 582, 80, 16 }; }
    static Rect toolRect (int i) { return { 878.0f + (i % 3) * 38.0f, 612.0f + (i / 3) * 42.0f, 30, 30 }; }
    // opensphere
    static Pt planCentre() { return { 146, 114 }; }
    static constexpr float kPlanR = 84.0f;
    static Rect motionRect (int i) { return { 598.0f + (i % 6) * 44.0f, 16.0f + (i / 6) * 52.0f, 42, 30 }; }
    static Rect distRect (int k) { return { 868.0f, 12.0f + k * 22.0f, 122, 21 }; }   // label + rail
    static constexpr int kDistParams = 6;
    struct Proj { Pt p; float depth; };
    Proj project (float x, float y, float z) const;      // the globe: world (x front, y left, z up) -> screen
    bool globeAtTop() const { return std::abs (viewYaw) < 0.001f && std::abs (viewPitch) < 0.001f; }
    double tauOf (const Item& i) const { return motionT - i.motionStart; }
    void movedNow (const Item&, double clock, float& x, float& y, float& d) const;   // incl. the flock
    void posOf (const Item&, float& x, float& y, float& d) const;
    SpaceParams shownSpace() const;
    void drawDistance (juce::Graphics&);
    void drawMeter (juce::Graphics&);
    static Rect liveRect() { return { 512, 30, 26, 26 }; }
    static Rect orderRect (int k) { return { 857.0f + k * 19.0f, 168, 16, 16 }; }
    static float metres (float d) { return 0.25f + 19.75f * d * d; }
    Pt planPos (const Item&, double t) const;
    Rect attachedMotionRect (int voiceId, const Item&) const;
    int planItemAt (Pt) const;
    void drawPlan (juce::Graphics&);
    void drawMotions (juce::Graphics&);
    void drawOrder (juce::Graphics&);
    static void drawMotionIcon (juce::Graphics&, int type, Rect);
    static void drawLiveSquare (juce::Graphics&, Rect, float level);

    Pt itemPos (const Item&) const;
    void posToXY (Pt p, float& x, float& y) const;
    float barWidth (const Item&) const;
    Rect barRect (int voiceId, const Item&) const;
    Rect stripRect (int voiceId, const Item&) const;
    Rect attachedFxRect (int voiceId, const Item&) const;
    Rect flagRect() const;
    Rect segRect (const Segment&) const;
    // timeline zoom: the visible part of the composition, as fractions of its length
    float fracToX (float f) const;
    float xToFrac (float x) const;
    float fracPerPx() const { return viewLen / timelineRect().getWidth(); }
    void zoomTimeline (float factor, float anchorX);
    void clampView();
    void drawTimelineScale (juce::Graphics&);
    int itemAt (Pt) const;
    std::vector<DrawItem> visibleItems() const;

    // drawing
    void renderStatic (juce::Graphics&);
    void drawBrowser (juce::Graphics&);
    void drawItems (juce::Graphics&);
    void drawOperators (juce::Graphics&);
    void drawSnapButton (juce::Graphics&);
    void drawReels (juce::Graphics&);
    void drawTimeline (juce::Graphics&);
    void drawTools (juce::Graphics&);
    void drawGuide (juce::Graphics&);
    void drawHelp (juce::Graphics&);
    void drawDragGhost (juce::Graphics&);
    void drawCursor (juce::Graphics&, Pt, bool pressed);
    static void drawFxIcon (juce::Graphics&, int fxType, Rect);
    static void drawReel (juce::Graphics&, Rect, juce::Colour);
    static void drawTag (juce::Graphics&, Rect, const juce::String&, juce::Justification, juce::Colour bg, juce::Colour fg);
    static void drawCellSquare (juce::Graphics&, int cell, Rect, bool ready);
    void drawTextOnArc (juce::Graphics&, Pt centre, float radius, float startAngle, const juce::String&);
    static juce::Font labelFont (float size);
    static juce::Colour reelColour (int c);

    // actions
    void toolAction (int i);
    void endDemo();
    void demoStep();
    void setupDemoStep();
    void addSegmentAt (int snapId, float centreFrac);
    void drawPreset (juce::Graphics&, int k, Rect, float flash);

    OpProcessor& proc;
    Model cur;
    bool playing = false;
    double tlSeconds = 0;
    double motionT = 0;
    float viewYaw = 0.0f, viewPitch = 0.0f;           // how the globe is turned (the view only)
    float dragYaw = 0.0f, dragPitch = 0.0f;
    std::array<float, 28> lvAz = filled<28> (-100.0f);
    std::array<float, 10> lvEl = filled<10> (-100.0f);
    template <size_t N> static std::array<float, N> filled (float v) { std::array<float, N> a; a.fill (v); return a; }
    float lvPeak = -100.0f;
    int ambiOrder = 0;
    float inLevel = 0;
    float viewStart = 0.0f, viewLen = 1.0f;
    float scale = 1.0f;
    juce::Image staticLayer;
    std::array<double, bank::kCells> cellSecs {};
    std::array<bool, bank::kCells> cellOk {};
    std::array<juce::Image, 8> glyphs;
    Rect dragStrip;
    bool demoActed = false;

    std::map<int, Pt> disp;           // displayed item positions (animated)
    std::optional<Pt> spawnFrom;
    int selectedItem = -1, selectedCell = -1;
    int helpPage = -1;                // -1 none, 0 guide, 1 & 2 text pages
    int hoverTool = -1;
    int dropCell = -2;                // file drag hover: -1 browser, >=0 cell
    float presetFlash[8] {}, opFlash[2] {}, snapFlash = 0;

    enum class Drag { None, Cell, Item, LoopS, LoopE, Effect, AttachedFx, Reel, Seg, SegResize, Flag, Wheel, View, Motion, AttachedMotion, Plan, Live, GlobeTurn, SceneTurn, Dist };
    Drag drag = Drag::None;
    int dragIndex = -1;
    Pt downPt, curPt, grabOffset;
    float dragStartVal = 0;
    Model wheelStart;
    bool moved = false;
    std::deque<Pt> trail;

    // demo / tutorial
    bool demo = false;
    Model demoSaved;
    int demoIndex = 0;
    juce::uint32 demoStepStart = 0;
    Pt demoFrom, demoTo, demoCursor;
    int demoKind = 0, demoArg = 0;
    bool demoCarry = false;
    juce::Random rng;
    juce::uint32 lastInput = 0;

    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OpEditor)
};
