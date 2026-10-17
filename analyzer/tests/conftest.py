from __future__ import annotations

import importlib.util
import io
import json
import os
import sys
from dataclasses import dataclass
from pathlib import Path

import pytest

from djanalyze.pipeline import Options, analyze
from djanalyze.progress import Progress

REPO = Path(__file__).resolve().parents[2]


def _load_generator():
    spec = importlib.util.spec_from_file_location("test_audio_generate", REPO / "test-audio" / "generate.py")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module  # dataclasses look their module up here
    spec.loader.exec_module(module)
    return module


generate = _load_generator()


@dataclass
class AnalysedClip:
    reference: object  # generate.ClipReference
    json_path: Path
    document: dict
    progress_lines: list[dict]


@pytest.fixture(scope="session")
def clips(tmp_path_factory) -> list:
    return generate.render_all(tmp_path_factory.mktemp("clips"))


@pytest.fixture(scope="session")
def cache_dir(tmp_path_factory) -> Path:
    """Where the session's analysis files go.

    Set DJMIX_ANALYZER_OUTPUT to keep them, for example so the C++ tests can
    check the same files against the schema and loader.
    """
    override = os.environ.get("DJMIX_ANALYZER_OUTPUT")
    if override:
        path = Path(override)
        path.mkdir(parents=True, exist_ok=True)
        return path
    return tmp_path_factory.mktemp("cache")


@pytest.fixture(scope="session")
def analysed(clips, cache_dir) -> dict[str, AnalysedClip]:
    """Every test clip analysed once with the built-in engine, keyed by clip name."""
    results = {}
    for reference in clips:
        stream = io.StringIO()
        result = analyze(reference.path, cache_dir, Options(engine="builtin", force=True), Progress(stream))
        lines = [json.loads(line) for line in stream.getvalue().splitlines()]
        results[reference.name] = AnalysedClip(
            reference, result.json_path, json.loads(result.json_path.read_text()), lines
        )
    return results
