"""Spectral features shared by the descriptor, key and structure stages.

One STFT and one harmonic/percussive split per track, reused everywhere.
"""

from __future__ import annotations

from dataclasses import dataclass

import numpy as np

N_FFT = 2048
HOP = 512


@dataclass
class Spectral:
    sr: int
    power: np.ndarray  # |X|^2, bins × frames, scaled so a full-scale sine reads 0 dB
    harmonic: np.ndarray  # same scaling
    percussive: np.ndarray
    freqs: np.ndarray

    @property
    def frame_seconds(self) -> float:
        return HOP / self.sr

    def frames(self, start: float, end: float) -> slice:
        """Frames whose centres fall in [start, end)."""
        first = max(0, int(np.ceil(start / self.frame_seconds)))
        last = max(first + 1, int(np.ceil(end / self.frame_seconds)))
        return slice(first, min(last, self.power.shape[1]))

    def band(self, low: float, high: float) -> np.ndarray:
        return (self.freqs >= low) & (self.freqs < high)


def compute(mono: np.ndarray, sr: int) -> Spectral:
    import librosa

    stft = librosa.stft(mono, n_fft=N_FFT, hop_length=HOP, window="hann")
    harmonic, percussive = librosa.decompose.hpss(stft)
    window = np.hanning(N_FFT + 1)[:-1]
    # Mean-square power from a one-sided spectrum (Parseval), relative to a
    # full-scale sine (mean square 0.5), so band sums read in dBFS.
    scale = 2.0 / (N_FFT * np.sum(window**2)) / 0.5

    def to_power(x: np.ndarray) -> np.ndarray:
        return (np.abs(x) ** 2) * scale

    return Spectral(
        sr=sr,
        power=to_power(stft),
        harmonic=to_power(harmonic),
        percussive=to_power(percussive),
        freqs=librosa.fft_frequencies(sr=sr, n_fft=N_FFT),
    )


def to_db(power: float | np.ndarray, floor: float = -120.0) -> float | np.ndarray:
    return np.maximum(10.0 * np.log10(np.maximum(power, 1e-20)), floor)


def bar_feature_matrix(spectral: Spectral, bar_spans: list[tuple[float, float]]) -> np.ndarray:
    """Per-bar timbre and harmony summary (features × bars), for section novelty."""
    import librosa

    mel = librosa.feature.melspectrogram(S=spectral.power, sr=spectral.sr, n_mels=64)
    mfcc = librosa.feature.mfcc(S=librosa.power_to_db(mel), n_mfcc=13)
    chroma = librosa.feature.chroma_stft(S=spectral.harmonic, sr=spectral.sr)
    bands = np.stack(
        [
            spectral.power[spectral.band(lo, hi)].sum(axis=0)
            for lo, hi in ((0, 150), (150, 800), (800, 5000), (5000, spectral.sr / 2 + 1))
        ]
    )
    band_db = to_db(bands, floor=-90.0)
    columns = []
    for start, end in bar_spans:
        frames = spectral.frames(start, end)
        columns.append(
            np.concatenate(
                [
                    mfcc[:, frames].mean(axis=1),
                    chroma[:, frames].mean(axis=1),
                    band_db[:, frames].mean(axis=1) / 10.0,
                ]
            )
        )
    return np.stack(columns, axis=1)
