# Spaces

**Spaces** by Gilly is a mix-referencing plugin (AU / VST3, macOS). It places your mix inside a real recorded space so you can hear how it translates to a club. Two things make that work: impulse responses captured in the room, and a looped recording of the room's own ambience.

This is the **v0.1 demo** with one space: **Corsica Studios, London** (Room 1 and Room 2).

## Download and install

Every push builds the plugin on GitHub:

1. Open the repo's **Actions** tab and click the latest green **Build** run.
2. At the bottom, under **Artifacts**, download **Spaces-macOS**.
3. Unzip it and follow `INSTALL.txt`. The short version:
   ```sh
   cp -R Spaces.component ~/Library/Audio/Plug-Ins/Components/
   cp -R Spaces.vst3      ~/Library/Audio/Plug-Ins/VST3/
   xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/Spaces.component ~/Library/Audio/Plug-Ins/VST3/Spaces.vst3
   ```

The build is a universal binary (Apple Silicon + Intel). It's ad-hoc signed but not notarised, which is why the `xattr` step is needed.

Pushing a tag such as `v0.1.0` also attaches the zip to a GitHub Release.

## Signal flow

```
Stereo in ─┬────────────────────────────────────────────────── Dry ─┐
           ├─► Direct: Conv(IR, first N ms) → Drive → EQ → Level ─┐  │
           └─► Room:   Conv(IR, full/trimmed) → Drive → EQ → Level ┴─► Auto gain ─┤
                                                                               Dry/Wet → Mono
                                                    Ambience loop → Level ──────► (+)
                                                                               Output trim → VU → Out
```

* **True stereo.** Each IR position is a pair of captures: the left speaker and the right speaker, each recorded with a stereo mic pair. That gives four paths (L→L, L→R, R→L, R→R). The iPhone captures are plain stereo (L→L, R→R).
* **Zero latency.** Convolution uses non-uniform partitioning. The plugin reports 0 samples of latency. The drive's 2× oversampling filters add a fraction of a sample to the wet path only.
* **Click-free switching.** A new IR is built on a background thread. It then runs silently on the live input for 100 ms, so its reverb has the right "history", and crossfades in over 100 ms. This is a small change to a local copy of `juce::dsp::Convolution` (`source/dsp/WarmConvolution.*`). Stock JUCE fades in a cold engine, which gives a faint tick on steady material.
* **Level calibration.** Each IR is calibrated so its response to centred, K-weighted pink noise matches the input. Switching rooms or IRs therefore stays roughly level before the slow auto gain has done anything. With the default levels, the remaining correction on the Corsica IRs is within −1.2 to +3.4 dB.
* **Auto gain** measures K-weighted loudness of the input and the wet signal over a 3 s window. It eases the difference in with a 1.5 s time constant, limited to ±18 dB, and freezes below −50 dBFS input. The ambience is never measured.
* **Room Size** scales the room's decay time (RT60) from 25% to 300%, keeping the direct sound and first 50 ms of reflections as recorded. Above 100%, once the recorded tail gets close to the noise floor, it is continued with grains taken from its own late part, decorrelated per path, with the highs damped progressively. The recordings are only about 1 s long, so this is what lets the room ring longer.
* **Pan** (in the EQ drawer, per layer) is a balance control for IRs that lean to one side: it turns the opposite side down and leaves centre at unity.
* **Ambience** runs from the plugin's own sample counter, not the host transport. It's loaded into memory and resampled to the session rate (with a band-limited, loop-aware resampler, so the loop stays seamless), then shared between instances. It's added after dry/wet and mono, so it stays stereo and unaffected by both.

## Ambience with the transport stopped

The ambience keeps playing whenever the host calls the plugin. To make hosts do that, Spaces reports an infinite tail: `kInfiniteTail` for VST3, and 24 hours for AU, which has no "infinite" value.

* **Ableton Live:** devices process continuously while the audio engine is on, whatever the transport is doing, so the ambience should always play.
* **Logic Pro:** Logic only processes a track when something needs it: during playback, for the selected ("live") track, or while a plugin's tail is still running. The long tail should keep Spaces running after you stop. If the ambience stops on an unselected track, select the track, or put Spaces on a bus or the Stereo Out.

These are expected behaviours. They haven't been checked in either host yet, so please confirm with the checklist below.

## Host test checklist

Things the automated tests can't cover:

- [ ] AU and VST3 both appear and load in Logic and in Ableton Live.
- [ ] Ambience keeps playing with the transport stopped, in both hosts (and on an unselected track in Logic).
- [ ] Flicking through presets, rooms and IRs while music plays: no clicks or level jumps.
- [ ] Auto gain: toggling Spaces on and off with Bypass sounds level-matched after a couple of seconds, with no pumping.
- [ ] Saving and reopening a session restores the room, IR, ambience and all knobs.
- [ ] UI scaling (75–150%) works and looks right on a Retina screen.

## Automated tests

Each CI run does the following:

* `SpacesTests` (offline): loads the real processor and checks:
  * the pack loads;
  * room switching applies room defaults;
  * state save/restore works;
  * the ambience loops sample-accurately and keeps playing with silent input;
  * **no heap allocation or freeing in `processBlock`** while switching rooms, IRs, presets and Length;
  * **click detection** on every switch (high-passed output during a switch of a steady sine; currently below −80 dBFS);
  * **gain match** within 1 dB on program material for all 21 IRs (worst case currently 0.2 dB), with less than 1 dB of correction swing (no pumping);
  * rendering at 44.1–192 kHz.
* **pluginval** at strictness 10 on VST3 (macOS + Linux) and AU (macOS).
* **auval** (`auval -strict -v aufx Spcs Gily`).

The CPU use measured in the tests is about 7% of one core at 48 kHz, and about 22% at 96 kHz.

## Building from source

Requirements: CMake 3.22+, a C++20 compiler (Xcode 15+ on macOS) and Ninja (optional). JUCE 8.0.9 is fetched automatically.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DSPACES_BUILD_TESTS=ON
cmake --build build
./build/SpacesTests_artefacts/Release/SpacesTests            # engine checks
./build/SpacesTests_artefacts/Release/SpacesTests --dump out/ # render WAVs to listen to
```

Plugins end up in `build/Spaces_artefacts/Release/{AU,VST3}`. The `spaces/` folder is copied into each bundle's `Contents/Resources/Packs` at build time.

## Adding a new space

A space is a folder holding a `space.json` manifest and the files it references. Adding one needs no code changes:

```
spaces/
  corsica/
    space.json
    irs/…  ambience/…  images/…
```

Spaces looks for packs in these places, in order (the first pack with a given `id` wins):

1. `$SPACES_PACKS_DIR`, for development.
2. `~/Music/Spaces/Packs/`, for user-installed expansion packs.
3. Inside the plugin bundle: `Contents/Resources/Packs/`, for the built-in packs.

The manifest looks like this:

```jsonc
{
  "schema": 1,
  "id": "corsica",                       // stable: saved in sessions
  "name": "Corsica Studios",
  "location": "London",
  "defaults": { "amb_level": -6.0 },     // any parameter ID -> value (real units)
  "rooms": [
    {
      "id": "room1", "name": "Room 1",
      "image": "images/Room 1.jpg",      // hero photo (JPEG/PNG)
      "heroFrames": { "folder": "images/room1-loop", "fps": 24 },   // optional animated hero
      "defaultIr": "middle-ortf",
      "defaultAmbience": "booth",
      "irs": [
        { "id": "middle-ortf", "name": "Middle · ORTF", "type": "trueStereo",
          "left": "irs/…L.aif", "right": "irs/…R.aif",
          "swapLR": false },             // swap mic channels (mics facing the crowd)
        { "id": "iphone", "name": "iPhone", "type": "stereo", "file": "irs/….aif" }
      ],
      "ambience": [
        { "id": "booth", "name": "Booth", "file": "ambience/….wav", "gainDb": -9.0 }
      ]
    }
  ],
  "presets": [
    // a full state saved from the plugin (Save as... in the preset menu)
    { "name": "Room 1 Back", "file": "presets/Room 1 Back.spacespreset" },
    // or a room/IR/ambience choice on top of the defaults
    { "name": "Room 1 Dancefloor", "room": "room1", "ir": "middle-ortf", "ambience": "booth",
      "params": { "direct_length": 20.0 },
      "hidden": true }                   // kept in the manifest, not shown in the list
  ]
}
```

Notes:

* IRs and loops can be WAV, AIFF or FLAC, at any sample rate.
* Ambience files must already be perfect loops. If the last frame repeats the first, it's dropped automatically.
* To turn your own presets into factory presets, copy the `.spacespreset` files from `~/Music/Spaces/Presets` into the pack's `presets/` folder and list them in the manifest. The first visible preset is what a new instance starts on.
* Parameter IDs are listed in `source/Params.h`. Never rename them, because sessions and presets store them.
* Sessions store the selection by `id` as well as by index, so you can add or reorder IRs without breaking saved sessions.

## Corsica asset mapping

Room 1 is two recording sessions: files prefixed "2 Room 1" are the second session.

| Room | Display name | Files (in `spaces/corsica/irs/`) | Notes |
|---|---|---|---|
| 1 | Front · ORTF | `2 Room 1 - Front ORTF …` L/R | |
| 1 | Front · Spaced Pair | `Room 1 - Spaced Pair, At the Front. Louder` L/R | |
| 1 | **Middle · ORTF** (default) | `2 Room 1 - Middle ORTF …` L/R | |
| 1 | Middle · ORTF (take 2) | `Room 1 - Middle ORTF …` L/R | first session |
| 1 | Middle · Spaced Pair | `2 Room 1 - Middle Spaced Pair …` L/R | |
| 1 | Spaced Pair 1m | `Room 1 - Spaced Pair 1m Louder …` L/R | |
| 1 | Spaced Pair 1m (medium vol) | `Room 1 - Spaced Pair 1m Medium …` L/R | |
| 1 | Back · ORTF | `2 Room 1 - Back ORTF …` L/R | |
| 1 | Back · Spaced Pair | `2 Room 1 - Back Spaced Pair … 45s Sweep` L/R | |
| 1 | DJ Booth · Binaural | `2 Room 1 - Booth Binural …` L/R | `swapLR` |
| 1 | iPhone · Middle | `2 Room 1 -iPhone Middle` | stereo |
| 2 | Front · ORTF | `Room 2 - ORTF Front` L/R | |
| 2 | Middle Front · ORTF | `Room 2 - ORTF Middle Front` L/R | |
| 2 | Middle Front · ORTF (−5 dB) | `… Middle Front, -5db less` L/R | |
| 2 | **Middle · ORTF** (default) | `Room 2 - ORTF Middle` L/R | |
| 2 | Middle · ORTF (med vol) | `… Middle, med volume` L/R | |
| 2 | Back · ORTF | `Room 2 - ORTF Back` L/R | |
| 2 | Right Side · ORTF | `Room 2 - ORTF Right Hand Side` L/R | |
| 2 | DJ Booth · ORTF | `Room 2 Booth ORTF` L/R | `swapLR` |
| 2 | DJ Booth · Sides | `Room 2 Booth Sides` L/R | `swapLR` |
| 2 | iPhone · Front | `Room 2 - iPhone Front` | stereo |

**Ambience:** Room 1 uses `Corsica - Room 1 Booth (Looped).wav`, with −9 dB trim to match Room 2's level. Room 2 uses `Corsica - Room 2 Back (Looped, Best, Smaller).wav`.

**Photos:** converted from WebP to JPEG, because JUCE can't decode WebP. The originals are in `assets-src/`, which also holds the two noisy "Impulse 0dB / -5dB" Room 1 captures that were left out. Their noise floor is around −20 dB, against around −115 dB for the sweeps.

## Plugin identity

| | |
|---|---|
| Name | Spaces |
| Company | Gilly |
| Bundle ID | `com.gilly.spaces` |
| Manufacturer code | `Gily` |
| Plugin code | `Spcs` |

These are placeholders. Change them in `CMakeLists.txt` before the first public release. Afterwards, changing them makes hosts treat saved instances as a different plugin.

## Code map

| Path | What |
|---|---|
| `source/PluginProcessor.*` | Parameters, signal flow, IR building and loading threads, state |
| `source/dsp/ConvolutionLayer.h` | One true-stereo layer: convolution → drive → EQ → level |
| `source/dsp/WarmConvolution.*` | `juce::dsp::Convolution` copy with warm-start crossfades |
| `source/dsp/GainMatch.h`, `Saturator.h`, `LayerEQ.h`, `AmbiencePlayer.h` | The DSP blocks |
| `source/dsp/Mailbox.h` | Lock-free hand-off of loaded data to the audio thread |
| `source/SpacePack.*` | Manifest parsing and pack discovery |
| `source/AssetCache.*` | Shared decoded audio + loop resampler |
| `source/PresetManager.*` | Factory (manifest) and user presets |
| `source/PluginEditor.*`, `source/ui/*` | The UI |
| `tests/SpacesTests.cpp` | Offline test runner, WAV dump and UI screenshots |
