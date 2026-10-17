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

    # K-weight and square the audio a few seconds at a time, carrying the filter
    # state across, and keep only the energy of each 100 ms step. Blocks are
    # four steps (400 ms) with a step's hop (75% overlap).
    step = int(round(0.1 * sr))
    if len(samples) < 4 * step:
        return None

    filters = _k_weighting(sr)
    channels = samples.shape[1]
    states = [np.zeros((max(len(a), len(b)) - 1, channels)) for b, a in filters]
    chunk = step * 100  # 10 s
    step_energy = []
    carry = np.zeros((0, channels))
    for start in range(0, len(samples), chunk):
        x = samples[start : start + chunk].astype(np.float64)
        for i, (b, a) in enumerate(filters):
            x, states[i] = lfilter(b, a, x, axis=0, zi=states[i])
        x = np.concatenate([carry, x**2])
        whole = len(x) // step * step
        step_energy.append(x[:whole].reshape(-1, step, channels).sum(axis=1))
        carry = x[whole:]
    steps = np.concatenate(step_energy).sum(axis=1)  # channel weights are 1

    block_power = np.convolve(steps, np.ones(4), mode="valid") / (4 * step)

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
