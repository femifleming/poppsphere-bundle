# openprocess

A VST3 / VST2 / AU / LV2 plug-in (an instrument, and an effect called **openprocess fx**) and standalone app modelled on **ovalprocess** (Markus Popp / Oval, 2000;
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
- **Glides**: a segment can cut to its scene or glide into it — drag its left end (or double-click it) and the
  scene before morphs smoothly into it: positions, loop parts, freeze rails (and in openfield distance, rails and
  scene turn) slide over, shared sounds keep playing, the rest cross-fade.
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

## In a DAW

**Two plug-ins.** `openprocess` is an instrument: put it on an instrument / MIDI track; it plays as soon as something is
in the play area. `openprocess fx` is the same workspace as an effect: put it on an audio track or after an instrument.
The track passes through it (Input Thru starts at 100 %), the loops play on top, and *capture input* chops the last
8 s the track played into a browser cell. Sessions of the two are interchangeable. The instrument's audio input is a
side-chain: route another track into it if the host can.

**Transport.** The switch **flag | host** (bottom left, plug-ins only) says who starts the timeline. *host* (the
default, parameter *Follow Host*): while the DAW plays, the timeline plays at the DAW's position — 1:30 in the song is
1:30 on the timeline, past its end the piece comes round again, and an export or bounce contains it. When the DAW stops
(or the timeline is empty) the play area sounds, as always. *flag*: the red flag starts and stops it, as in the app.

**Parameters** (all automatable, IDs fixed):

| parameter | what it does |
|---|---|
| Master, Input Thru | output level; how much of the audio input is passed on |
| Timeline Play | the red flag |
| Follow Host | the flag / host switch |
| Recall Snapshot | –, 1 … 10: puts that reel back into the play area, on the sample block where the automation says so |
| Sound 01 … 24: Pan, Level, Loop Start, Loop Length | one set per sound of the play area. A sound takes the lowest free number when it enters; select it and its label shows it ("sound 03: glass 8"). Dragging the sound moves the parameters (so a host can record the move), and automation moves the sound. |

openfield has, per sound, *Around*, *Height*, *Distance*, *Loop Start*, *Loop Length*, and its order, distance rails,
motion speed and scene turn / tilt as before.

**Sessions** keep the play area, reels, timeline, composition length, how every browser cell was made (files by path),
all parameters and the editor's size. A reopened session never starts the timeline by hand or recalls a snapshot by itself.

**Ableton Live.** Browser → *Plug-Ins* → *VST3* (or *VST* / *Audio Units*) → *FreakiePie*. Drag `openprocess` onto a
MIDI track, or `openprocess fx` onto an audio track. To automate: click the plug-in's *Configure* button and move a
sound; its parameters appear in the device. After updating: *Settings → Plug-Ins → Rescan*.

**REAPER.** Either kind goes on any track. For openfield make the track 16 channels wide (3rd order; 36 for 5th, 64
for 7th) before inserting it; the VST3 and VST2 then send that many channels, and a decoder follows on the same track.

Manual: [docs/manual/openprocess Manual.pdf](docs/manual/openprocess%20Manual.pdf) (source `docs/manual/manual.html`, printed with headless Chrome).

## openfield

(Called opensphere up to 1.2; renamed openfield in 1.3, with a green field.)

The same workspace as an **Ambisonics encoder** (built from these sources with `OP_SPATIAL=1`; plug-in code `OpSp`),
as an instrument and as an effect, **openfield fx** (`OpSx`): an encoder for the track it sits on, which starts with
the track's audio placed straight ahead of you.
The play area becomes the sphere around you unrolled (left–right = once round you, middle = front, edges = behind;
bottom–top = 45° below to overhead), a plan view (top left) sets distance (near = loud, wide, bright; far = quiet,
narrow, dull), motions (orbit, swing, rise, drift) are dragged onto sounds like effects, and the red **in** square places
the live input in the space. Output: ACN / SN3D (AmbiX), 1st–7th order (up to 64 channels; 3rd order as an AU); on a
stereo bus or in the desktop app a W±Y stereo preview. Render writes a 24-bit AmbiX WAV at the chosen order.
Manual: [docs/manual-field/openfield Manual.pdf](docs/manual-field/openfield%20Manual.pdf).

## Build

From the top of the bundle (JUCE and the VST2 headers live there):

```
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target openprocess_All openfield_All openprocess_fx_All openfield_fx_All openprocessTest openfieldTest
build/openprocess/openprocessTest_artefacts/Release/openprocessTest [--render dir] [--screenshots dir]
build/openprocess/openfieldTest_artefacts/Release/openfieldTest [--render dir] [--screenshots dir]
```

On macOS add `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"` for a universal build. VST2 uses the FST headers in
`../third_party/vst2-fst`.
