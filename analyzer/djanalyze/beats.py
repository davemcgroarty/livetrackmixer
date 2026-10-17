"""Beats, downbeats and tempo.

The spec's models are allin1 (primary: beats, downbeats, BPM, segments) and
Beat This! (cross-check). Both are optional imports. When they are missing, a
built-in librosa estimator stands in, so the pipeline and its tests run anywhere.

v1 supports constant tempo only: the detected beats are fitted with a straight
grid, and when the fit is good the grid itself becomes the beat list. Tracks whose
beats stray from any straight grid are flagged as variable and keep the detected
beats.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path

import numpy as np

BEATS_PER_BAR = 4
MIN_BPM = 70.0
MAX_BPM = 180.0
# Detected beats further than this from the fitted grid (median, as a fraction of
# a beat) make the track variable-tempo.
VARIABLE_TEMPO_TOLERANCE = 0.08
# Two downbeat estimates agree on a bar when they are this close (seconds).
DOWNBEAT_MATCH_SECONDS = 0.07
# More than this share of bars disagreeing marks the beats for review.
DOWNBEAT_DISAGREEMENT_LIMIT = 0.10
LOW_BEAT_CONFIDENCE = 0.3

ONSET_HOP = 128


@dataclass
class BeatEstimate:
    beats: np.ndarray
    downbeats: np.ndarray
    source: str
    segments: list[float] = field(default_factory=list)  # boundary times, allin1 only


@dataclass
class BeatGrid:
    beats: np.ndarray
    downbeat_indices: np.ndarray  # indices into beats
    bpm: float
    variable: bool
    confidence: float
    sources: list[str]
    segments: list[float]

    @property
    def downbeats(self) -> np.ndarray:
        return self.beats[self.downbeat_indices]


# ---------------------------------------------------------------- model backends


def allin1_available() -> bool:
    try:
        import allin1  # noqa: F401
    except Exception:
        return False
    return True


def beat_this_available() -> bool:
    try:
        import beat_this.inference  # noqa: F401
    except Exception:
        return False
    return True


def estimate_allin1(path: Path) -> BeatEstimate:
    import allin1

    result = allin1.analyze(str(path))
    boundaries = sorted({float(s.start) for s in result.segments} | {float(s.end) for s in result.segments})
    # Segment labels are discarded on purpose: sections are boundaries only.
    return BeatEstimate(
        beats=np.asarray(result.beats, dtype=float),
        downbeats=np.asarray(result.downbeats, dtype=float),
        source="allin1",
        segments=boundaries,
    )


def estimate_beat_this(path: Path) -> BeatEstimate:
    from beat_this.inference import File2Beats

    beats, downbeats = File2Beats(checkpoint_path="final0", device="cpu", dbn=False)(str(path))
    return BeatEstimate(np.asarray(beats, dtype=float), np.asarray(downbeats, dtype=float), "beat_this")


# ------------------------------------------------------------- built-in backend


def _onset_envelope(mono: np.ndarray, sr: int) -> np.ndarray:
    import librosa

    return librosa.onset.onset_strength(y=mono, sr=sr, hop_length=ONSET_HOP)


def _low_band_onsets(mono: np.ndarray, sr: int, cutoff: float = 150.0) -> np.ndarray:
    """Spectral flux below `cutoff` Hz (kicks and bass hits), one value per ONSET_HOP."""
    import librosa

    n_fft = 4096
    spectrum = np.abs(librosa.stft(mono, n_fft=n_fft, hop_length=ONSET_HOP))
    low = spectrum[librosa.fft_frequencies(sr=sr, n_fft=n_fft) < cutoff]
    flux = np.maximum(0.0, np.diff(np.log1p(100.0 * low), axis=1)).sum(axis=0)
    return np.concatenate([[0.0], flux])


def estimate_builtin(mono: np.ndarray, sr: int) -> BeatEstimate:
    """librosa beat tracking plus a downbeat-phase heuristic."""
    import librosa

    envelope = _onset_envelope(mono, sr)
    _, frames = librosa.beat.beat_track(
        onset_envelope=envelope, sr=sr, hop_length=ONSET_HOP, start_bpm=120.0, tightness=100, trim=False
    )
    beats = librosa.frames_to_time(np.asarray(frames), sr=sr, hop_length=ONSET_HOP)
    beats = _fold_tempo(beats)
    phase = _downbeat_phase(mono, sr, beats)
    return BeatEstimate(beats=beats, downbeats=beats[phase::BEATS_PER_BAR], source="builtin")


def _fold_tempo(beats: np.ndarray) -> np.ndarray:
    """Halve or double the beat rate until the tempo is in [MIN_BPM, MAX_BPM)."""
    if len(beats) < 4:
        return beats
    period = float(np.median(np.diff(beats)))
    while 60.0 / period >= MAX_BPM and len(beats) >= 8:
        beats = beats[::2]
        period *= 2
    while 60.0 / period < MIN_BPM:
        mids = (beats[:-1] + beats[1:]) / 2
        beats = np.sort(np.concatenate([beats, mids]))
        period /= 2
    return beats


def _downbeat_phase(mono: np.ndarray, sr: int, beats: np.ndarray) -> int:
    """Which of the four beat phases is the downbeat.

    Scores each phase by harmonic change (chords tend to change on the downbeat)
    plus low-frequency onsets (kicks and bass hits), averaged over the track.
    """
    import librosa

    if len(beats) < 2 * BEATS_PER_BAR:
        return 0

    hop = 512
    chroma = librosa.feature.chroma_stft(y=mono, sr=sr, hop_length=hop)
    beat_frames = librosa.time_to_frames(beats, sr=sr, hop_length=hop)
    beat_frames = np.clip(beat_frames, 0, chroma.shape[1] - 1)
    # sync() pads a segment before the first beat; drop it so column i is the
    # stretch from beat i to beat i + 1.
    synced = librosa.util.sync(chroma, beat_frames, aggregate=np.median, pad=True)[:, 1:]
    synced = synced / (np.linalg.norm(synced, axis=0, keepdims=True) + 1e-9)
    # change[i] = chroma change going into beat i
    change = np.zeros(len(beats))
    n = min(len(beats), synced.shape[1])
    change[1:n] = 1.0 - np.sum(synced[:, 1:n] * synced[:, : n - 1], axis=0)

    low = _low_band_onsets(mono, sr)
    low_frames = librosa.time_to_frames(beats, sr=sr, hop_length=ONSET_HOP)
    window = 3
    low_at_beats = np.array(
        [low[max(0, f - window) : f + window + 1].max(initial=0.0) if f < len(low) else 0.0 for f in low_frames]
    )

    def phase_scores(values: np.ndarray) -> np.ndarray:
        scores = np.array([values[p::BEATS_PER_BAR].mean() for p in range(BEATS_PER_BAR)])
        return scores / (scores.mean() + 1e-9)

    scores = 2.0 * phase_scores(change) + phase_scores(low_at_beats)
    return int(np.argmax(scores))


# ------------------------------------------------------------------- grid fitting


def fit_grid(beats: np.ndarray) -> tuple[float, float, float]:
    """Least-squares straight grid through the beats.

    Returns (period, offset, median deviation as a fraction of a beat), with
    beat i at offset + i * period.
    """
    if len(beats) < 2:
        raise ValueError("need at least two beats to fit a grid")
    period = float(np.median(np.diff(beats)))
    # Number beats by rounding to the median period so a missed or extra beat
    # does not shift every later index.
    index = np.round((beats - beats[0]) / period)
    slope, intercept = np.polyfit(index, beats, 1)
    deviation = float(np.median(np.abs(beats - (intercept + slope * index))) / slope)
    return float(slope), float(intercept), deviation


def refine_phase(mono: np.ndarray, sr: int, period: float, offset: float) -> float:
    """Move the grid offset to where onsets line up best, within ±40 ms."""
    envelope = _onset_envelope(mono, sr)
    frame_time = ONSET_HOP / sr
    times = np.arange(len(envelope)) * frame_time
    # Onset envelopes peak a little after the attack; compare against the rise.
    candidates = np.arange(-0.04, 0.0401, 0.001)
    best, best_score = 0.0, -np.inf
    duration = times[-1] if len(times) else 0.0
    for delta in candidates:
        grid = np.arange(offset + delta - period * np.floor((offset + delta) / period), duration, period)
        frames = np.round(grid / frame_time).astype(int)
        frames = frames[(frames >= 0) & (frames < len(envelope))]
        score = envelope[frames].sum()
        if score > best_score:
            best, best_score = delta, score
    return offset + best


def constant_grid(period: float, offset: float, duration: float) -> np.ndarray:
    """Every grid beat from the first one at or after 0 s to the end of the track."""
    first = offset - period * np.floor(offset / period)
    if first < 0:
        first += period
    return np.arange(first, duration, period)


def downbeat_indices_from(downbeats: np.ndarray, beats: np.ndarray) -> np.ndarray:
    """Map estimated downbeat times onto a beat list by majority phase."""
    if len(downbeats) == 0 or len(beats) == 0:
        return np.arange(0, len(beats), BEATS_PER_BAR)
    nearest = np.abs(beats[None, :] - downbeats[:, None]).argmin(axis=1)
    phase = int(np.bincount(nearest % BEATS_PER_BAR, minlength=BEATS_PER_BAR).argmax())
    return np.arange(phase, len(beats), BEATS_PER_BAR)


def downbeat_disagreement(a: np.ndarray, b: np.ndarray, tolerance: float = DOWNBEAT_MATCH_SECONDS) -> float:
    """Share of downbeats in `a` with no downbeat in `b` within `tolerance`."""
    if len(a) == 0:
        return 0.0 if len(b) == 0 else 1.0
    if len(b) == 0:
        return 1.0
    distances = np.abs(a[:, None] - b[None, :]).min(axis=1)
    return float(np.mean(distances > tolerance))


# --------------------------------------------------------------------- top level


def build_grid(
    mono: np.ndarray,
    sr: int,
    duration: float,
    primary: BeatEstimate,
    secondary: BeatEstimate | None = None,
) -> BeatGrid:
    if len(primary.beats) < 2 * BEATS_PER_BAR:
        raise ValueError("too few beats detected to build a grid")

    period, offset, deviation = fit_grid(primary.beats)
    variable = deviation > VARIABLE_TEMPO_TOLERANCE

    if variable:
        beats = primary.beats
    else:
        offset = refine_phase(mono, sr, period, offset)
        beats = constant_grid(period, offset, duration)

    downbeat_indices = downbeat_indices_from(primary.downbeats, beats)
    downbeats = beats[downbeat_indices]

    # Confidence starts from how well the beats fit a straight grid.
    confidence = float(np.clip(1.0 - deviation / VARIABLE_TEMPO_TOLERANCE, 0.0, 1.0))
    sources = [primary.source]
    if secondary is not None and len(secondary.downbeats):
        sources.append(secondary.source)
        disagreement = downbeat_disagreement(downbeats, secondary.downbeats)
        if disagreement > DOWNBEAT_DISAGREEMENT_LIMIT:
            confidence = min(confidence, LOW_BEAT_CONFIDENCE)
        else:
            confidence = min(confidence, 1.0 - disagreement)
    else:
        # Without a second opinion the downbeat is never certain.
        confidence = min(confidence, 0.7)

    return BeatGrid(
        beats=beats,
        downbeat_indices=downbeat_indices,
        bpm=60.0 / period,
        variable=variable,
        confidence=confidence,
        sources=sources,
        segments=primary.segments,
    )
