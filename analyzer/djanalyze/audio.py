"""File hashing and decoding."""

from __future__ import annotations

import hashlib
from dataclasses import dataclass
from pathlib import Path

import numpy as np
import soundfile as sf
import soxr

TARGET_SAMPLE_RATE = 44100


@dataclass(frozen=True)
class DecodedAudio:
    samples: np.ndarray  # shape (frames, 2), float32
    sample_rate: int

    @property
    def mono(self) -> np.ndarray:
        return self.samples.mean(axis=1)

    @property
    def duration(self) -> float:
        return len(self.samples) / self.sample_rate


def file_hash(path: Path) -> str:
    """SHA-256 of the file's bytes, as "sha256:<hex>"."""
    digest = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            digest.update(chunk)
    return "sha256:" + digest.hexdigest()


def cache_stem(hash_: str) -> str:
    """File name stem for a hash. Colons are not allowed in Windows file names."""
    return hash_.replace(":", "-")


def decode(path: Path) -> DecodedAudio:
    """Decode to 44.1 kHz stereo float32."""
    try:
        data, rate = sf.read(str(path), dtype="float32", always_2d=True)
    except sf.LibsndfileError:
        # Formats libsndfile cannot read go through librosa's audioread fallback.
        import librosa

        mono_or_multi, rate = librosa.load(str(path), sr=None, mono=False)
        data = np.atleast_2d(mono_or_multi).T.astype(np.float32)

    if data.shape[1] == 1:
        data = np.repeat(data, 2, axis=1)
    elif data.shape[1] > 2:
        data = data[:, :2]

    if rate != TARGET_SAMPLE_RATE:
        data = soxr.resample(data, rate, TARGET_SAMPLE_RATE, quality="HQ").astype(np.float32)

    if len(data) == 0:
        raise ValueError(f"{path} contains no audio")

    return DecodedAudio(np.ascontiguousarray(data), TARGET_SAMPLE_RATE)
