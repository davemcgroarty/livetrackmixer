"""Waveform overview files (.wfm). The format is described in docs/wfm-format.md."""

from __future__ import annotations

import os
import struct
import tempfile
from dataclasses import dataclass
from pathlib import Path

import numpy as np

MAGIC = b"DJWF"
FORMAT_VERSION = 1
SAMPLES_PER_POINT = 256
HEADER = struct.Struct("<4sHHIIQQ")  # 32 bytes


@dataclass(frozen=True)
class Overview:
    sample_rate: int
    samples_per_point: int
    peak: np.ndarray
    rms: np.ndarray


def compute(mono: np.ndarray, sample_rate: int, samples_per_point: int = SAMPLES_PER_POINT) -> Overview:
    points = int(np.ceil(len(mono) / samples_per_point))
    padded = np.zeros(points * samples_per_point, dtype=np.float32)
    padded[: len(mono)] = mono
    blocks = padded.reshape(points, samples_per_point)
    # The last block's RMS counts only its real samples.
    counts = np.full(points, samples_per_point, dtype=np.float64)
    counts[-1] = len(mono) - (points - 1) * samples_per_point
    peak = np.abs(blocks).max(axis=1)
    rms = np.sqrt(np.einsum("ij,ij->i", blocks, blocks, dtype=np.float64) / counts)
    return Overview(sample_rate, samples_per_point, peak.astype(np.float32), rms.astype(np.float32))


def write(path: Path, overview: Overview) -> None:
    """Write atomically: a temp file in the same directory, then rename."""
    header = HEADER.pack(MAGIC, FORMAT_VERSION, 1, overview.sample_rate, overview.samples_per_point, len(overview.peak), 0)
    body = np.empty(len(overview.peak) * 2, dtype="<f4")
    body[0::2] = overview.peak
    body[1::2] = overview.rms
    fd, tmp = tempfile.mkstemp(dir=path.parent, prefix=path.name, suffix=".tmp")
    try:
        with os.fdopen(fd, "wb") as f:
            f.write(header)
            f.write(body.tobytes())
        os.replace(tmp, path)
    except BaseException:
        Path(tmp).unlink(missing_ok=True)
        raise


def read(path: Path) -> Overview:
    data = path.read_bytes()
    magic, version, channels, rate, per_point, points, _ = HEADER.unpack_from(data)
    if magic != MAGIC or version != FORMAT_VERSION or channels != 1:
        raise ValueError(f"{path} is not a version {FORMAT_VERSION} waveform file")
    body = np.frombuffer(data, dtype="<f4", offset=HEADER.size)
    if len(body) != points * 2:
        raise ValueError(f"{path} is truncated")
    return Overview(rate, per_point, body[0::2].copy(), body[1::2].copy())
