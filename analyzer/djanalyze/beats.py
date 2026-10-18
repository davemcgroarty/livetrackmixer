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

from .features import chunked_stft

BEATS_PER_BAR = 4
MIN_BPM = 70.0
MAX_BPM = 180.0
# Detected beats further than this from the fitted grid (median, as a fraction of
# a beat) make the track variable-tempo.
VARIABLE_TEMPO_TOLERANCE = 0.08
# A constant grid must match the local beat phase this closely (seconds) in a
# window for that window to count as on the grid.
GRID_WINDOW_TOLERANCE = 0.025
GRID_WINDOW_BEATS = 16
# Windows whose folded onsets peak less than this far above an even spread have
# no clear beat (breakdowns, pads) and are left out of the check.
MIN_WINDOW_PEAKINESS = 1.5
# Below this share of on-grid windows the tempo is treated as variable.
MIN_GRID_CONSISTENCY = 0.5
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
    # Built-in estimator only: a constant grid fitted to the onset envelope.
    grid: ConstantGrid | None = None


@dataclass(frozen=True)
class ConstantGrid:
    period: float  # seconds per beat
    offset: float  # time of one beat (seconds)
    consistency: float  # share of the track's rhythm that sits on this grid, 0–1


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


def onset_envelope(mono: np.ndarray, sr: int) -> np.ndarray:
    """librosa's onset strength (128 mel bands, one value per ONSET_HOP), computed
    in chunks so memory stays flat on long tracks."""
    import librosa

    n_fft = 2048
    mel_basis = librosa.filters.mel(sr=sr, n_fft=n_fft, n_mels=128)
    mel = chunked_stft(mono, n_fft, ONSET_HOP, lambda stft, keep: mel_basis @ (np.abs(stft[:, keep]) ** 2), sr=sr)
    return librosa.onset.onset_strength(
        S=librosa.power_to_db(mel, ref=np.max), sr=sr, hop_length=ONSET_HOP, n_fft=n_fft
    )


def _low_band_onsets(mono: np.ndarray, sr: int, cutoff: float = 150.0) -> np.ndarray:
    """Spectral flux below `cutoff` Hz (kicks and bass hits), one value per ONSET_HOP."""
    import librosa

    n_fft = 4096
    low_bins = librosa.fft_frequencies(sr=sr, n_fft=n_fft) < cutoff
    low = chunked_stft(mono, n_fft, ONSET_HOP, lambda stft, keep: np.abs(stft[low_bins, keep]), sr=sr)
    flux = np.maximum(0.0, np.diff(np.log1p(100.0 * low), axis=1)).sum(axis=0)
    return np.concatenate([[0.0], flux])


def estimate_builtin(mono: np.ndarray, sr: int, envelope: np.ndarray, chroma: np.ndarray, chroma_hop: int) -> BeatEstimate:
    """Built-in beats: a constant grid fitted to the whole track's onsets, with
    librosa's beat tracker as the fallback for variable-tempo tracks.

    `envelope` is onset_envelope(mono, sr); `chroma` is a 12 × frames chroma of
    the track with `chroma_hop` samples per frame.
    """
    import librosa

    bpm_guess = fold_bpm(global_tempo(envelope, sr))
    grid = fit_constant_grid(envelope, sr, bpm_guess)

    # Passing the tempo in skips librosa's own estimate, which builds an
    # 8-second autocorrelation for every frame: gigabytes on a long track.
    _, frames = librosa.beat.beat_track(
        onset_envelope=envelope, sr=sr, hop_length=ONSET_HOP, bpm=60.0 / grid.period, tightness=100, trim=False
    )
    tracked = _fold_tempo(librosa.frames_to_time(np.asarray(frames), sr=sr, hop_length=ONSET_HOP))

    duration = len(mono) / sr
    on_grid = grid.consistency >= MIN_GRID_CONSISTENCY
    reference = constant_grid(grid.period, grid.offset, duration) if on_grid else tracked
    phase = _downbeat_phase(mono, sr, reference, chroma, chroma_hop)
    return BeatEstimate(
        beats=tracked, downbeats=reference[phase::BEATS_PER_BAR], source="builtin", grid=grid
    )


def fold_bpm(bpm: float) -> float:
    """Halve or double a tempo into [MIN_BPM, MAX_BPM)."""
    while bpm >= MAX_BPM:
        bpm /= 2
    while bpm < MIN_BPM:
        bpm *= 2
    return bpm


def _fold_onsets(envelope: np.ndarray, times: np.ndarray, period: float, bin_seconds: float = 0.005):
    """Fold onset strength onto one beat period.

    Returns (peakiness, phase): how far the strongest 15 ms of the folded beat
    stands above an even spread (1.0 = no beat at all), and where in the beat
    (seconds from the start of the cycle) that peak is.
    """
    bins = max(3, int(round(period / bin_seconds)))
    index = (np.floor((times % period) / period * bins)).astype(np.int64) % bins
    histogram = np.bincount(index, weights=envelope, minlength=bins)
    total = histogram.sum()
    if total <= 0:
        return 1.0, 0.0
    smoothed = histogram + np.roll(histogram, 1) + np.roll(histogram, -1)
    peak = int(np.argmax(smoothed))
    peakiness = float(smoothed[peak] / (3.0 * total / bins))
    return peakiness, (peak + 0.5) / bins * period


def fit_constant_grid(envelope: np.ndarray, sr: int, bpm_guess: float) -> ConstantGrid:
    """The constant-tempo grid that best explains the whole track's onsets.

    Searches tempos within 3% of `bpm_guess` (coarse, then fine) for the period
    whose folded onsets peak most sharply, then checks the grid against the
    music in windows of GRID_WINDOW_BEATS beats: `consistency` is the share of
    rhythmic windows (weighted by how clear their beat is) whose own beat phase
    stays within GRID_WINDOW_TOLERANCE of the grid's eighth notes.

    Unlike fitting a line through tracked beats, this is not thrown by a beat
    tracker that follows syncopation for a few beats.
    """
    frame = ONSET_HOP / sr
    envelope = np.maximum(envelope.astype(np.float64) - np.median(envelope), 0.0)
    times = np.arange(len(envelope)) * frame

    def best_bpm(candidates: np.ndarray) -> float:
        scores = [_fold_onsets(envelope, times, 60.0 / b)[0] for b in candidates]
        return float(candidates[int(np.argmax(scores))])

    coarse = best_bpm(np.arange(bpm_guess * 0.97, bpm_guess * 1.03, 0.05))
    fine = best_bpm(np.arange(coarse - 0.06, coarse + 0.0601, 0.002))
    period = 60.0 / fine
    _, offset = _fold_onsets(envelope, times, period)

    # Each window's own beat phase, relative to the grid, compared modulo half
    # a beat: a section driven by off-beat hats peaks between the grid's beats
    # but is still on its eighth-note grid. Phases only repeat, so follow the
    # drift from window to window (unwrapping): a track that slowly drifts a
    # whole beat must not look aligned again. Only windows with a clear beat
    # take part, so a noisy one cannot introduce a false wrap.
    window = int(round(GRID_WINDOW_BEATS * period / frame))
    weights, drifts = [], []
    for start in range(0, max(1, len(envelope) - window // 2), max(1, window)):
        part = slice(start, start + window)
        peakiness, local = _fold_onsets(envelope[part], times[part], period)
        if peakiness < MIN_WINDOW_PEAKINESS:
            continue
        weights.append(peakiness - 1.0)
        half = period / 2
        drifts.append((local - offset + half / 2) % half - half / 2)

    if not weights:
        return ConstantGrid(period=period, offset=offset, consistency=0.0)
    unwrapped = np.unwrap(np.array(drifts), period=period / 2)
    hits = np.abs(unwrapped) <= GRID_WINDOW_TOLERANCE
    consistency = float(np.average(hits, weights=weights))
    return ConstantGrid(period=period, offset=offset, consistency=consistency)


def global_tempo(envelope: np.ndarray, sr: int, prior_bpm: float = 120.0) -> float:
    """The track's main tempo: the strongest periodicity of the onset envelope.

    One autocorrelation of the whole envelope (O(n log n) time, O(n) memory),
    weighted by a log-normal prior around `prior_bpm` as librosa's tempo
    estimate is, with parabolic interpolation between lags.
    """
    x = envelope.astype(np.float64) - float(np.mean(envelope))
    n = len(x)
    if n < 4:
        return prior_bpm
    spectrum = np.fft.rfft(x, 2 * n)
    autocorr = np.fft.irfft(np.abs(spectrum) ** 2)[:n]

    frames_per_minute = 60.0 * sr / ONSET_HOP
    min_lag = max(1, int(frames_per_minute / 240.0))
    max_lag = min(n - 2, int(np.ceil(frames_per_minute / 40.0)))
    if max_lag <= min_lag:
        return prior_bpm
    lags = np.arange(min_lag, max_lag + 1)
    bpms = frames_per_minute / lags
    prior = np.exp(-0.5 * (np.log2(bpms / prior_bpm) / 1.0) ** 2)
    scores = autocorr[lags] * prior
    best = int(np.argmax(scores))

    lag = float(lags[best])
    if 0 < best < len(lags) - 1:
        a, b, c = scores[best - 1], scores[best], scores[best + 1]
        denominator = a - 2 * b + c
        if denominator < 0:
            lag += 0.5 * (a - c) / denominator
    return float(frames_per_minute / lag)


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


def _downbeat_phase(mono: np.ndarray, sr: int, beats: np.ndarray, chroma: np.ndarray, hop: int) -> int:
    """Which of the four beat phases is the downbeat.

    Scores each phase by harmonic change (chords tend to change on the downbeat)
    plus low-frequency onsets (kicks and bass hits), averaged over the track.
    """
    import librosa

    if len(beats) < 2 * BEATS_PER_BAR:
        return 0

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
    # Number the beats by counting each gap in median periods (1 normally, 2
    # across a missed beat). Counting gaps rather than dividing each beat's time
    # by the median period keeps a tiny error in that period (beats sit on
    # analysis frames) from adding up over hundreds of beats.
    steps = np.maximum(1.0, np.round(np.diff(beats) / period))
    index = np.concatenate([[0.0], np.cumsum(steps)])
    slope, intercept = np.polyfit(index, beats, 1)
    deviation = float(np.median(np.abs(beats - (intercept + slope * index))) / slope)
    return float(slope), float(intercept), deviation


def refine_phase(envelope: np.ndarray, sr: int, period: float, offset: float) -> float:
    """Move the grid offset to where onsets (`envelope`, from onset_envelope) line up best, within ±40 ms."""
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
    envelope: np.ndarray,
    sr: int,
    duration: float,
    primary: BeatEstimate,
    secondary: BeatEstimate | None = None,
) -> BeatGrid:
    if len(primary.beats) < 2 * BEATS_PER_BAR:
        raise ValueError("too few beats detected to build a grid")

    if primary.grid is not None:
        # Built-in estimate: the grid was fitted to the onsets directly.
        period, offset = primary.grid.period, primary.grid.offset
        variable = primary.grid.consistency < MIN_GRID_CONSISTENCY
        fit_confidence = primary.grid.consistency
        if variable:
            period = fit_grid(primary.beats)[0]
    else:
        period, offset, deviation = fit_grid(primary.beats)
        variable = deviation > VARIABLE_TEMPO_TOLERANCE
        fit_confidence = float(np.clip(1.0 - deviation / VARIABLE_TEMPO_TOLERANCE, 0.0, 1.0))

    if variable:
        beats = primary.beats
    else:
        offset = refine_phase(envelope, sr, period, offset)
        beats = constant_grid(period, offset, duration)

    downbeat_indices = downbeat_indices_from(primary.downbeats, beats)
    downbeats = beats[downbeat_indices]

    # Confidence starts from how well the beats fit a straight grid.
    confidence = fit_confidence
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
