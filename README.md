<p align="center"><img src="openprocess/Resources/icon/openprocess_1024.png" width="96" alt="openprocess icon">&nbsp;&nbsp;<img src="openprocess/Resources/icon/openfield_1024.png" width="96" alt="openfield icon">&nbsp;&nbsp;<img src="quell/Resources/icon/icon_1024.png" width="96" alt="quell icon"></p>

# poppsphere bundle

Three instruments and effects in one download, for **macOS, Windows and Linux**, each with a PDF manual.

| | what it is | formats |
|---|---|---|
| **[openprocess](openprocess/)** | a loop workspace after Oval's *ovalprocess*: an 8 × 8 browser of loops, a play area (left/right, loud/soft), effects, presets, snapshots and a timeline up to **30 minutes** that zooms, cutting or **gliding smoothly** between scenes | VST3 · VST2 · AU · LV2 · app |
| **[openfield](openprocess/README.md#openfield)** | openprocess as an **Ambisonics encoder**: the play area is the sphere around you, a plan view sets distance, sounds orbit, swing, rise and drift, the live input can be placed in the space; ACN/SN3D 1st–7th order (up to 64 channels), AmbiX renders | VST3 · VST2 · AU · LV2 · app |
| **[Quell](quell/)** | a dynamic resonance suppressor: hundreds of tiny compressors across the spectrum, cutting peaks only while they ring | VST3 · VST2 · AU · LV2 · app |

Manuals: [openprocess](openprocess/docs/manual/openprocess%20Manual.pdf) ·
[openfield](openprocess/docs/manual-field/openfield%20Manual.pdf) ·
[Quell](quell/docs/manual/manual.pdf) (also inside every download).

## Download and install

Get the zip for your system from **[Releases](https://github.com/femifleming/poppsphere-bundle/releases/latest)**:
`poppsphere-bundle-macOS.zip`, `poppsphere-bundle-Windows.zip` or `poppsphere-bundle-Linux.tar.gz`.
Each holds one folder per product, with its manual.

| system | VST3 | other formats | app |
|---|---|---|---|
| macOS | `~/Library/Audio/Plug-Ins/VST3` | `.component` → `~/Library/Audio/Plug-Ins/Components`, `.vst` → `~/Library/Audio/Plug-Ins/VST` | `.app` → `/Applications` |
| Windows | `C:\Program Files\Common Files\VST3` | `.dll` (VST2) → your VST folder | `.exe` |
| Linux | `~/.vst3` | `.lv2` → `~/.lv2`, `.so` (VST2) → `~/.vst` | run the binary (ALSA or JACK) |

On a Mac, Homebrew installs them in one line each (first `brew tap femifleming/tap`):

```
brew install --cask openprocess openfield quell
```

The macOS builds are universal (Apple silicon + Intel) and signed ad hoc, not notarised: the Homebrew casks clear the
quarantine flag; if you install by hand, right-click the app → Open the first time, or run
`xattr -dr com.apple.quarantine <file>`.

## Build from source

```
git clone --recursive https://github.com/femifleming/poppsphere-bundle
cd poppsphere-bundle
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release        # macOS universal: add -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build --target openprocess_All openfield_All Quell_All
```

Tests: `openprocessTest`, `openfieldTest`, `QuellTest` (same build). Linux needs the usual JUCE packages
(see `.github/workflows/build.yml`). Every push builds and tests all three on all three systems; a `v*` tag publishes
a release.

## Licence

AGPL-3.0 (JUCE). VST2 support uses the GPL-3.0 FST headers in `third_party/vst2-fst`. VST is a trademark of
Steinberg Media Technologies GmbH. openprocess is an independent homage to ovalprocess, not made or endorsed by
Markus Popp / Oval. Quell is not affiliated with oeksound.
