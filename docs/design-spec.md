# DJ Mixing App — Design Specification

Exported from the design doc on 17 Oct 2026. The doc is the source of truth; this copy is for reference in the repo.

## Overview

A desktop app that analyses music files into beats, bars, phrases and sections, then lets you arrange mixes and transitions on a visual timeline. Version 1 covers analysis and arrangement. There are no virtual decks. A mix is layers of sound on one timeline. In a later version that timeline becomes the live surface: the playhead runs while you line up the next transition ahead of it. The v1 design must not block that.

**Platform:** desktop (macOS and Windows first), C++20 with JUCE, plus a Python analysis sidecar. Personal and open-source use, so GPL/AGPL libraries are acceptable.

**v1 goals**

- Import audio files (MP3, WAV, FLAC, AIFF) and analyse each one once, caching the results.
- Show each track as a waveform with a beat grid, bar lines, phrase lines and section boundaries, all unlabelled.
- Correct the analysis by hand: move the downbeat, nudge the grid, split or merge sections.
- Drag whole tracks, sections or phrases onto a multi-lane timeline that snaps to bars and phrases.
- Build transitions visually: overlapping regions with crossfade curves, 3-band EQ swaps and filter sweeps drawn as automation.
- Play the arrangement back in sync at a project tempo, with key-locked time-stretching.
- Save and reopen projects; export the mix to WAV.

**Non-goals for v1**

- Live triggering of layers and MIDI controller mapping (planned for v2). There will be no deck, platter or jog-wheel emulation in any version.
- Stem separation (planned for v1.1).
- Streaming-service integration, recording, or effects beyond EQ, filter and gain.

## Tech stack

The app is C++ and JUCE end to end, apart from analysis. Analysis runs in Python because the best beat and structure models only exist there. Verify each library's current version and licence when setting up the repo.

| Layer | Library | Role | Licence |
| --- | --- | --- | --- |
| App framework | JUCE 8 | Audio I/O, UI, file formats, plugin-style DSP | AGPLv3 (free tier) |
| Build | CMake + CPM.cmake | Builds and fetches dependencies | MIT |
| Time-stretch | Rubber Band Library (R3 engine) | Tempo change with key lock | GPLv2+ |
| MP3/FLAC decode | JUCE readers (+ minimp3 if needed) | Decoding audio files | AGPL / CC0 |
| Library database | SQLite | Track library and analysis index | Public domain |
| JSON | nlohmann/json | Reading analysis files and projects | MIT |
| C++ tests | Catch2 | Unit tests | BSL-1.0 |
| Structure analysis | allin1 | Beats, downbeats, BPM, section boundaries | MIT |
| Beat cross-check | Beat This! | Second beat/downbeat estimate | MIT |
| Key and loudness | Essentia | Musical key, LUFS, energy curve | AGPLv3 |
| General audio | librosa, soundfile | Resampling, utilities | ISC / BSD |
| Stems (v1.1) | Demucs (htdemucs) | Drums, bass, vocals, other | MIT |
| Python tests | pytest | Analyzer tests | MIT |

Mixxx, the open-source DJ app, is the main code reference for beat grids, sync and waveform rendering. Borrow its grid and waveform ideas, not its deck model.

## Architecture

The Python analyzer and the C++ app share only files on disk: the analysis JSON and the waveform file. The app can be built and tested with fixture JSON before the analyzer is finished.

*(Architecture diagram: see the design doc.)*

Only the audio engine runs on the audio thread. Everything else talks to it through immutable snapshots and pre-rendered buffers.

## Analysis pipeline

The analyzer is a Python command-line tool, `djanalyze`, that the app runs as a subprocess. It takes one audio file and writes one JSON file. It never talks to the audio engine directly.

**Command:** `djanalyze analyze <audio-file> --out <cache-dir> [--stems]`. It prints JSON progress lines on stdout (`{"stage":"beats","progress":0.4}`) so the app can show a progress bar.

**Steps**

1. Hash the file (SHA-256 of the audio bytes). If `<cache-dir>/<hash>.json` exists with the current `analyzerVersion`, stop.
2. Decode to 44.1 kHz stereo float.
3. Run allin1 for beats, downbeats, BPM and segment boundaries. Its segment labels are discarded.
4. Run Beat This! and compare. If the downbeats disagree on more than 10% of bars, set `confidence.beats` low so the UI flags the track for review.
5. Derive bars from downbeats, numbering from 1 at the first downbeat.
6. Derive phrases: groups of 8 bars (or 16 for long sections), aligned to section starts. Each section boundary always starts a new phrase.
7. Run Essentia for the overall key (with Camelot code) and a key per section, each with a confidence score, plus integrated loudness and a per-bar energy curve. A section whose key confidence or melody score is low (for example a drums-only intro) is marked as having no reliable key.
8. Compute waveform overviews: peak/RMS per 256 samples, for drawing the waveform. Save as a binary `.wfm` file beside the JSON.
9. Write the JSON atomically (temp file, then rename).

**Content descriptors**

For every bar, the analyzer scores four qualities from 0 to 1, then averages them per section. They show what a section contains, not just how loud it is.

| Descriptor | What it measures | Method |
| --- | --- | --- |
| Rhythm | How strong and steady the beat is | Percussive part (librosa HPSS) onset strength, weighted by how regularly onsets land on the beat grid |
| Bass | How much low end there is | Energy below \~150 Hz |
| High end | How much top end (hats, air, cymbals) there is | Energy above \~5 kHz |
| Melody | How much pitched, tonal content there is | Harmonic part (HPSS) energy from 200 Hz to 4 kHz, weighted by chroma clarity and Essentia pitch salience |

- Scores use fixed reference ranges, not per-track normalisation, so a 0.8 bass section means the same across the whole library. Raw values (dB, salience) are stored as well.
- In v1.1, when stems exist, recompute melody from the vocals and other stems, and rhythm from the drums stem, for cleaner results.
- Calibrate the reference ranges against the test clips and a sample of real tracks during M1.

**Analysis file (schema v1)**

All positions are in seconds from the file start. Beat indices are 0-based; bar and phrase numbers are 1-based for display.

```json
{
  "schemaVersion": 1,
  "analyzerVersion": "0.1.0",
  "file": { "hash": "sha256:…", "durationSec": 372.4, "sampleRate": 44100 },
  "tempo": { "bpm": 124.0, "variable": false },
  "key": { "name": "A minor", "camelot": "8A", "confidence": 0.82 },
  "loudness": { "integratedLufs": -8.1 },
  "beats": [0.512, 0.996, 1.480],
  "downbeats": [0.512, 2.448],
  "bars": [ { "n": 1, "start": 0.512, "firstBeat": 0 } ],
  "phrases": [ { "n": 1, "startBar": 1, "lengthBars": 16, "section": 0 } ],
  "sections": [ { "start": 0.512, "end": 31.5, "startBar": 1, "lengthBars": 16, "key": { "camelot": "8A", "confidence": 0.77 } } ],
  "energyPerBar": [0.21, 0.24],
  "descriptors": {
    "perBar": { "rhythm": [0.12, 0.15], "bass": [0.05, 0.08], "high": [0.40, 0.42], "melody": [0.66, 0.70] },
    "perSection": [ { "section": 0, "rhythm": 0.14, "bass": 0.07, "high": 0.41, "melody": 0.68 } ]
  },
  "confidence": { "beats": 0.93, "sections": 0.71 },
  "waveformFile": "sha256-….wfm",
  "stems": null
}
```

Sections are boundaries only, with no labels. User corrections are stored separately (see Data model), never written into this file.

## Data model

Everything on the timeline is measured in **beats at project tempo**, not seconds. A clip maps a beat range of its source track onto a beat range of the timeline. This keeps every edit musical and makes later tempo changes free.

| Entity | Key fields | Stored in |
| --- | --- | --- |
| Track | id, file path, file hash, title, artist, colour, analysis status | SQLite library |
| Analysis | the JSON above, keyed by file hash | Cache directory |
| Grid override | track id, beat offset (s), BPM override, downbeat shift (beats) | SQLite library |
| Section override | track id, edited section boundaries (startBar, lengthBars) | SQLite library |
| Project | id, name, tempo (BPM), key (optional), layers, clips, transitions | `.djmix` JSON file |
| Layer | id, name, gain, mute, solo, order, live EQ and filter settings | Project |
| Clip | id, trackId, layerId, sourceStartBeat, timelineStartBeat, lengthBeats, gain, transposeSemitones, label (colour comes from its track) | Project |
| Transition | id, clipIds (two or more), startBeat, lengthBeats, type, automation | Project |
| Automation lane | target (clip gain, EQ low/mid/high, filter cutoff), breakpoints (beat, value, curve) | Inside a transition or clip |

**Rules**

- A track's effective grid = analysis + grid override. Section and phrase views always use the effective grid.
- Clips snap to beats by default, and to bars or phrases when snap mode says so. Holding a modifier key disables snapping.
- Any number of layers can sound at once; there is no fixed A/B pairing. A transition is a time region where clips on two or more layers overlap, and its preset writes automation on each of them. Moving a clip moves or shrinks the transition.
- Transition types are presets that create automation: **Crossfade** (gain curves), **Bass swap** (low-EQ kill on one, restore on the other at a chosen bar), **Filter out/in** (high-pass on outgoing, low-pass opening on incoming), **Cut** (instant switch on a downbeat).
- Projects store the file hash with each track, so a moved file can be relinked by hash.
- Every edit goes through an undoable command (JUCE `UndoManager`).

## Audio engine

The engine plays the arrangement as a graph: one voice per clip, mixed per layer, then to the master bus. The audio thread never allocates memory, locks a mutex or touches disk.

**Playback model**

- A transport holds the playhead in beats. Each audio block converts its beat range to source positions for every active clip.
- Each clip voice reads pre-stretched audio from a **render cache**. When project tempo changes, a background thread re-renders affected clips with Rubber Band (offline, R3 engine, high quality), applying both the tempo ratio and any transpose, and swaps them in when ready.
- Until a re-render finishes, the voice plays the old render, so playback never stops. Edits made while playing take effect at once: the next snapshot swaps in mid-playback, so you can rework anything ahead of the playhead without stopping.
- This design is simple and glitch-free for v1. For v2 live play, add a real-time Rubber Band path for layers triggered live, alongside it.

**Per-clip DSP chain:** gain → 3-band EQ (low shelf \~200 Hz, peak \~1 kHz, high shelf \~4 kHz, each with full kill) → state-variable filter (high-pass or low-pass sweep) → layer live EQ and filter (see Live controls) → layer gain.

**Thread layout**

| Thread | Does | Talks to audio via |
| --- | --- | --- |
| Audio (JUCE device callback) | Mixes voices, applies DSP and automation | — |
| Message (UI) | Edits project, draws waveforms | Lock-free FIFO of engine commands |
| Render worker | Decodes, time-stretches, fills render cache | Atomic swap of immutable buffers |
| Analyzer runner | Spawns `djanalyze`, parses progress | Posts results to message thread |

**Project state hand-off:** the UI builds an immutable `PlaybackSnapshot` (clips, automation, render buffers) after each edit and publishes it with an atomic pointer swap. Old snapshots are freed on the message thread, never the audio thread.

**Export:** run the same graph offline, faster than real time, and write 24-bit WAV.

## UI and interaction

The main window has three areas: the **Library** on the left, the **Track view** on top, and the **Arrangement** below. Sections have no labels or type colours. Each boundary is a line, with alternating light shading between sections.

**Library**

- Table of tracks: title, artist, BPM, key (Camelot), duration, analysis status.
- Drag files or folders in to import; analysis starts in the background with a progress bar per row. Each new track is given a distinct colour from a fixed palette of about 12 hues, and you can change it.
- Tracks with low beat confidence show a warning badge.
- Sort and filter by BPM range and harmonically compatible keys (same, ±1, relative major/minor).

**Track view** (the selected track)

- Full-width waveform with a section strip above it showing where each section starts and ends. Every bar of the waveform is a content cell that shows bass, high end and rhythm at once, so bar-to-bar changes are easy to compare. Bass is a full-width block rising from the bottom of the cell, its height set by the bass score. High end is a block hanging from the top, its height set by the treble score. Melody is not drawn in the cell, so it never competes with the waveform; its score is still computed and shown on hover. Rhythm is the contrast of the waveform itself: faint for a weak beat, solid for a strong one. Bass and treble blocks each fill at most half the cell, so they never overlap, and both are translucent so the waveform stays readable. Each track has its own colour, used for its bass and treble blocks and a faint cell background; bass and treble are told apart by position, not colour. The waveform is drawn in one neutral colour. Hovering a bar or section shows the four values.
- Beat ticks, bar lines and heavier phrase lines overlaid, with no numbers; zoom from whole track to single beats.
- Grid editing: drag to set the downbeat, nudge left/right, tap tempo, halve/double BPM.
- Section editing: drag boundaries (they snap to bars), add, split or merge.
- Click a section or phrase to audition it; drag it into the Arrangement to create a clip.

**Arrangement**

- Horizontal timeline in bars at project tempo, with layers stacked vertically. A new project starts with one layer; add as many as the mix needs, for example a separate layer for a vocal or a percussion loop.
- Clips show their waveform, section boundaries and (toggleable) content cells, and resize from either edge with bar snapping.
- Snap modes: off, beat, bar, phrase (8 bars). Phrase lines are drawn heavier than bar lines.
- Where clips on different layers overlap, a transition region appears between them. Choose a preset from its menu, then edit the automation curves directly.
- Key warnings compare the keys of whatever is sounding together, section by section, taking each clip's transpose into account. They check every overlap between layers and every hard cut from one clip straight into the next. Same key, ±1 on the Camelot wheel, or relative major/minor is fine; ±2 is a caution (amber); anything further is a clash (red). Sections with no reliable key never trigger a warning, so a drum intro under a melodic track is safe. A warning shows on the timeline at the bars where it applies, with the two keys, and offers a fix: transpose the incoming clip by the smallest number of semitones (at most ±2) that makes it compatible. A bass-clash indicator appears where two or more overlapping layers have high bass scores at once and no bass-swap automation.
- Transport: play/pause, loop region, follow playhead, project tempo field. In follow mode the playhead sits about a third of the way across, so most of the screen shows what is coming next, and the next phrase boundary is highlighted.

**Keyboard:** Space play/pause, S split clip at playhead, Cmd/Ctrl+Z undo, 1–4 snap mode, +/− zoom.

## Live controls

Every layer has a compact control strip in its header, for hands-on changes while playing. The strip acts on whatever that layer is playing, on top of any automation in the arrangement.

**Layer strip**

- **3-band EQ:** low, mid and high knobs, each from full kill to +6 dB. Double-click resets to 0 dB.
- **Filter:** one knob, low-pass to the left, high-pass to the right, off in the middle.
- **Gain** fader, **mute**, and the **cue** (headphone) button.
- **Shortcut buttons:**
  - **Bass cut:** kills the low band, the most common move in a transition.
  - **Mid cut** and **high cut:** kill those bands.
  - **HPF:** jumps the filter to a thin high-pass (about 300 Hz).
  - **LPF:** jumps the filter to a muffled low-pass (about 800 Hz).
  - **Reset:** returns every control on the layer to neutral.
- Shortcuts act instantly by default. A **quantise** option makes them land on the next beat or bar, so a bass cut always hits the downbeat. Each shortcut is either a toggle or hold-to-engage, set in settings.

**Live moves and automation**

- When you move a live control, it overrides that layer's automation for that parameter until you reset it or release a shortcut.
- **Record moves** (off by default) writes live control changes into the arrangement as automation, as one undoable step when recording stops.

**Detail panel**

Selecting a layer or clip opens a panel at the side with fuller controls:

- EQ with adjustable frequency and gain per band, and Q for the mid band.
- Filter type, cutoff and resonance, and a sweep speed for the HPF and LPF shortcuts.
- Clip gain and transpose, with the clip's BPM, key and the key at the current playhead section.
- The content values (bass, high end, rhythm, melody) for the bar under the playhead.
- The clip's transitions and their automation, for editing.

## Audio outputs and cueing

The engine has two output buses: **Master**, which the audience hears, and **Cue**, for the DJ's headphones. Nothing auditioned for preparation ever reaches Master.

**Routing**

- Default: one audio interface with at least four outputs. Master goes to outputs 1–2 and Cue to outputs 3–4; both are selectable in settings. Most DJ controllers and interfaces present themselves this way.
- Fallback: **split cue** on a single stereo output, with Cue summed to mono on the left channel and Master in mono on the right, for practising on one pair of headphones.
- Separate devices for Master and Cue (for example, built-in audio plus a USB interface) are a later feature, because their clocks drift and need resampling.
- Output routing is an app setting, saved outside the project.

**What goes to Cue**

- Every audition: clicking a section, phrase or library track plays it on Cue only.
- **Cue on layer:** each layer has a headphone button. When on, that layer is sent to Cue as well as Master, so you can isolate it in your headphones.
- **Look-ahead cue:** click anywhere ahead of the playhead to hear those bars on Cue now, beat-locked to Master, so you can check an upcoming transition before the audience hears it. Cue follows the Master bar position, so it stays in phase; releasing returns Cue to silence or to the cued layers.

**Cue controls:** cue volume and a cue/Master blend knob, so you can hear the cue against the live mix in your headphones.

**Engine:** the cue path is a second read position into the same render cache, mixed into its own bus with no effect on Master timing. Vamp, edits and look-ahead cue all work at the same time.

## Vamp

Vamp holds the mix in place: it repeats the current window of 1, 2, 4, 8 or 16 bars on every active layer until you release it. It is a playback action in v1 and works the same way when live triggering arrives in v2.

**Behaviour**

- **Window:** the N bars containing the playhead, aligned to multiples of N from the start of the current section. A 4-bar vamp therefore always lines up with phrases.
- **Layers:** every layer with a clip sounding in the window repeats together, including its transition automation. Muted layers stay muted, and you can mute or unmute layers while vamping.
- **Engage:** takes effect immediately; the playhead jumps back to the window start each time it reaches the window end.
- **Change length while vamping:** the new length applies at the next window boundary, keeping the bar alignment.
- **Release:** quantised to the end of the current repetition by default, then playback continues into the next bar. An "instant" option releases on the next downbeat.
- **Rest of the arrangement:** everything after the window waits, so the music ahead plays later by the time spent vamping. Arrangement positions do not change.
- **Loop seam:** a 5 ms crossfade at the jump point avoids clicks. Clips that start inside the window restart cleanly each repetition.
- **Commit (optional):** after a vamp, "Commit vamp" inserts the repeated bars into the arrangement across all layers, shifting later clips right. This is one undoable command.

**UI:** a Vamp button and length selector (1/2/4/8/16) in the transport. While active, the window is shaded on the timeline with a repeat counter, and the look-ahead area shows a marker where playback will resume.

**Keyboard:** V engages or releases vamp; \[ and \] halve or double the length.

**Engine:** vamp is a transport state (window start, length in beats, release mode), sent to the audio thread through the command FIFO. Because clips play from pre-rendered buffers, a jump is only a read-position change; no re-render is needed. Vamp state is not saved in the project; only the last-used length is remembered.

## Repo structure

One monorepo, two buildable parts: the C++ app and the Python analyzer.

```
dj-mixer/
├── CMakeLists.txt
├── cmake/                 # CPM.cmake, toolchain helpers
├── app/
│   ├── Source/
│   │   ├── Main.cpp
│   │   ├── model/         # Track, Clip, Transition, Project, UndoableCommands
│   │   ├── library/       # SQLite library, import, relink-by-hash
│   │   ├── analysis/      # AnalysisLoader, AnalyzerRunner (subprocess), effective grid
│   │   ├── engine/        # Transport, ClipVoice, RenderCache, DSP, PlaybackSnapshot
│   │   ├── ui/            # LibraryPanel, TrackView, ArrangementView, WaveformRenderer
│   │   └── io/            # Project load/save, WAV export
│   └── Tests/             # Catch2 tests
├── analyzer/
│   ├── pyproject.toml
│   ├── djanalyze/         # cli.py, beats.py, structure.py, key.py, waveform.py, schema.py
│   └── tests/             # pytest, golden JSON fixtures
├── schemas/
│   ├── analysis.v1.schema.json
│   └── project.v1.schema.json
├── test-audio/            # short CC0 clips with known BPM and structure
└── docs/
    └── design-spec.md     # this document
```

The JSON Schema files are the contract between the two halves. Both sides validate against them in their tests.

## Milestones

Build in this order. Each milestone ends with something you can run and judge by ear or eye.

1. **M0 · Skeleton.** CMake project builds a JUCE window on macOS and Windows; the analyzer installs with `pip install -e analyzer`; CI runs both test suites.
   - Done when: CI is green and the app opens an empty window.
2. **M1 · Analyzer CLI.** `djanalyze` produces schema-valid JSON and a `.wfm` file, with caching by hash.
   - Done when: on the test clips, BPM is within ±0.5 and downbeats are within 30 ms of the reference.
3. **M2 · Library and Track view.** Import files, run analysis in the background, show the coloured waveform with grid, bars, phrases, sections and per-bar content cells.
   - Done when: a 6-minute track scrolls and zooms at 60 fps, and clicking a section plays it.
4. **M3 · Grid and section editing.** Downbeat drag, nudge, BPM override, section split/merge, saved as overrides.
   - Done when: edits survive an app restart and re-analysis leaves them intact.
5. **M4 · Arrangement playback.** Layers, clips, snapping, transport, Master and Cue outputs, render cache with Rubber Band, project save/load.
   - Done when: two 124 BPM tracks placed at a 128 BPM project tempo stay beat-aligned for 5 minutes with no audible drift or clicks.
6. **M5 · Transitions and vamp.** Overlap regions, the four presets, editable automation, EQ and filter DSP, key-clash indicator, vamp, and layer live controls with the detail panel.
   - Done when: a bass swap on a phrase boundary sounds clean, and every automation edit is undoable. A 4-bar vamp over a transition loops all its layers with no audible seam, and release lands on the next bar.
7. **M6 · Export and polish.** Offline WAV export, keyboard shortcuts, relink by hash, error handling for missing files.
   - Done when: the exported WAV matches real-time playback sample for sample.

## Risks and open questions

| Risk | Impact | Mitigation |
| --- | --- | --- |
| Section boundaries can land in the wrong place | Bad phrase anchors | Easy manual editing (M3); confidence score shown in UI |
| Downbeat off by one beat | Every phrase misaligned | Beat This! cross-check; one-key "shift downbeat" |
| Variable-tempo tracks (live drums, old records) | Grid drifts | Flag `tempo.variable`; v1 supports constant tempo only |
| Python dependencies (PyTorch, Essentia) are heavy to bundle | Hard to distribute | v1 assumes a local Python environment; bundling is a later task |
| Rubber Band re-render is slow on long tracks | Lag after tempo change | Render only visible/nearby clips first; keep old render playing |
| Time-stretch artefacts beyond ±8% tempo | Poor sound | Warn when a clip is stretched more than 8% |

**Open questions**

- Linux support in v1, or later?
- Should phrase length default to 8 or 16 bars, or follow the genre?
- Is WAV export enough, or is MP3 export wanted?

## Notes for Claude Code

- Start with M0 and M1 only. Do not start the engine until the analysis JSON is stable.
- Write `schemas/analysis.v1.schema.json` first, then the analyzer, then the C++ loader. Both sides validate against the schema in tests.
- Keep audio-thread code free of allocation, locks and logging. Add a debug assertion helper to catch violations.
- Keep model code (`app/Source/model`) free of JUCE UI classes, so it is unit-testable without a window.
- Prefer small pull requests, one per milestone sub-task, each with tests.
- Test audio must be short, CC0-licensed clips with documented BPM and structure.
