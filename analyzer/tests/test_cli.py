"""The command line contract with the app: files written, progress lines, caching, errors."""

from __future__ import annotations

import io
import json
import os
import shutil
import subprocess
import sys

import jsonschema
import numpy as np
import pytest
import soundfile

import djanalyze
from djanalyze import audio, cli, pipeline, schema, waveform
from djanalyze.progress import Progress
from conftest import REPO


def run_cli(*args: str) -> subprocess.CompletedProcess:
    return subprocess.run(
        [sys.executable, "-m", "djanalyze.cli", *args], capture_output=True, text=True, check=False, cwd=REPO
    )


@pytest.fixture(scope="module")
def short_clip(clips):
    # The shortest clip keeps the subprocess tests quick.
    return min(clips, key=lambda c: c.total_bars)


def test_analyze_writes_json_and_waveform(short_clip, tmp_path):
    result = run_cli("analyze", str(short_clip.path), "--out", str(tmp_path), "--engine", "builtin")
    assert result.returncode == 0, result.stdout + result.stderr

    lines = [json.loads(line) for line in result.stdout.splitlines()]
    assert lines[-1]["stage"] == "done"
    json_path = tmp_path / os.path.basename(lines[-1]["json"])
    assert json_path.exists()

    document = json.loads(json_path.read_text())
    schema.validate(document)
    assert json_path.name == document["file"]["hash"].replace(":", "-") + ".json"
    assert (tmp_path / document["waveformFile"]).exists()
    assert not list(tmp_path.glob("*.tmp")), "temp files are renamed or removed"


def test_progress_lines(analysed):
    for clip in analysed.values():
        lines = clip.progress_lines
        stages = [line["stage"] for line in lines]
        assert stages[0] == "hash"
        assert stages[-1] == "done"
        for expected in ("decode", "beats", "structure", "key", "descriptors", "waveform", "write"):
            assert expected in stages
        progress = [line["progress"] for line in lines]
        assert progress == sorted(progress), "progress never goes backwards"
        assert all(0.0 <= p <= 1.0 for p in progress)


def test_second_run_uses_the_cache(short_clip, tmp_path):
    first = run_cli("analyze", str(short_clip.path), "--out", str(tmp_path), "--engine", "builtin")
    assert first.returncode == 0
    json_path = next(tmp_path.glob("*.json"))
    mtime = json_path.stat().st_mtime_ns

    second = run_cli("analyze", str(short_clip.path), "--out", str(tmp_path), "--engine", "builtin")
    assert second.returncode == 0
    assert json.loads(second.stdout.splitlines()[-1])["stage"] == "cached"
    assert json_path.stat().st_mtime_ns == mtime


def test_cache_is_ignored_after_an_analyzer_upgrade(tmp_path):
    json_path, waveform_path = tmp_path / "a.json", tmp_path / "a.wfm"
    waveform_path.write_bytes(b"")
    json_path.write_text(json.dumps({"schemaVersion": 1, "analyzerVersion": djanalyze.__version__}))
    assert pipeline.is_cached(json_path, waveform_path)

    json_path.write_text(json.dumps({"schemaVersion": 1, "analyzerVersion": "0.0.1"}))
    assert not pipeline.is_cached(json_path, waveform_path)

    json_path.write_text("{ truncated")
    assert not pipeline.is_cached(json_path, waveform_path)


def test_cache_needs_the_waveform_too(tmp_path):
    json_path = tmp_path / "a.json"
    json_path.write_text(json.dumps({"schemaVersion": 1, "analyzerVersion": djanalyze.__version__}))
    assert not pipeline.is_cached(json_path, tmp_path / "a.wfm")


def test_missing_file_is_an_error_line(tmp_path, capsys):
    assert cli.main(["analyze", str(tmp_path / "nope.wav"), "--out", str(tmp_path)]) == 2
    line = json.loads(capsys.readouterr().out.splitlines()[-1])
    assert line["stage"] == "error"
    assert "nope.wav" in line["message"]


def test_undecodable_file_is_an_error_line(tmp_path, capsys):
    bad = tmp_path / "bad.wav"
    bad.write_bytes(b"not audio at all")
    assert cli.main(["analyze", str(bad), "--out", str(tmp_path / "cache"), "--engine", "builtin"]) == 1
    assert json.loads(capsys.readouterr().out.splitlines()[-1])["stage"] == "error"
    assert not list((tmp_path / "cache").glob("*.json"))


def test_waveform_matches_the_audio(analysed):
    clip = next(iter(analysed.values()))
    overview = waveform.read(clip.json_path.parent / clip.document["waveformFile"])
    assert overview.sample_rate == clip.document["file"]["sampleRate"]
    assert overview.samples_per_point == 256
    expected_points = clip.document["file"]["durationSec"] * overview.sample_rate / 256
    assert len(overview.peak) == pytest.approx(expected_points, abs=1)
    assert (overview.rms <= overview.peak + 1e-6).all()
    assert 0.5 < overview.peak.max() <= 1.0


def test_schema_contract(analysed):
    for clip in analysed.values():
        schema.validate(clip.document)

    example = json.loads((REPO / "schemas" / "examples" / "analysis.v1.example.json").read_text())
    schema.validate(example)

    labelled = json.loads(json.dumps(example))
    labelled["sections"][0]["label"] = "chorus"
    with pytest.raises(jsonschema.ValidationError):
        schema.validate(labelled)


needs_ffmpeg = pytest.mark.skipif(shutil.which("ffmpeg") is None, reason="ffmpeg is not installed")


@needs_ffmpeg
@pytest.mark.parametrize("codec, extension", [("aac", "m4a"), ("alac", "m4a")])
def test_formats_libsndfile_cannot_read_go_through_ffmpeg(clips, tmp_path, codec, extension):
    reference = min(clips, key=lambda c: c.total_bars)
    converted = tmp_path / f"{reference.name}-{codec}.{extension}"
    subprocess.run(
        ["ffmpeg", "-nostdin", "-v", "error", "-i", str(reference.path), "-c:a", codec, str(converted)], check=True
    )
    with pytest.raises(soundfile.LibsndfileError):
        soundfile.info(str(converted))

    result = pipeline.analyze(converted, tmp_path / "cache", pipeline.Options(engine="builtin"), Progress(io.StringIO()))
    document = json.loads(result.json_path.read_text())
    assert document["tempo"]["bpm"] == pytest.approx(reference.bpm, abs=0.5)
    found = np.array(document["downbeats"])
    misses = np.abs(np.array(reference.downbeats)[:, None] - found[None, :]).min(axis=1)
    assert misses.max() <= 0.030


def test_unreadable_format_without_ffmpeg_names_ffmpeg(tmp_path, monkeypatch):
    fake = tmp_path / "track.m4a"
    fake.write_bytes(b"\x00\x00\x00\x20ftypM4A " + bytes(64))
    monkeypatch.setattr(audio.shutil, "which", lambda _: None)
    with pytest.raises(ValueError, match="ffmpeg"):
        audio.decode(fake)
