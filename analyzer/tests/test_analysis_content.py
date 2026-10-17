"""Structure, key and descriptors on the test clips, against what each clip was built from."""

from __future__ import annotations

import numpy as np
import pytest

from conftest import generate

CLIP_NAMES = [clip["name"] for clip in generate.load_manifest()]


def section_parts(clip) -> list[set[str]]:
    return [set(s["parts"]) for s in clip.reference.sections]


@pytest.mark.parametrize("name", CLIP_NAMES)
def test_section_boundaries(analysed, name):
    clip = analysed[name]
    starts = [s["startBar"] for s in clip.document["sections"]]
    assert starts == clip.reference.section_start_bars


@pytest.mark.parametrize("name", CLIP_NAMES)
def test_sections_tile_the_bars(analysed, name):
    doc = analysed[name].document
    sections = doc["sections"]
    assert sections[0]["startBar"] == 1
    for a, b in zip(sections, sections[1:]):
        assert a["startBar"] + a["lengthBars"] == b["startBar"]
        assert a["end"] == pytest.approx(b["start"])
    assert sections[-1]["startBar"] + sections[-1]["lengthBars"] - 1 == len(doc["bars"])


@pytest.mark.parametrize("name", CLIP_NAMES)
def test_phrases_restart_at_sections(analysed, name):
    doc = analysed[name].document
    phrase_starts = {p["startBar"] for p in doc["phrases"]}
    for section in doc["sections"]:
        assert section["startBar"] in phrase_starts
    for phrase in doc["phrases"]:
        section = doc["sections"][phrase["section"]]
        assert section["startBar"] <= phrase["startBar"]
        assert phrase["startBar"] + phrase["lengthBars"] <= section["startBar"] + section["lengthBars"]
        assert phrase["lengthBars"] <= 16
    assert [p["n"] for p in doc["phrases"]] == list(range(1, len(doc["phrases"]) + 1))


@pytest.mark.parametrize("name", CLIP_NAMES)
def test_overall_key(analysed, name):
    clip = analysed[name]
    assert clip.document["key"]["camelot"] == clip.reference.camelot
    assert clip.document["key"]["name"] == clip.reference.key_name


@pytest.mark.parametrize("name", CLIP_NAMES)
def test_section_keys(analysed, name):
    clip = analysed[name]
    for parts, section in zip(section_parts(clip), clip.document["sections"]):
        if "chords" not in parts:
            assert section["key"] is None, "a section without chords has no reliable key"
        elif section["key"] is not None:
            assert section["key"]["camelot"] == clip.reference.camelot


def test_melodic_sections_mostly_have_a_key(analysed):
    with_chords = [
        section["key"] is not None
        for clip in analysed.values()
        for parts, section in zip(section_parts(clip), clip.document["sections"])
        if "chords" in parts
    ]
    assert np.mean(with_chords) >= 0.8


@pytest.mark.parametrize(
    "quality, part",
    [("bass", "kick"), ("high", "hats"), ("melody", "chords"), ("rhythm", "kick")],
)
def test_descriptors_follow_the_parts(analysed, quality, part):
    """Sections with a part score higher on its quality than sections without it, in the same clip."""
    compared = 0
    for clip in analysed.values():
        scores = [s[quality] for s in clip.document["descriptors"]["perSection"]]
        has = [scores[i] for i, parts in enumerate(section_parts(clip)) if part in parts]
        lacks = [scores[i] for i, parts in enumerate(section_parts(clip)) if part not in parts]
        if has and lacks:
            assert min(has) > max(lacks), f"{clip.reference.name}: {quality} {has} vs {lacks}"
            compared += 1
    assert compared > 0


def test_bassline_adds_bass(analysed):
    clip = analysed["house_124_a_minor"]
    scores = [s["bass"] for s in clip.document["descriptors"]["perSection"]]
    # Section 0: kick only; section 1: kick and bassline.
    assert scores[1] > scores[0]


@pytest.mark.parametrize("name", CLIP_NAMES)
def test_per_bar_arrays_match_bars(analysed, name):
    doc = analysed[name].document
    bars = len(doc["bars"])
    assert len(doc["energyPerBar"]) == bars
    for values in doc["descriptors"]["perBar"].values():
        if isinstance(values, list):
            assert len(values) == bars
    for values in doc["descriptors"]["perBar"]["raw"].values():
        assert len(values) == bars
    assert [s["section"] for s in doc["descriptors"]["perSection"]] == list(range(len(doc["sections"])))


@pytest.mark.parametrize("name", CLIP_NAMES)
def test_loudness_is_plausible(analysed, name):
    lufs = analysed[name].document["loudness"]["integratedLufs"]
    assert -30.0 < lufs < -5.0
