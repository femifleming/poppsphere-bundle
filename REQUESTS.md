# Requests

Typed requests that changed this bundle (earlier ones are in the commit history).

## 2026-11-15
- "one of the vst versions of quell made my stereo mono. vst3 versions works fine." → Quell and openprocess only accept stereo outside VST3 (VST2 / AU / LV2 have no real bus negotiation, so offering mono let the host fold a stereo track to one channel). Mono stays available in VST3. Released as v1.3.1.

## 2026-11-17
- "make sure all vsts work well. some vsts mono the sound as opposed to the vst3 that works well, just double check."
- "also make openprocess and openfield vsts not just standalone apps"

## 2026-11-18
- "Convert OpenProcess and OpenField from standalone applications into proper VST plugins. Requirements: Preserve their existing functionality and core concepts. Adapt their interfaces and interaction models for use within a DAW. Ensure correct plugin parameter handling and host integration. Verify that they load and function correctly in Ableton Live. Test audio processing, param…" (pasted list, cut off here in the log: parameter automation, state restoration, stability)

→ 1.4.0. openprocess and openfield were already installed as VST3 / VST2 / AU, but only as *instruments*, with two automatable
parameters and no connection to the host's transport. Now each also comes as an effect (**openprocess fx**, **openfield fx**) for
audio tracks; the timeline can run with the host's transport (**flag | host**); every sound in the play area has its own
automatable parameters (place, level, loop part), plus Timeline Play, Follow Host and Recall Snapshot; the editor's size is kept
with the session; the VST3s no longer list 2080 unused "MIDI CC" parameters; openfield's VST3 follows the width of the track
in REAPER, and its VST2 is a stereo plug-in in hosts whose tracks are stereo (so W / Y are never played as left / right).
Checked in REAPER for VST3, VST2 and AU: loading, audio, channels, automation, sessions.
