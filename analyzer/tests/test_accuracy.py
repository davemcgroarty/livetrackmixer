"""M1 acceptance: on the test clips, BPM within ±0.5 and downbeats within 30 ms."""

from __future__ import annotations

import numpy as np
import pytest

from conftest import generate

CLIP_NAMES = [clip["name"] for clip in generate.load_manifest()]
BPM_TOLERANCE = 0.5
DOWNBEAT_TOLERANCE = 0.030


@pytest.mark.parametrize("name", CLIP_NAMES)
def test_bpm(analysed, name):
    clip = analysed[name]
    assert clip.document["tempo"]["bpm"] == pytest.approx(clip.reference.bpm, abs=BPM_TOLERANCE)
    assert clip.document["tempo"]["variable"] is False


@pytest.mark.parametrize("name", CLIP_NAMES)
def test_downbeats(analysed, name):
    clip = analysed[name]
    expected = np.array(clip.reference.downbeats)
    found = np.array(clip.document["downbeats"])

    # Every reference downbeat has a detected one nearby...
    misses = np.abs(expected[:, None] - found[None, :]).min(axis=1)
    assert misses.max() <= DOWNBEAT_TOLERANCE, f"worst downbeat error {misses.max() * 1000:.1f} ms"

    # ...and no detected downbeat inside the music falls between them.
    bar = 240.0 / clip.reference.bpm
    inside = found[(found >= expected[0] - bar / 2) & (found <= expected[-1] + bar / 2)]
    extras = np.abs(inside[:, None] - expected[None, :]).min(axis=1)
    assert extras.max() <= DOWNBEAT_TOLERANCE


@pytest.mark.parametrize("name", CLIP_NAMES)
def test_bar_one_is_the_first_downbeat(analysed, name):
    clip = analysed[name]
    first_bar = clip.document["bars"][0]
    assert first_bar["n"] == 1
    assert first_bar["start"] == pytest.approx(clip.reference.downbeats[0], abs=DOWNBEAT_TOLERANCE)
