"""Spectral features shared by the beat, descriptor, key and structure stages.

A full-resolution spectrogram of a whole track takes gigabytes, so the audio is
processed in chunks of CHUNK_SECONDS and each chunk is reduced at once to the
few per-frame values the later stages use (band energies, chroma, mel bands).
Peak memory therefore stays flat however long the track is.
"""

from __future__ import annotations

from collections.abc import Callable
from dataclasses import dataclass

import numpy as np

N_FFT = 2048
HOP = 512
N_MELS = 64
CHUNK_SECONDS = 20.0
# Extra frames either side of each chunk so the harmonic/percussive median
# filters (31 frames) see the same context as on the whole track.
HPSS_MARGIN_FRAMES = 32

BASS_CUTOFF_HZ = 150.0
HIGH_CUTOFF_HZ = 5000.0
MELODY_BAND_HZ = (200.0, 4000.0)

ProgressCallback = Callable[[float], None]


def frame_count(num_samples: int, hop: int) -> int:
    """Frames in a centred STFT, as librosa counts them."""
    return 1 + num_samples // hop


def chunked_stft(
    y: np.ndarray,
    n_fft: int,
    hop: int,
    reduce: Callable[[np.ndarray, slice], np.ndarray],
    chunk_seconds: float = CHUNK_SECONDS,
    sr: int = 44100,
    margin_frames: int = 0,
    on_progress: ProgressCallback | None = None,
) -> np.ndarray:
    """Centred STFT of `y`, one chunk at a time.

    `reduce(stft, keep)` turns each chunk's complex STFT (bins × frames, which
    includes `margin_frames` of context either side) into a smaller array of
    shape (rows × frames); `keep` selects the chunk's own frames. The results
    are joined along the frame axis, so the output matches a whole-track STFT
    frame for frame.
    """
    import librosa

    pad = n_fft // 2
    padded = np.pad(y.astype(np.float32), pad)  # librosa's centring, with zeros
    total = frame_count(len(y), hop)
    per_chunk = max(1, int(chunk_seconds * sr / hop))

    pieces = []
    for first in range(0, total, per_chunk):
        last = min(total, first + per_chunk)
        a = max(0, first - margin_frames)
        b = min(total, last + margin_frames)
        segment = padded[a * hop : (b - 1) * hop + n_fft]
        if len(segment) < n_fft:
            segment = np.pad(segment, (0, n_fft - len(segment)))
        stft = librosa.stft(segment, n_fft=n_fft, hop_length=hop, center=False)
        pieces.append(reduce(stft, slice(first - a, first - a + (last - first))))
        if on_progress:
            on_progress(last / total)
    return np.concatenate(pieces, axis=-1)


@dataclass
class Spectral:
    """Per-frame summaries of the track (HOP samples per frame).

    Powers are scaled so a full-scale sine reads 0 dB.
    """

    sr: int
    bass: np.ndarray  # power below 150 Hz
    low_mid: np.ndarray  # 150–800 Hz
    high_mid: np.ndarray  # 800 Hz – 5 kHz
    high: np.ndarray  # 5 kHz and up
    melody_harmonic: np.ndarray  # harmonic part, 200 Hz – 4 kHz
    melody_percussive: np.ndarray  # percussive part, 200 Hz – 4 kHz
    harmonic_chroma: np.ndarray  # 12 × frames, from the harmonic part
    mel_db: np.ndarray  # N_MELS × frames, whole mix
    percussive_mel_db: np.ndarray  # N_MELS × frames, percussive part

    @property
    def frame_seconds(self) -> float:
        return HOP / self.sr

    @property
    def num_frames(self) -> int:
        return len(self.bass)

    @property
    def total(self) -> np.ndarray:
        return self.bass + self.low_mid + self.high_mid + self.high

    def frames(self, start: float, end: float) -> slice:
        """Frames whose centres fall in [start, end)."""
        first = max(0, int(np.ceil(start / self.frame_seconds)))
        last = max(first + 1, int(np.ceil(end / self.frame_seconds)))
        return slice(first, min(last, self.num_frames))


def compute(mono: np.ndarray, sr: int, on_progress: ProgressCallback | None = None) -> Spectral:
    import librosa

    freqs = librosa.fft_frequencies(sr=sr, n_fft=N_FFT)
    window = np.hanning(N_FFT + 1)[:-1]
    # Mean-square power from a one-sided spectrum (Parseval), relative to a
    # full-scale sine (mean square 0.5), so band sums read in dBFS.
    scale = np.float32(2.0 / (N_FFT * np.sum(window**2)) / 0.5)
    mel_basis = librosa.filters.mel(sr=sr, n_fft=N_FFT, n_mels=N_MELS)

    def band(low: float, high: float) -> np.ndarray:
        return (freqs >= low) & (freqs < high)

    bands = [band(0, BASS_CUTOFF_HZ), band(BASS_CUTOFF_HZ, 800), band(800, HIGH_CUTOFF_HZ), band(HIGH_CUTOFF_HZ, sr)]
    melody = band(*MELODY_BAND_HZ)

    # Chroma depends on the tuning estimate; take it once, from the middle of
    # the track, so every chunk uses the same one.
    middle = len(mono) // 2
    excerpt = mono[max(0, middle - 15 * sr) : middle + 15 * sr]
    tuning = float(librosa.estimate_tuning(y=excerpt, sr=sr)) if len(excerpt) >= N_FFT else 0.0

    def reduce(stft: np.ndarray, keep: slice) -> np.ndarray:
        harmonic, percussive = librosa.decompose.hpss(stft)
        power = (np.abs(stft[:, keep]) ** 2) * scale
        h_power = (np.abs(harmonic[:, keep]) ** 2) * scale
        p_power = (np.abs(percussive[:, keep]) ** 2) * scale
        rows = [power[b].sum(axis=0) for b in bands]
        rows.append(h_power[melody].sum(axis=0))
        rows.append(p_power[melody].sum(axis=0))
        chroma = librosa.feature.chroma_stft(S=h_power, sr=sr, tuning=tuning)
        return np.vstack([np.array(rows), chroma, mel_basis @ power, mel_basis @ p_power]).astype(np.float32)

    stacked = chunked_stft(
        mono, N_FFT, HOP, reduce, sr=sr, margin_frames=HPSS_MARGIN_FRAMES, on_progress=on_progress
    )
    return Spectral(
        sr=sr,
        bass=stacked[0],
        low_mid=stacked[1],
        high_mid=stacked[2],
        high=stacked[3],
        melody_harmonic=stacked[4],
        melody_percussive=stacked[5],
        harmonic_chroma=stacked[6:18],
        mel_db=librosa.power_to_db(stacked[18 : 18 + N_MELS]),
        percussive_mel_db=librosa.power_to_db(stacked[18 + N_MELS :]),
    )


def to_db(power: float | np.ndarray, floor: float = -120.0) -> float | np.ndarray:
    return np.maximum(10.0 * np.log10(np.maximum(power, 1e-20)), floor)


def bar_feature_matrix(spectral: Spectral, bar_spans: list[tuple[float, float]]) -> np.ndarray:
    """Per-bar timbre and harmony summary (features × bars), for section novelty."""
    import librosa

    mfcc = librosa.feature.mfcc(S=spectral.mel_db, n_mfcc=13)
    band_db = to_db(np.stack([spectral.bass, spectral.low_mid, spectral.high_mid, spectral.high]), floor=-90.0)
    columns = []
    for start, end in bar_spans:
        frames = spectral.frames(start, end)
        columns.append(
            np.concatenate(
                [
                    mfcc[:, frames].mean(axis=1),
                    spectral.harmonic_chroma[:, frames].mean(axis=1),
                    band_db[:, frames].mean(axis=1) / 10.0,
                ]
            )
        )
    return np.stack(columns, axis=1)
