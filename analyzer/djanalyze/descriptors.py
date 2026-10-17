"""Per-bar content descriptors: rhythm, bass, high end and melody, each 0 to 1.

Scores map raw measurements through fixed reference ranges (REFERENCE), not
per-track normalisation, so a 0.8 bass section means the same in every track.
The raw values are returned too and stored in the analysis file.

Calibration: REFERENCE was set against the synthetic test clips
(test-audio/clips.json). It still needs checking against a sample of real tracks,
which the spec schedules for M1.
"""

from __future__ import annotations

from dataclasses import dataclass, field

import numpy as np

from .features import Spectral, to_db

BASS_CUTOFF_HZ = 150.0
HIGH_CUTOFF_HZ = 5000.0
MELODY_BAND_HZ = (200.0, 4000.0)
GRID_TOLERANCE_SECONDS = 0.03


@dataclass(frozen=True)
class Range:
    low: float
    high: float

    def score(self, value: float) -> float:
        return float(np.clip((value - self.low) / (self.high - self.low), 0.0, 1.0))


# Raw value that maps to 0 and to 1 for each quality.
REFERENCE = {
    "rhythm_onset": Range(0.0, 1.5),  # mean percussive onset strength per frame
    "bass_db": Range(-45.0, -5.0),  # dBFS, energy below 150 Hz
    "high_db": Range(-60.0, -20.0),  # dBFS, energy above 5 kHz
    "melody_db": Range(-50.0, -12.0),  # dBFS, harmonic energy 200 Hz – 4 kHz
    "energy_db": Range(-45.0, -3.0),  # dBFS, full band
}


@dataclass
class BarDescriptors:
    rhythm: list[float] = field(default_factory=list)
    bass: list[float] = field(default_factory=list)
    high: list[float] = field(default_factory=list)
    melody: list[float] = field(default_factory=list)
    energy: list[float] = field(default_factory=list)
    raw: dict[str, list[float]] = field(
        default_factory=lambda: {
            "rhythmOnsetStrength": [],
            "rhythmRegularity": [],
            "bassDb": [],
            "highDb": [],
            "melodyDb": [],
            "melodySalience": [],
        }
    )


def _percussive_onsets(spectral: Spectral) -> np.ndarray:
    import librosa

    mel = librosa.feature.melspectrogram(S=spectral.percussive, sr=spectral.sr, n_mels=64)
    return librosa.onset.onset_strength(S=librosa.power_to_db(mel, ref=1.0), sr=spectral.sr)


def _regularity(onsets: np.ndarray, frame_seconds: float, frames: slice, grid: np.ndarray) -> float:
    """How much of the bar's onset energy lands near 8th-note grid positions.

    Rescaled so onsets spread evenly in time score 0 and onsets only on the grid
    score 1.
    """
    values = onsets[frames]
    total = float(values.sum())
    if total <= 0 or len(grid) == 0:
        return 0.0
    times = (np.arange(frames.start, frames.start + len(values))) * frame_seconds
    near = np.abs(times[:, None] - grid[None, :]).min(axis=1) <= GRID_TOLERANCE_SECONDS
    on_grid = float(values[near].sum()) / total
    coverage = float(np.mean(near))
    if coverage >= 1.0:
        return 1.0
    return float(np.clip((on_grid - coverage) / (1.0 - coverage), 0.0, 1.0))


def _chroma_clarity(chroma: np.ndarray) -> float:
    """1 for a few clear pitch classes, 0 for flat (noise-like) chroma."""
    profile = chroma.mean(axis=1)
    total = profile.sum()
    if total <= 0:
        return 0.0
    p = profile / total
    entropy = -np.sum(p * np.log(p + 1e-12)) / np.log(len(p))
    return float(np.clip(1.0 - entropy, 0.0, 1.0) ** 0.5)


def compute(
    spectral: Spectral,
    bar_spans: list[tuple[float, float]],
    beats: np.ndarray,
    salience: np.ndarray | None = None,
) -> BarDescriptors:
    """Score every bar. `salience` is an optional per-frame pitch salience (Essentia)."""
    import librosa

    onsets = _percussive_onsets(spectral)
    chroma = librosa.feature.chroma_stft(S=spectral.harmonic, sr=spectral.sr)
    bass_band = spectral.band(0.0, BASS_CUTOFF_HZ)
    high_band = spectral.band(HIGH_CUTOFF_HZ, spectral.sr)
    melody_band = spectral.band(*MELODY_BAND_HZ)

    # 8th-note positions across the track.
    if len(beats) > 1:
        eighths = np.sort(np.concatenate([beats, (beats[:-1] + beats[1:]) / 2]))
    else:
        eighths = beats

    out = BarDescriptors()
    for start, end in bar_spans:
        frames = spectral.frames(start, end)
        power = spectral.power[:, frames]
        harmonic = spectral.harmonic[:, frames]
        percussive = spectral.percussive[:, frames]

        onset_strength = float(onsets[frames].mean()) if onsets[frames].size else 0.0
        regularity = _regularity(onsets, spectral.frame_seconds, frames, eighths[(eighths >= start) & (eighths < end)])
        bass_db = float(to_db(power[bass_band].sum(axis=0).mean()))
        high_db = float(to_db(power[high_band].sum(axis=0).mean()))
        melody_power = harmonic[melody_band].sum(axis=0).mean()
        melody_db = float(to_db(melody_power))

        if salience is not None:
            bar_salience = float(np.clip(np.mean(salience[frames]) if salience[frames].size else 0.0, 0.0, 1.0))
        else:
            # Without Essentia: share of the band's energy that is harmonic.
            total = melody_power + percussive[melody_band].sum(axis=0).mean()
            bar_salience = float(melody_power / total) if total > 0 else 0.0
        clarity = _chroma_clarity(chroma[:, frames])

        out.rhythm.append(REFERENCE["rhythm_onset"].score(onset_strength) * regularity)
        out.bass.append(REFERENCE["bass_db"].score(bass_db))
        out.high.append(REFERENCE["high_db"].score(high_db))
        out.melody.append(REFERENCE["melody_db"].score(melody_db) * (0.5 * clarity + 0.5 * bar_salience))
        out.energy.append(REFERENCE["energy_db"].score(float(to_db(power.sum(axis=0).mean()))))

        out.raw["rhythmOnsetStrength"].append(onset_strength)
        out.raw["rhythmRegularity"].append(regularity)
        out.raw["bassDb"].append(bass_db)
        out.raw["highDb"].append(high_db)
        out.raw["melodyDb"].append(melody_db)
        out.raw["melodySalience"].append(bar_salience)

    return out


def per_section(values: list[float], sections: list[tuple[int, int]]) -> list[float]:
    """Mean of per-bar values over each (startBar, lengthBars) section, 1-based bars."""
    result = []
    for start_bar, length in sections:
        chunk = values[start_bar - 1 : start_bar - 1 + length]
        result.append(float(np.mean(chunk)) if chunk else 0.0)
    return result
