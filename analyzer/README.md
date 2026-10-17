# djanalyze

```
djanalyze analyze <audio-file> --out <cache-dir> [--stems] [--force] [--engine auto|builtin]
```

Reads MP3, WAV, FLAC or AIFF (anything libsndfile decodes, with librosa's
audioread as a fallback) and writes `<cache-dir>/sha256-<hex>.json`
(schema: `schemas/analysis.v1.schema.json`) and `sha256-<hex>.wfm`
(format: `docs/wfm-format.md`). The `.wfm` file is written first and the JSON
last, both atomically, so a JSON file always has its waveform beside it.

Exit codes: 0 success or cached, 1 analysis failed, 2 no such input file.
`--stems` is accepted and ignored until v1.1.

## Stages

| Stage | With the models installed | Built-in fallback |
| --- | --- | --- |
| Beats and downbeats | allin1 | librosa beat tracker; downbeat phase from chord changes and low-frequency onsets |
| Downbeat cross-check | Beat This!: over 10% of bars disagreeing sets `confidence.beats` to 0.3 | none; `confidence.beats` is capped at 0.7 |
| Sections | allin1 segments snapped to bars, labels discarded | Foote novelty on a bar-by-bar self-similarity matrix (MFCC, chroma, band levels) |
| Key, overall and per section | Essentia `KeyExtractor` (EDMA profile) | Log-compressed harmonic chroma against Albrecht–Shanahan key profiles |
| Loudness | Essentia `LoudnessEBUR128` | Built-in ITU-R BS.1770-4 |
| Descriptors | built-in (see below) | built-in |

Common to both engines:

- **Constant-tempo grid.** Detected beats are fitted with a straight line, and
  the grid's phase is refined against the onset envelope. When the beats sit
  within 8% of a beat of the line (median), the grid becomes the beat list,
  from the first beat after 0 s to the end of the file. Otherwise
  `tempo.variable` is true and the detected beats are kept.
- **Bars** start at each downbeat, numbered from 1. 4/4 is assumed.
- **Phrases** are 8 bars, or 16 in sections of 32 bars or more that divide
  evenly. Every section boundary starts a phrase. The final phrase of a section
  can be shorter.
- **Sections without a reliable key** (key confidence under 0.5 or melody
  score under 0.15) get `"key": null`.

## Content descriptors

Each bar gets four scores from 0 to 1. `perSection` holds their mean over each
section, and `perBar.raw` holds the measurements behind them.

| Score | Measurement | 0 at | 1 at |
| --- | --- | --- | --- |
| rhythm | percussive (HPSS) onset strength × how much of it lands within 30 ms of the 8th-note grid | 0 | 1.5 |
| bass | energy below 150 Hz | −45 dBFS | −5 dBFS |
| high | energy above 5 kHz | −60 dBFS | −20 dBFS |
| melody | harmonic (HPSS) energy from 200 Hz to 4 kHz × (chroma clarity + harmonic share) / 2 | −50 dBFS | −12 dBFS |
| energyPerBar | full-band energy | −45 dBFS | −3 dBFS |

These reference ranges are fixed (`REFERENCE` in `descriptors.py`), so scores
compare across tracks. They were set on the synthetic test clips, which master
to about −15 LUFS, with headroom for louder masters. They still need checking
against a sample of real tracks.

## Tests

`python -m pytest analyzer` renders the clips described in
`test-audio/clips.json` with `test-audio/generate.py`, analyses each once with
the built-in engine and checks:

- BPM within ±0.5 and downbeats within 30 ms (the M1 targets)
- section boundaries, phrases and keys against how each clip was built
- that descriptors rise and fall with the parts playing in each section
- the CLI contract: output files, progress lines, caching, errors, the schema

Set `DJMIX_ANALYZER_OUTPUT` to keep the analysis files for the C++ tests.
