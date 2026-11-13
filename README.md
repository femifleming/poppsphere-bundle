# Quell

A dynamic resonance suppressor (VST3 / VST2 / AU / standalone, macOS universal). It follows the feature set described
in the soothe3 user manual — with its own name, look and DSP — and finds resonances as they happen and notches them
out only while they ring.

## Controls

- **Soft / Hard** — Soft uses an adaptive threshold (relative to the local spectral envelope, independent of level, low distortion).
  Hard is level-dependent: a fixed threshold that falls as Depth rises, for firmer control or heavy sound design.
- **Depth** (0–20) amount of reduction · **Detail** (0–10) width of the cuts, higher = narrower and deeper ·
  **Attack** / **Release** (0–10) reaction and recovery speed, frequency-dependent (faster at high frequencies).
- **Graph** — reduction (white = L/M, grey = R/S), the faint input spectrum, and the depth curve (orange; grey for R/S
  when band focus splits it). Up to 8 bands: low cut, low shelf, high shelf, high cut, bell, band pass, band reject, tilt.
  Boosting a band means more suppression there, cutting means less; cut bands stop processing outside their range; band
  passes process only their own regions (in parallel).
  - double-click: add a bell (near the ends a low or high cut, with cmd a band pass); right-click: choose the shape
  - drag a node (shift = fine, cmd = Q/slope, alt = listen while dragging); scroll a node: Q (cmd: focus)
  - double-click a node: enable/bypass; shift-click / drag a box: select several; delete key removes them
  - scroll on empty space: change the graph range (6–48 dB)
- **Band strip** — shape, frequency (type `6k3`, `6.3k`, `6300`…), depth, Q/slope, focus, on, listen (delta of the band's region), delete.
- **Side panel** — stereo mode L/R or M/S, Link (0% dual mono … 100% summed analysis), Focus; Detail/Attack/Release tilt
  for below ~500 Hz and above ~2 kHz; Max cut (grey area on the graph); Wet trim.
- **Footer** — Bypass (soft, keeps processing), Delta (what is removed), Mix, Out gain, Quality (Normal / High / Ultra time
  resolution, plus Linear phase), Low latency, reduction meters.
- **Toolbar** — external sidechain (SC) + listen + level, presets (factory, user save/load in
  `~/Library/Application Support/Quell/Presets`), undo/redo (cmd-Z / shift-cmd-Z), A/B + copy, settings (always render on
  Ultra when bouncing offline, frequency readout, window size S/M/L/XL; the window also resizes from the corner).

Every control: drag, shift-drag for fine, double-click to type, alt-click to reset.

## Latency

| mode | 44.1/48 kHz | 88.2/96 kHz | 176.4/192 kHz |
|---|---|---|---|
| minimum phase (default) | 2048 | 4096 | 8192 |
| linear phase | 3072 | 6144 | 12288 |
| low latency | 0 | 0 | 0 |

## How it works

Every hop (256/128/64 samples at 48 kHz for Normal/High/Ultra) the last 2048 samples are Hann-windowed and analysed with
a zero-padded 4096 FFT. Per bin: level in dB (L/R or M/S, blended toward the channel average by Link) → excess above a
log-frequency-smoothed envelope (Detail sets its width) → Soft knee or Hard threshold → smoothing across frequency
(Detail) → per-bin attack/release → × depth curve → clamp to Max cut. The gain curve (with Mix, Wet trim, Delta and band
listen folded in) becomes a minimum-phase filter (folded cepstrum) or a linear-phase one, its impulse response windowed so
the overlap-add of the zero-padded frames is a true linear convolution. Low latency runs the same minimum-phase filter
truncated to 256 taps directly on the signal, crossfaded each hop.

## Build

```
git submodule update --init
cmake -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DQUELL_COPY_AFTER_BUILD=ON
cmake --build build-release --target Quell_All
cmake --build build-release --target QuellTest && build-release/QuellTest_artefacts/Release/QuellTest
```

`QuellTest` checks nulls at zero depth for every path, latency alignment, resonance reduction, wet + delta = dry,
random-settings stability, state/A-B round trip and speed. `--shot file.png [bands]` renders the window; `--wav dir` writes renders.

VST2 uses the FST headers in `third_party/vst2-fst`.
