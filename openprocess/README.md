# openprocess

A VST3 / VST2 / AU instrument and standalone app modelled on **ovalprocess** (Markus Popp / Oval, 2000;
software by Richard Ross), the "interactive authoring environment for personal contemporary desktop audio"
that ran in Oval's public sound terminals. Layout and behaviour follow the installation manual
(*Ovalprocess sound installation – Dokumentation, Bedienungs- und Wartungshinweise*, Ars Electronica archive)
and its screenshots.

There are no scales, notes, tempo or bars, as in the original: everything loops all the time and you
change it while it plays.

## The screen (top to bottom, as in the original)

- **Browser**: 8 x 8 colour-coded loops. Drag one into the play area to start it.
- **Play area**: left/right = panorama, up/down = loud/soft. Each sound loops; a black pointer runs over it.
  Click a sound to get **two red pointers** that set which part of the sample loops.
  Throw a sound out of the play area to stop it.
- **Effects** (right): fade in+out, fade in, fade out, mute, solo. Drag onto a sound; drag off to remove.
- **Presets** (8 brackets, left): a new, unpredictable mix from a pre-sorted pool. Shift-click adds.
- **Logical operators** (top right): random loop lengths / random positions for everything in the play area.
- **Snapshot** (yellow button): store the play area as a reel.
- **Timeline**: drag reels onto it, move/scale segments (alt-drag copies, drag away deletes).
  The wheel sets the composition length (1–30 min; shift-drag keeps segment times). Mouse wheel / pinch over the
  timeline zooms (down to 10 s across), sideways scroll moves along it, double-click shows everything; an overview
  strip appears when zoomed. The red flag plays/stops (drag it to set the start).
  While the timeline plays nothing can be edited, as in the original.
- **? ? ?**: guide labels, play-area help, timeline/sounds help. Click the word mark for the animated demo
  (the standalone starts it by itself after 3 idle minutes, and puts your work back when you click).

## Sounds

The original Oval sound-file archive is not included. The built-in bank is synthesized: eight rows
(organ, guitar, feedback — the three timbres Popp set for the *ovalprocess* CD — then skips, glass, low,
noise, skipped pads), all in one pitch set so any loops combine, each chopped into a loop by a
"CD skip" process (tiny fragments, repeats, jumps back, read errors). Columns go from clean (left) to heavily skipped (right).

Drop audio files or a folder on the browser (or use the **load** tool) to chop your own audio into 64 loops
the same way. Dropping one file on one cell replaces only that cell.

Tools (bottom right): load sounds · built-in sounds · skip again (re-chop the selected cell) ·
capture input (last 8 s of audio input into the selected cell; saved in `~/Music/openprocess/captures`) ·
render to file (48 kHz 16-bit WAV of the timeline, or of the play area if the timeline is empty; streamed to disk) · clear.

MIDI: C1–G1 presets, G#1 / A1 logical operators, A#1 snapshot, B1 timeline play/stop, C2 upwards toggle the 64 cells.
Parameters: Master, Input Thru.

Manual: [docs/manual/openprocess Manual.pdf](docs/manual/openprocess%20Manual.pdf) (source `docs/manual/manual.html`, printed with headless Chrome).

## opensphere

The same workspace as an **Ambisonics encoder** (built from these sources with `OP_SPATIAL=1`; plug-in code `OpSp`).
The play area becomes the sphere around you unrolled (left–right = once round you, middle = front, edges = behind;
bottom–top = 45° below to overhead), a plan view (top left) sets distance (near = loud, wide, bright; far = quiet,
narrow, dull), motions (orbit, swing, rise, drift) are dragged onto sounds like effects, and the red **in** square places
the live input in the space. Output: ACN / SN3D (AmbiX), 1st–7th order (up to 64 channels; 3rd order as an AU); on a
stereo bus or in the desktop app a W±Y stereo preview. Render writes a 24-bit AmbiX WAV at the chosen order.
Manual: [docs/manual-sphere/opensphere Manual.pdf](docs/manual-sphere/opensphere%20Manual.pdf).

## Build

From the top of the bundle (JUCE and the VST2 headers live there):

```
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target openprocess_All opensphere_All openprocessTest opensphereTest
build/openprocess/openprocessTest_artefacts/Release/openprocessTest [--render dir] [--screenshots dir]
build/openprocess/opensphereTest_artefacts/Release/opensphereTest [--render dir] [--screenshots dir]
```

On macOS add `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"` for a universal build. VST2 uses the FST headers in
`../third_party/vst2-fst`.
