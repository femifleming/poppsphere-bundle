# Requests

Typed requests that changed this bundle (earlier ones are in the commit history).

## 2026-11-15
- "one of the vst versions of quell made my stereo mono. vst3 versions works fine." → Quell and openprocess only accept stereo outside VST3 (VST2 / AU / LV2 have no real bus negotiation, so offering mono let the host fold a stereo track to one channel). Mono stays available in VST3. Released as v1.3.1.
