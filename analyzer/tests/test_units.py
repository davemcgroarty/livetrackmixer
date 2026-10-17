"""Unit tests for the pieces that do not need audio files."""

from __future__ import annotations

import numpy as np
import pytest

from djanalyze import beats, key, loudness, structure, waveform
from djanalyze.beats import BeatEstimate


# --------------------------------------------------------------------- beats


def test_fit_grid_recovers_tempo_through_jitter_and_a_missed_beat():
    rng = np.random.default_rng(0)
    period = 60 / 126
    times = 0.3 + period * np.arange(200) + rng.normal(0, 0.004, 200)
    times = np.delete(times, 50)
    fitted_period, offset, deviation = beats.fit_grid(times)
    assert 60 / fitted_period == pytest.approx(126, abs=0.05)
    assert offset == pytest.approx(0.3, abs=0.005)
    assert deviation < 0.02


def test_constant_grid_starts_at_the_first_beat_after_zero():
    grid = beats.constant_grid(0.5, 1.3, 3.0)
    assert grid == pytest.approx([0.3, 0.8, 1.3, 1.8, 2.3, 2.8])


def test_downbeat_indices_use_the_majority_phase():
    grid = np.arange(16) * 0.5
    # Downbeats on beat phase 1, one of them misplaced.
    estimated = np.array([0.5, 2.5, 4.5, 5.0])
    assert list(beats.downbeat_indices_from(estimated, grid)) == [1, 5, 9, 13]


def test_downbeat_disagreement():
    a = np.array([1.0, 3.0, 5.0, 7.0])
    assert beats.downbeat_disagreement(a, a + 0.02) == 0.0
    assert beats.downbeat_disagreement(a, np.array([1.0, 3.0, 5.5, 7.5])) == 0.5
    assert beats.downbeat_disagreement(a, np.array([])) == 1.0


def _estimate(downbeat_phase: int, source: str) -> BeatEstimate:
    b = 0.25 + 0.5 * np.arange(64)
    return BeatEstimate(beats=b, downbeats=b[downbeat_phase::4], source=source)


def test_cross_check_lowers_confidence_when_downbeats_disagree():
    sr = 8000
    mono = np.zeros(sr * 33)
    duration = len(mono) / sr
    agree = beats.build_grid(mono, sr, duration, _estimate(0, "a"), _estimate(0, "b"))
    disagree = beats.build_grid(mono, sr, duration, _estimate(0, "a"), _estimate(1, "b"))
    assert agree.confidence > 0.8
    assert disagree.confidence <= beats.LOW_BEAT_CONFIDENCE
    assert disagree.sources == ["a", "b"]


def test_tempo_folding():
    fast = np.arange(0, 30, 60 / 250)
    assert 60 / np.median(np.diff(beats._fold_tempo(fast))) == pytest.approx(125)
    slow = np.arange(0, 30, 60 / 62)
    assert 60 / np.median(np.diff(beats._fold_tempo(slow))) == pytest.approx(124)


# ----------------------------------------------------------------- structure


def _bars(n: int) -> list[structure.Bar]:
    return [structure.Bar(n=i + 1, start=2.0 * i, first_beat=4 * i) for i in range(n)]


def test_phrases_are_8_bars_or_16_in_long_sections():
    sections = [
        structure.Section(start_bar=1, length_bars=12, start=0, end=24),
        structure.Section(start_bar=13, length_bars=32, start=24, end=88),
    ]
    phrases = structure.phrases_for(sections)
    assert [(p.start_bar, p.length_bars, p.section) for p in phrases] == [
        (1, 8, 0),
        (9, 4, 0),
        (13, 16, 1),
        (29, 16, 1),
    ]
    assert [p.n for p in phrases] == [1, 2, 3, 4]


def test_sections_from_boundaries():
    bars = _bars(20)
    beat_times = np.arange(80) * 0.5
    sections = structure.sections_from_boundaries([9, 1, 25], bars, beat_times, 40.0)
    assert [(s.start_bar, s.length_bars) for s in sections] == [(1, 8), (9, 12)]
    assert sections[0].end == sections[1].start == 16.0
    assert sections[1].end == 40.0


def test_short_sections_are_dropped():
    assert structure.drop_short_sections([3, 9, 11, 30], total_bars=32) == [9]


def test_snap_times_to_bars():
    assert structure.snap_times_to_bars([0.1, 7.9, 8.6], _bars(10)) == [1, 5]


def test_novelty_finds_a_change():
    rng = np.random.default_rng(1)
    first = rng.normal(0, 0.1, (10, 16)) + np.linspace(1, 2, 10)[:, None]
    second = rng.normal(0, 0.1, (10, 16)) + np.linspace(2, 1, 10)[:, None]
    starts, confidence = structure.novelty_boundaries(np.concatenate([first, second], axis=1))
    assert starts == [17]
    assert confidence > 0.5


# ----------------------------------------------------------------------- key


@pytest.mark.parametrize(
    "tonic, minor, name, camelot",
    [
        (9, True, "A minor", "8A"),
        (0, False, "C major", "8B"),
        (6, True, "F# minor", "11A"),
        (1, False, "Db major", "3B"),
        (3, True, "Eb minor", "2A"),
        (11, False, "B major", "1B"),
        (8, True, "G# minor", "1A"),
        (5, False, "F major", "7B"),
    ],
)
def test_camelot_codes(tonic, minor, name, camelot):
    k = key.Key(tonic, minor, 1.0)
    assert k.name == name
    assert k.camelot == camelot


def test_key_from_essentia_names():
    assert key.key_from_name("Bb", "minor", 0.8).camelot == "3A"
    assert key.key_from_name("C#", "major", 1.4).confidence == 1.0


def test_chroma_profile_of_a_key_is_found():
    profile = np.roll(key.PROFILE_MINOR, 9)  # A minor
    found = key.estimate_from_chroma(profile)
    assert found.camelot == "8A"
    assert found.confidence > 0.8


def test_flat_chroma_has_no_confident_key():
    assert key.estimate_from_chroma(np.ones(12)).confidence < 0.2
    assert key.estimate_from_chroma(np.zeros(12)).confidence == 0.0


def test_section_key_needs_confidence_and_melody():
    k = key.Key(9, True, 0.9)
    assert key.section_key(k, melody=0.5) is k
    assert key.section_key(k, melody=0.05) is None
    assert key.section_key(key.Key(9, True, 0.2), melody=0.5) is None


# ------------------------------------------------------------------ loudness


def test_loudness_of_a_reference_sine():
    """EBU Tech 3341: a 1 kHz stereo sine at -23 dBFS per channel reads -23 LUFS."""
    sr = 48000
    t = np.arange(sr * 10) / sr
    sine = 10 ** (-23 / 20) * np.sin(2 * np.pi * 1000 * t)
    assert loudness.integrated_lufs(np.stack([sine, sine], axis=1), sr) == pytest.approx(-23.0, abs=0.1)


def test_loudness_of_silence():
    assert loudness.integrated_lufs(np.zeros((44100, 2)), 44100) is None
    assert loudness.integrated_lufs(np.zeros((100, 2)), 44100) is None


# ------------------------------------------------------------------ waveform


def test_waveform_round_trip(tmp_path):
    signal = np.concatenate([np.full(256, 0.5), -np.ones(256), np.full(10, 0.25)]).astype(np.float32)
    overview = waveform.compute(signal, 44100)
    assert overview.peak == pytest.approx([0.5, 1.0, 0.25])
    assert overview.rms == pytest.approx([0.5, 1.0, 0.25])

    path = tmp_path / "x.wfm"
    waveform.write(path, overview)
    assert path.stat().st_size == 32 + 3 * 2 * 4
    again = waveform.read(path)
    assert again.peak == pytest.approx(overview.peak)
    assert again.rms == pytest.approx(overview.rms)
    assert again.sample_rate == 44100


def test_waveform_rejects_other_files(tmp_path):
    path = tmp_path / "x.wfm"
    path.write_bytes(b"RIFF" + bytes(40))
    with pytest.raises(ValueError):
        waveform.read(path)
