"""Musical key with Camelot codes, overall and per section.

Essentia's KeyExtractor is used when it is installed. Otherwise a built-in
estimator correlates log-compressed harmonic chroma with the Albrecht–Shanahan
key profiles (Music Perception 31(1), 2013). On the synthetic test clips that
pairing beat Krumhansl–Kessler, Temperley and EDMA profiles, and CQT or CENS chroma.
"""

from __future__ import annotations

from dataclasses import dataclass

import numpy as np

# Pitch-class spellings as DJ software usually shows them.
MAJOR_NAMES = ["C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"]
MINOR_NAMES = ["C", "C#", "D", "Eb", "E", "F", "F#", "G", "G#", "A", "Bb", "B"]

# Camelot number by tonic pitch class (C = 0).
CAMELOT_MAJOR = [8, 3, 10, 5, 12, 7, 2, 9, 4, 11, 6, 1]
CAMELOT_MINOR = [5, 12, 7, 2, 9, 4, 11, 6, 1, 8, 3, 10]

_PITCH_CLASS = {
    "C": 0, "B#": 0, "C#": 1, "Db": 1, "D": 2, "D#": 3, "Eb": 3, "E": 4, "Fb": 4, "F": 5, "E#": 5,
    "F#": 6, "Gb": 6, "G": 7, "G#": 8, "Ab": 8, "A": 9, "A#": 10, "Bb": 10, "B": 11, "Cb": 11,
}  # fmt: skip

# Albrecht–Shanahan key profiles, tonic first.
PROFILE_MAJOR = np.array([0.238, 0.006, 0.111, 0.006, 0.137, 0.094, 0.016, 0.214, 0.009, 0.080, 0.008, 0.081])
PROFILE_MINOR = np.array([0.220, 0.006, 0.104, 0.123, 0.019, 0.103, 0.012, 0.214, 0.062, 0.022, 0.061, 0.052])
# Chroma is compressed with log1p(CHROMA_COMPRESSION * chroma) so that loud
# upper harmonics count less against the notes actually played.
CHROMA_COMPRESSION = 1000.0

# Below these, a section has no reliable key.
MIN_SECTION_KEY_CONFIDENCE = 0.5
MIN_SECTION_MELODY = 0.15


@dataclass(frozen=True)
class Key:
    tonic: int  # pitch class, C = 0
    minor: bool
    confidence: float

    @property
    def name(self) -> str:
        names = MINOR_NAMES if self.minor else MAJOR_NAMES
        return f"{names[self.tonic]} {'minor' if self.minor else 'major'}"

    @property
    def camelot(self) -> str:
        number = (CAMELOT_MINOR if self.minor else CAMELOT_MAJOR)[self.tonic]
        return f"{number}{'A' if self.minor else 'B'}"


def key_from_name(tonic: str, scale: str, confidence: float) -> Key:
    return Key(_PITCH_CLASS[tonic], scale.lower().startswith("min"), float(np.clip(confidence, 0.0, 1.0)))


def relative(key: Key) -> tuple[int, bool]:
    return ((key.tonic + 3) % 12, False) if key.minor else ((key.tonic + 9) % 12, True)


def estimate_from_chroma(profile: np.ndarray) -> Key:
    """Best-matching key for a 12-bin chroma profile (C first).

    Confidence combines how well the best key correlates (0.15 or less counts as
    no key, 0.6 or more as a clear one) with how far it beats the best key that
    is neither it nor its relative major/minor, which share its notes and are
    hard to tell apart from chroma alone.
    """
    if profile.sum() <= 0 or np.ptp(profile) <= 1e-9 * profile.max():
        return Key(0, False, 0.0)  # silence or flat chroma: no key

    scores = {}
    for tonic in range(12):
        rolled = np.roll(profile, -tonic)
        scores[(tonic, False)] = float(np.corrcoef(rolled, PROFILE_MAJOR)[0, 1])
        scores[(tonic, True)] = float(np.corrcoef(rolled, PROFILE_MINOR)[0, 1])

    (tonic, minor), best = max(scores.items(), key=lambda kv: kv[1])
    rel = relative(Key(tonic, minor, 0.0))
    rival = max(v for k, v in scores.items() if k not in {(tonic, minor), rel})
    fit = float(np.clip((best - 0.15) / 0.45, 0.0, 1.0))
    margin = float(np.clip((best - rival) / 0.1, 0.0, 1.0))
    confidence = fit * margin**0.5
    return Key(tonic, minor, confidence)


def essentia_available() -> bool:
    try:
        import essentia.standard  # noqa: F401
    except Exception:
        return False
    return True


def estimate_essentia(mono: np.ndarray) -> Key:
    import essentia.standard as es

    tonic, scale, strength = es.KeyExtractor(profileType="edma")(mono.astype(np.float32))
    return key_from_name(tonic, scale, strength)


class KeyEstimator:
    """Overall and per-section keys from one chroma computation."""

    def __init__(self, mono: np.ndarray, sr: int, harmonic_chroma: np.ndarray, hop: int, use_essentia: bool) -> None:
        """harmonic_chroma: 12 × frames chroma of the harmonic part, `hop` samples per frame."""
        self.mono = mono
        self.sr = sr
        self.hop = hop
        self.use_essentia = use_essentia
        if not use_essentia:
            self.chroma = np.log1p(CHROMA_COMPRESSION * harmonic_chroma)

    def estimate(self, start: float = 0.0, end: float | None = None) -> Key:
        if self.use_essentia:
            a = int(start * self.sr)
            b = len(self.mono) if end is None else int(end * self.sr)
            return estimate_essentia(self.mono[a:b])
        a = int(start * self.sr / self.hop)
        b = self.chroma.shape[1] if end is None else max(a + 1, int(end * self.sr / self.hop))
        return estimate_from_chroma(self.chroma[:, a:b].sum(axis=1))


def section_key(key: Key, melody: float) -> Key | None:
    """None when the section has no reliable key."""
    if key.confidence < MIN_SECTION_KEY_CONFIDENCE or melody < MIN_SECTION_MELODY:
        return None
    return key
