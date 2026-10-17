"""Integrated loudness (ITU-R BS.1770-4 / EBU R128), in LUFS.

Uses Essentia's LoudnessEBUR128 when installed; the built-in version below
follows the same standard: K-weighting, 400 ms blocks with 75% overlap, an
absolute gate at -70 LUFS and a relative gate 10 LU below the ungated mean.
"""

from __future__ import annotations

import numpy as np
from scipy.signal import lfilter


def _k_weighting(sr: int) -> list[tuple[np.ndarray, np.ndarray]]:
    """The two BS.1770 pre-filters (high shelf, then high pass) for any sample rate."""
    # Stage 1: high shelf.
    f0, gain_db, q = 1681.974450955533, 3.999843853973347, 0.7071752369554196
    k = np.tan(np.pi * f0 / sr)
    vh = 10 ** (gain_db / 20)
    vb = vh**0.4996667741545416
    a0 = 1 + k / q + k * k
    shelf_b = np.array([(vh + vb * k / q + k * k) / a0, 2 * (k * k - vh) / a0, (vh - vb * k / q + k * k) / a0])
    shelf_a = np.array([1.0, 2 * (k * k - 1) / a0, (1 - k / q + k * k) / a0])

    # Stage 2: high pass (RLB).
    f0, q = 38.13547087602444, 0.5003270373238773
    k = np.tan(np.pi * f0 / sr)
    a0 = 1 + k / q + k * k
    hp_b = np.array([1.0, -2.0, 1.0])
    hp_a = np.array([1.0, 2 * (k * k - 1) / a0, (1 - k / q + k * k) / a0])
    return [(shelf_b, shelf_a), (hp_b, hp_a)]


def integrated_lufs(samples: np.ndarray, sr: int) -> float | None:
    """samples: (frames, channels). Returns None for silence or audio under 400 ms."""
    try:
        import essentia.standard as es

        _, _, integrated, _ = es.LoudnessEBUR128(sampleRate=sr)(samples.astype(np.float32))
        return float(integrated)
    except Exception:
        pass

    x = samples.astype(np.float64)
    for b, a in _k_weighting(sr):
        x = lfilter(b, a, x, axis=0)

    block = int(round(0.4 * sr))
    step = int(round(0.1 * sr))
    if len(x) < block:
        return None

    # Mean square per channel per block, summed over channels (L/R weights are 1).
    squares = np.cumsum(np.concatenate([np.zeros((1, x.shape[1])), x**2]), axis=0)
    starts = np.arange(0, len(x) - block + 1, step)
    block_power = ((squares[starts + block] - squares[starts]) / block).sum(axis=1)

    def loudness(power: np.ndarray) -> np.ndarray:
        return -0.691 + 10 * np.log10(np.maximum(power, 1e-20))

    gated = block_power[loudness(block_power) > -70.0]
    if len(gated) == 0:
        return None
    relative_gate = loudness(np.mean(gated)) - 10.0
    gated = gated[loudness(gated) > relative_gate]
    if len(gated) == 0:
        return None
    return float(loudness(np.mean(gated)))
