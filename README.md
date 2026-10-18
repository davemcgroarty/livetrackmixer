# DJ Mixer

A desktop app that analyses music into beats, bars, phrases and sections, then
lets you arrange mixes and transitions on a visual timeline. The design is in
[docs/design-spec.md](docs/design-spec.md).

The repo has two parts that share only files on disk:

- **`analyzer/`**: `djanalyze`, a Python command-line tool that turns one audio
  file into one analysis JSON file and one `.wfm` waveform file.
- **`app/`**: the C++20 / JUCE 9 desktop app, which runs `djanalyze` as a
  subprocess and reads its output.

`schemas/analysis.v1.schema.json` is the contract between them. Both test suites
validate against it.

## Status

| Milestone | State |
| --- | --- |
| M0 · Skeleton: CMake + JUCE window, installable analyzer, CI | Done |
| M1 · Analyzer CLI: schema-valid JSON and `.wfm`, cached by hash | Done (see notes below) |
| M2a · Library: import, background analysis, track table | Done |
| M2b · Track view: waveform, beat/bar/phrase grid, sections, content cells | Done |
| M2c · Audition on Cue: sections, phrases and tracks; Master/Cue routing | Done |
| M3 · Grid and section editing, saved as overrides, with undo | Done |
| M4 · Arrangement playback | Next |
| M5–M6 | Not started |

On the synthetic test clips, BPM is within ±0.02 and downbeats are within 13 ms
of the reference. The M1 targets are ±0.5 BPM and 30 ms.

## Analyzer

```sh
python -m venv .venv && . .venv/bin/activate
pip install -e "analyzer[dev]"
djanalyze analyze track.flac --out ~/.cache/djmixer/analysis
python -m pytest analyzer
```

It reads WAV, FLAC, AIFF, MP3 and OGG directly, and M4A (AAC or ALAC) and
other formats when `ffmpeg` is installed.

`djanalyze` prints one JSON progress line per stage on stdout, for example
`{"stage": "beats", "progress": 0.4}`, then `{"stage": "done", ...}` or
`{"stage": "error", "message": ...}`. Output files are named after the
SHA-256 of the audio file (`sha256-<hex>.json` and `sha256-<hex>.wfm`). A
second run on the same file returns the cached result unless the analyzer
version has changed or `--force` is given.

The spec's models (allin1, Beat This!, Essentia) are optional extras:
`pip install -e "analyzer[models]"`. They need PyTorch, and allin1 also needs
NATTEN and madmom, so they do not install everywhere. Without them `djanalyze`
uses built-in librosa estimators, and that built-in engine is what the tests
check. `--engine builtin` forces it even when the models are installed.

See [analyzer/README.md](analyzer/README.md) for how each stage works.

## App

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

Run the app from `build/app/DJMixer_artefacts/` (on macOS, `DJ Mixer.app`).

### Using the app

1. Open **Settings...** and point the app at `djanalyze`: in the virtual
   environment from the Analyzer section that is `.venv\Scripts\djanalyze.exe`
   on Windows and `.venv/bin/djanalyze` elsewhere. **Test** checks it runs.
   If `djanalyze` is on `PATH` when the app starts, it is found automatically.
2. Drag audio files or folders onto the Library, or use **Add...**. Folders are
   searched recursively. Files named on the command line are imported too.
3. Each new track is queued and analysed in the background, two at a time,
   with a progress bar in the Status column. Analysis results are cached by
   file hash, so a track analysed before (or a copy of it) is ready at once.

The table sorts by any column. Filter by a BPM range, or tick **Compatible
keys** to show only tracks that mix well with the selected one (same key, ±1 on
the Camelot wheel, or the relative major/minor). Right-click a track to change
its colour, analyse it again, show the file or remove it from the library
(the file itself is never touched). Tracks with an uncertain beat grid show
**Check beats**; hover over a status for details.

Select a track to see it in the **Track view**: the waveform over its beat
grid, with bar lines, heavier phrase lines (every 8 bars) and section
boundaries, and a strip of sections above. Each bar is a cell in the track's
colour: the block rising from the bottom is its bass, the block hanging from
the top its high end, and the waveform is brighter where the rhythm is
stronger. Hover over a bar, or a section in the strip, to see its bass, high,
rhythm and melody scores.

Zoom with the mouse wheel or a pinch (around the pointer), from the whole
track down to two beats; scroll with shift-wheel, a sideways swipe, the scroll
bar or by dragging. **+** and **-** zoom and **Home** shows the whole track.

**Auditioning** plays on the Cue output only, never Master:

- Click a section in the strip to hear that section, or click in the waveform
  to hear the 8-bar phrase under the pointer.
- Double-click a track in the Library to hear it from the start.
- Click the same section or phrase again, click **Stop cue**, or press **Esc**
  to stop.
- The **Cue** button in the Track view header sets what a cue does when it
  reaches the end of its section or phrase. Click it to step through:
  - **Once**: stop.
  - **Loop**: go round again.
  - **Continue**: play on through the rest of the track.

  Changing it while a cue plays changes that cue, and the choice is
  remembered.

A cue starts 5 ms early, so its fade-in is over by the first beat and the
opening hit plays at full level. Each loop pass crossfades over the last and
first 5 ms, so the loop is exactly as long as the section and keeps time.

The selected track is decoded in the background as soon as you pick it, so
clicks play at once. A playhead shows where you are.

**Correcting the analysis.** When the beat grid or sections are wrong, fix
them in the Track view. Right-click for the edit menu.

The grid:
- **Downbeat:** make the beat under the pointer the downbeat, with the menu or
  **D**.
- **Nudge:** move the whole grid 5 ms with **Alt+Left/Right**, or 1 ms with
  **Shift** as well. **Alt-drag** in the waveform moves the grid with the
  pointer, to lay a beat line on a hit.
- **Tempo:** halve or double the BPM, type a BPM, or tap **T** in time (at
  least four taps).
- **Reset grid** returns to the analysis.

Sections:
- **Split:** start a section at the bar you clicked.
- **Merge:** join a section with the next one.
- **Move:** drag a boundary in the section strip. It snaps to bars.
- **Reset sections** returns to the analysis.

Phrases follow the new sections. Cmd/Ctrl+Z undoes and Cmd/Ctrl+Shift+Z (or
Ctrl+Y) redoes.

Edits are saved in the library as overrides on top of the analysis, so they
survive a restart and analysing the track again. Section starts are stored as
times, so after a grid edit they stay where the music changes and snap to the
nearest bar of the new grid. An edited track shows **(edited)** next to its
BPM. The Library shows the edited BPM and filters by it. **Check beats** goes
once the grid has been corrected.

**Audio output** is set in **Settings...**: the device, which outputs carry
Master and Cue, and the Cue volume. **Test cue** beeps on the Cue outputs.
- With four or more outputs, Master goes to 1-2 and Cue to 3-4 by default.
- On a stereo device, or with **Split** chosen, Cue plays in mono on the left
  and Master in mono on the right, for practising on one pair of headphones.

Master is silent until the Arrangement arrives in M4.

When the app finds a newer (or different) `djanalyze` than the one that
analysed a track, it analyses the track again in the background; the old
analysis stays on screen until the new one is ready.

Titles and artists come from file names (`Artist - Title.ext`, with any leading
track number dropped); reading tags is a later step. The library is a SQLite
file in the app's data folder (**Settings...** shows where), next to the
analysis cache.

CPM.cmake fetches JUCE, SQLiteCpp, nlohmann/json, Catch2 and json-schema-validator at
configure time. Set `CPM_SOURCE_CACHE=~/.cache/CPM` to reuse the downloads.
Configure with `-DDJMIX_BUILD_APP=OFF` to build only the tests, without JUCE.

On Linux, JUCE needs `libasound2-dev libfreetype-dev libfontconfig1-dev
libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxext-dev libxi-dev
libxcomposite-dev libxrender-dev libgl-dev`.

To check real analyzer output from the C++ side, point both test suites at the
same directory:

```sh
DJMIX_ANALYZER_OUTPUT=/tmp/analysis python -m pytest analyzer
DJMIX_ANALYZER_OUTPUT=/tmp/analysis ctest --test-dir build
```

The Track view tests draw a six-minute track into an image at four zoom levels
and print the time per frame (a few milliseconds in a Release build). Set
`DJMIX_SNAPSHOT_DIR` to a folder to also save the images there.

The Cue player is tested by calling its audio callback directly, with no
device. The tests check routing, fades (no clicks on start, stop, switch or the
end of a range), resampling, and that decoded audio is only freed off the audio
thread. They also replace `operator new` to check the callback never allocates.

The analyzer runner is tested against `fake_djanalyze`, a small stand-in built
with the tests. Set `DJMIX_DJANALYZE` to a real `djanalyze` to also run it
end to end.

## Licence

AGPL-3.0, as required by JUCE's free tier and Essentia. See [LICENSE](LICENSE).
