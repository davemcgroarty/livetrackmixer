"""Per-bar content descriptors: rhythm, bass, high end and melody, each 0 to 1.

Scores map raw measurements through fixed reference ranges (REFERENCE), not
per-track normalisation, so a 0.8 bass section means the same in every track.
The raw values are returned too and stored in the analysis file.

Calibration: REFERENCE was first set against the synthetic test clips
(test-audio/clips.json). The rhythm ranges were then reset against real
recordings: dance and drum & bass, pop, ragtime, orchestral, solo trumpet and
tempo-drifting material. With the first ranges a steady four-on-the-floor bar
scored about 0.6 and real dance tracks about 0.3.
"""

from __future__ import annotations

from dataclasses import dataclass, field

import numpy as np

from .features import Spectral, to_db

# Rhythm periodicity: the onset envelope is smoothed by this many frames
# (about 17 ms) and compared with itself at lags within this many frames of a
# beat, half a bar and a bar, so human timing and frame rounding do not count
# as irregular.
PERIODICITY_SMOOTHING_FRAMES = 1.5
PERIODICITY_LAG_TOLERANCE_FRAMES = 1


@dataclass(frozen=True)
class Range:
    low: float
    high: float

    def score(self, value: float) -> float:
        return float(np.clip((value - self.low) / (self.high - self.low), 0.0, 1.0))


# Raw value that maps to 0 and to 1 for each quality.
REFERENCE = {
    "rhythm_onset": Range(0.2, 0.9),  # mean percussive onset strength per frame
    "rhythm_periodicity": Range(0.1, 0.8),  # onset envelope autocorrelation at beat and bar lags
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

    return librosa.onset.onset_strength(S=spectral.percussive_mel_db, sr=spectral.sr)


def _smoothed(onsets: np.ndarray) -> np.ndarray:
    from scipy.ndimage import gaussian_filter1d

    return gaussian_filter1d(onsets.astype(float), PERIODICITY_SMOOTHING_FRAMES)


def _periodicity(envelope: np.ndarray, frame_seconds: float, start: float, end: float, period: float) -> float:
    """How strongly the onsets around a bar repeat every beat, half bar or bar.

    The normalised autocorrelation of the (smoothed, mean-removed) onset
    envelope over the bar and one bar either side, at the best of those lags:
    near 1 for a pattern that repeats, near 0 for onsets with no steady pulse.
    It needs the beat period but not the grid's phase, so a grid a few
    milliseconds off does not lower it.
    """
    if period <= 0 or end <= start:
        return 0.0
    length = end - start
    first = max(0, int((start - length) / frame_seconds))
    last = min(len(envelope), int((end + length) / frame_seconds) + 1)
    x = envelope[first:last] - envelope[first:last].mean()
    power = float(np.dot(x, x))
    if power <= 0:
        return 0.0
    best = 0.0
    for beats in (1, 2, 4):
        centre = int(round(beats * period / frame_seconds))
        for lag in range(centre - PERIODICITY_LAG_TOLERANCE_FRAMES, centre + PERIODICITY_LAG_TOLERANCE_FRAMES + 1):
            if 0 < lag < len(x):
                # Scaled up for the shorter overlap so long lags are not penalised.
                r = float(np.dot(x[:-lag], x[lag:])) / power * len(x) / (len(x) - lag)
                best = max(best, r)
    return float(np.clip(best, 0.0, 1.0))


def _local_period(beats: np.ndarray, start: float, end: float) -> float:
    """Median beat gap around a bar, so variable-tempo tracks use their local tempo."""
    length = end - start
    near = beats[(beats >= start - length) & (beats <= end + length)]
    if len(near) < 2:
        near = beats
    return float(np.median(np.diff(near))) if len(near) > 1 else 0.0


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
    onsets = _percussive_onsets(spectral)
    envelope = _smoothed(onsets)
    total_power = spectral.total

    out = BarDescriptors()
    for start, end in bar_spans:
        frames = spectral.frames(start, end)

        onset_strength = float(onsets[frames].mean()) if onsets[frames].size else 0.0
        periodicity = _periodicity(envelope, spectral.frame_seconds, start, end, _local_period(beats, start, end))
        bass_db = float(to_db(spectral.bass[frames].mean()))
        high_db = float(to_db(spectral.high[frames].mean()))
        melody_power = spectral.melody_harmonic[frames].mean()
        melody_db = float(to_db(melody_power))

        if salience is not None:
            bar_salience = float(np.clip(np.mean(salience[frames]) if salience[frames].size else 0.0, 0.0, 1.0))
        else:
            # Without Essentia: share of the band's energy that is harmonic.
            total = melody_power + spectral.melody_percussive[frames].mean()
            bar_salience = float(melody_power / total) if total > 0 else 0.0
        clarity = _chroma_clarity(spectral.harmonic_chroma[:, frames])

        out.rhythm.append(
            REFERENCE["rhythm_onset"].score(onset_strength) * REFERENCE["rhythm_periodicity"].score(periodicity)
        )
        out.bass.append(REFERENCE["bass_db"].score(bass_db))
        out.high.append(REFERENCE["high_db"].score(high_db))
        out.melody.append(REFERENCE["melody_db"].score(melody_db) * (0.5 * clarity + 0.5 * bar_salience))
        out.energy.append(REFERENCE["energy_db"].score(float(to_db(total_power[frames].mean()))))

        out.raw["rhythmOnsetStrength"].append(onset_strength)
        out.raw["rhythmRegularity"].append(periodicity)
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
