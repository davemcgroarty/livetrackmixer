"""The analysis pipeline: one audio file in, one JSON file and one .wfm file out."""

from __future__ import annotations

import json
import os
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import numpy as np

from . import SCHEMA_VERSION, __version__, audio, beats, descriptors, features, key, loudness, schema, structure, waveform
from .progress import Progress


@dataclass(frozen=True)
class Options:
    # "auto" uses allin1 / Beat This! / Essentia when installed; "builtin" never does.
    engine: str = "auto"
    force: bool = False


@dataclass(frozen=True)
class Result:
    json_path: Path
    waveform_path: Path
    cached: bool


def output_paths(cache_dir: Path, file_hash: str) -> tuple[Path, Path]:
    stem = audio.cache_stem(file_hash)
    return cache_dir / f"{stem}.json", cache_dir / f"{stem}.wfm"


def is_cached(json_path: Path, waveform_path: Path) -> bool:
    """A cache entry counts only if it was written by this analyzer version."""
    if not (json_path.exists() and waveform_path.exists()):
        return False
    try:
        existing = json.loads(json_path.read_text())
    except (OSError, ValueError):
        return False
    return existing.get("analyzerVersion") == __version__ and existing.get("schemaVersion") == SCHEMA_VERSION


def write_json_atomically(path: Path, document: dict[str, Any]) -> None:
    fd, tmp = tempfile.mkstemp(dir=path.parent, prefix=path.name, suffix=".tmp")
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as f:
            json.dump(document, f, indent=1)
            f.write("\n")
        os.replace(tmp, path)
    except BaseException:
        Path(tmp).unlink(missing_ok=True)
        raise


def _round(values: Any, digits: int = 4) -> Any:
    if isinstance(values, (list, tuple, np.ndarray)):
        return [_round(v, digits) for v in values]
    return round(float(values), digits)


def analyze(audio_path: Path, cache_dir: Path, options: Options = Options(), progress: Progress | None = None) -> Result:
    progress = progress or Progress()
    cache_dir.mkdir(parents=True, exist_ok=True)

    progress.stage("hash")
    file_hash = audio.file_hash(audio_path)
    json_path, waveform_path = output_paths(cache_dir, file_hash)
    if not options.force and is_cached(json_path, waveform_path):
        progress.emit("cached", 1.0, json=str(json_path))
        return Result(json_path, waveform_path, cached=True)

    progress.stage("decode")
    decoded = audio.decode(audio_path)
    mono = decoded.mono
    sr = decoded.sample_rate
    duration = decoded.duration

    progress.stage("beats")
    use_models = options.engine == "auto"
    if use_models and beats.allin1_available():
        primary = beats.estimate_allin1(audio_path)
    else:
        primary = beats.estimate_builtin(mono, sr)
    progress.stage("beats", 0.6)
    secondary = beats.estimate_beat_this(audio_path) if use_models and beats.beat_this_available() else None
    grid = beats.build_grid(mono, sr, duration, primary, secondary)

    progress.stage("structure")
    spectral = features.compute(mono, sr)
    bars = structure.bars_from_grid(grid)
    bar_spans = [(b.start, structure.bar_end(bars, i, grid.beats, duration)) for i, b in enumerate(bars)]
    if grid.segments:
        boundaries = structure.snap_times_to_bars(grid.segments, bars)
        boundaries = structure.drop_short_sections(boundaries, len(bars), minimum=1)
        section_confidence = structure.allin1_section_confidence(boundaries)
    else:
        boundaries, section_confidence = structure.novelty_boundaries(features.bar_feature_matrix(spectral, bar_spans))
        boundaries = structure.drop_short_sections(boundaries, len(bars))
    sections = structure.sections_from_boundaries(boundaries, bars, grid.beats, duration)
    phrases = structure.phrases_for(sections)

    progress.stage("descriptors")
    bar_scores = descriptors.compute(spectral, bar_spans, grid.beats)
    section_bars = [(s.start_bar, s.length_bars) for s in sections]
    section_scores = {
        name: descriptors.per_section(getattr(bar_scores, name), section_bars)
        for name in ("rhythm", "bass", "high", "melody")
    }

    progress.stage("key")
    estimator = key.KeyEstimator(mono, sr, spectral.harmonic, features.HOP, use_models and key.essentia_available())
    overall_key = estimator.estimate()
    section_keys = [
        key.section_key(estimator.estimate(s.start, s.end), section_scores["melody"][i]) for i, s in enumerate(sections)
    ]
    lufs = loudness.integrated_lufs(decoded.samples, sr)

    progress.stage("waveform")
    waveform.write(waveform_path, waveform.compute(mono, sr))

    progress.stage("write")
    document = {
        "schemaVersion": SCHEMA_VERSION,
        "analyzerVersion": __version__,
        "file": {"hash": file_hash, "durationSec": round(duration, 4), "sampleRate": sr},
        "tempo": {"bpm": round(grid.bpm, 3), "variable": bool(grid.variable)},
        "key": {"name": overall_key.name, "camelot": overall_key.camelot, "confidence": round(overall_key.confidence, 3)},
        "loudness": {"integratedLufs": None if lufs is None else round(lufs, 2)},
        "beats": _round(grid.beats),
        "downbeats": _round(grid.downbeats),
        "bars": [{"n": b.n, "start": round(b.start, 4), "firstBeat": b.first_beat} for b in bars],
        "phrases": [
            {"n": p.n, "startBar": p.start_bar, "lengthBars": p.length_bars, "section": p.section} for p in phrases
        ],
        "sections": [
            {
                "start": round(s.start, 4),
                "end": round(s.end, 4),
                "startBar": s.start_bar,
                "lengthBars": s.length_bars,
                "key": None
                if k is None
                else {"name": k.name, "camelot": k.camelot, "confidence": round(k.confidence, 3)},
            }
            for s, k in zip(sections, section_keys)
        ],
        "energyPerBar": _round(bar_scores.energy, 3),
        "descriptors": {
            "perBar": {
                "rhythm": _round(bar_scores.rhythm, 3),
                "bass": _round(bar_scores.bass, 3),
                "high": _round(bar_scores.high, 3),
                "melody": _round(bar_scores.melody, 3),
                "raw": {name: _round(values, 3) for name, values in bar_scores.raw.items()},
            },
            "perSection": [
                {"section": i, **{name: round(section_scores[name][i], 3) for name in section_scores}}
                for i in range(len(sections))
            ],
        },
        "confidence": {"beats": round(grid.confidence, 3), "sections": round(section_confidence, 3)},
        "waveformFile": waveform_path.name,
        "stems": None,
    }
    schema.validate(document)
    write_json_atomically(json_path, document)  # last, so a JSON file always has its .wfm
    progress.emit("done", 1.0, json=str(json_path), sources=grid.sources)
    return Result(json_path, waveform_path, cached=False)
