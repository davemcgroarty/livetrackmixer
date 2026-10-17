# DJ Mixer

A desktop app that analyses music into beats, bars, phrases and sections, then
lets you arrange mixes and transitions on a visual timeline. The design is in
[docs/design-spec.md](docs/design-spec.md).

The repo has two parts that share only files on disk:

- **`analyzer/`**: `djanalyze`, a Python command-line tool that turns one audio
  file into one analysis JSON file and one `.wfm` waveform file.
- **`app/`**: the C++20 / JUCE 8 desktop app, which runs `djanalyze` as a
  subprocess and reads its output.

`schemas/analysis.v1.schema.json` is the contract between them. Both test suites
validate against it.

## Status

| Milestone | State |
| --- | --- |
| M0 · Skeleton: CMake + JUCE window, installable analyzer, CI | Done |
| M1 · Analyzer CLI: schema-valid JSON and `.wfm`, cached by hash | Done (see notes below) |
| M2–M6 | Not started |

On the synthetic test clips, BPM is within ±0.02 and downbeats are within 13 ms
of the reference. The M1 targets are ±0.5 BPM and 30 ms.

## Analyzer

```sh
python -m venv .venv && . .venv/bin/activate
pip install -e "analyzer[dev]"
djanalyze analyze track.flac --out ~/.cache/djmixer/analysis
python -m pytest analyzer
```

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

CPM.cmake fetches JUCE, nlohmann/json, Catch2 and json-schema-validator at
configure time. Set `CPM_SOURCE_CACHE=~/.cache/CPM` to reuse the downloads.
Configure with `-DDJMIX_BUILD_APP=OFF` to build only the tests, without JUCE.

On Linux, JUCE needs `libasound2-dev libfreetype-dev libfontconfig1-dev
libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxext-dev libgl-dev`.

To check real analyzer output from the C++ side, point both test suites at the
same directory:

```sh
DJMIX_ANALYZER_OUTPUT=/tmp/analysis python -m pytest analyzer
DJMIX_ANALYZER_OUTPUT=/tmp/analysis ctest --test-dir build
```

## Licence

AGPL-3.0, as required by JUCE's free tier and Essentia. See [LICENSE](LICENSE).
