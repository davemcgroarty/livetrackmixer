"""File hashing and decoding.

libsndfile (through soundfile) reads WAV, FLAC, AIFF, MP3, OGG and a few more.
Anything else, such as M4A/AAC or ALAC, is decoded by ffmpeg when it is on PATH.
"""

from __future__ import annotations

import hashlib
import shutil
import subprocess
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
    except sf.LibsndfileError as e:
        data, rate = _decode_with_ffmpeg(path, e)

    if data.shape[1] == 1:
        data = np.repeat(data, 2, axis=1)
    elif data.shape[1] > 2:
        data = data[:, :2]

    if rate != TARGET_SAMPLE_RATE:
        data = soxr.resample(data, rate, TARGET_SAMPLE_RATE, quality="HQ").astype(np.float32)

    if len(data) == 0:
        raise ValueError(f"{path} contains no audio")

    return DecodedAudio(np.ascontiguousarray(data), TARGET_SAMPLE_RATE)


def _decode_with_ffmpeg(path: Path, libsndfile_error: Exception) -> tuple[np.ndarray, int]:
    """Decode with ffmpeg to 44.1 kHz stereo float32. ffmpeg does the resampling and channel mix."""
    ffmpeg = shutil.which("ffmpeg")
    if ffmpeg is None:
        raise ValueError(f"cannot decode {path}: {libsndfile_error}; installing ffmpeg adds M4A/AAC, ALAC and more")

    result = subprocess.run(
        [ffmpeg, "-nostdin", "-v", "error", "-i", str(path), "-map", "0:a:0", "-vn",
         "-f", "f32le", "-acodec", "pcm_f32le", "-ac", "2", "-ar", str(TARGET_SAMPLE_RATE), "-"],
        capture_output=True,
        check=False,
    )  # fmt: skip
    if result.returncode != 0:
        message = result.stderr.decode(errors="replace").strip().splitlines()
        raise ValueError(f"ffmpeg cannot decode {path}: {message[-1] if message else 'unknown error'}")

    data = np.frombuffer(result.stdout, dtype="<f4")
    return data[: len(data) - len(data) % 2].reshape(-1, 2), TARGET_SAMPLE_RATE
