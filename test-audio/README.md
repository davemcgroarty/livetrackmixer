# Test audio

Synthetic clips with known tempo, downbeats, sections and key, for the analyzer
tests. `clips.json` describes each clip; `generate.py` renders them:

```sh
python test-audio/generate.py            # writes test-audio/clips/ (git-ignored)
```

The clips are built from synthesised drums, bass and chords with a fixed random
seed, so they are reproducible and contain no recorded material. They are
released under CC0 1.0. There are four, one per supported format (WAV, FLAC,
AIFF, MP3), at 100–128 BPM and 16–32 bars, each with a drums-only or
chords-only section to exercise section, key and descriptor detection.
