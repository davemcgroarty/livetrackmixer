# Waveform overview file (.wfm), format version 1

`djanalyze` writes one `.wfm` file beside each analysis JSON, named in its
`waveformFile` field. It holds one peak and one RMS value per block of 256
samples of the mono mix (the mean of left and right), for drawing waveforms
without decoding the audio.

All numbers are little-endian.

| Offset | Size | Type      | Field             | Value                          |
| ------ | ---- | --------- | ----------------- | ------------------------------ |
| 0      | 4    | bytes     | magic             | `DJWF`                         |
| 4      | 2    | uint16    | format version    | 1                              |
| 6      | 2    | uint16    | channels          | 1 (mono mix)                   |
| 8      | 4    | uint32    | sample rate       | 44100                          |
| 12     | 4    | uint32    | samples per point | 256                            |
| 16     | 8    | uint64    | point count       | ceil(frames / samples per point) |
| 24     | 8    | uint64    | reserved          | 0                              |
| 32     | 8×n  | float32×2 | points            | peak, RMS for each point       |

- **Peak** is the largest absolute sample value in the block, 0 to 1 for
  unclipped audio.
- **RMS** is the root mean square of the block. The last block is usually
  short; its RMS counts only its real samples.
- Point `i` covers samples `i × 256` to `(i + 1) × 256 − 1`, so it starts at
  `i × 256 / sampleRate` seconds.

Readers must reject a file whose magic, version or channel count they do not
know, or whose size is not `32 + 8 × point count` bytes.

Implementations: `analyzer/djanalyze/waveform.py` (writer and reader) and
`app/Source/analysis/WaveformFile.cpp` (reader).
