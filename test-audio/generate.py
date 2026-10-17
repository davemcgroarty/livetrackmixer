"""Render the synthetic test clips described in clips.json.

Every clip is built from simple synthesised drums, bass and chords, so its tempo,
downbeats, section boundaries and key are known exactly. The output is
deterministic (fixed random seed), and since nothing is sampled from recordings
the clips are CC0 like the rest of this directory.

    python test-audio/generate.py [--out test-audio/clips]

The analyzer tests call render_all() themselves, so running this script is only
needed to listen to the clips.
"""

from __future__ import annotations

import argparse
import dataclasses
import json
from dataclasses import dataclass
from pathlib import Path

import numpy as np
import soundfile as sf

SAMPLE_RATE = 44100
BEATS_PER_BAR = 4
TAIL_SECONDS = 1.0
MANIFEST = Path(__file__).with_name("clips.json")

FORMATS = {
    "wav": ("WAV", "PCM_16"),
    "flac": ("FLAC", "PCM_16"),
    "aiff": ("AIFF", "PCM_16"),
    "mp3": ("MP3", "MPEG_LAYER_III"),
}


@dataclass(frozen=True)
class ClipReference:
    """What the analyzer should find in a rendered clip."""

    name: str
    path: Path
    bpm: float
    downbeats: list[float]
    section_start_bars: list[int]  # 1-based
    total_bars: int
    key_name: str
    camelot: str
    sections: list[dict]


def load_manifest() -> list[dict]:
    return json.loads(MANIFEST.read_text())["clips"]


def _envelope(n: int, attack: float, decay: float) -> np.ndarray:
    t = np.arange(n) / SAMPLE_RATE
    env = np.exp(-t / decay)
    a = max(1, int(attack * SAMPLE_RATE))
    env[:a] *= np.linspace(0.0, 1.0, a)
    return env


def _kick() -> np.ndarray:
    n = int(0.35 * SAMPLE_RATE)
    t = np.arange(n) / SAMPLE_RATE
    freq = 45.0 + 75.0 * np.exp(-t / 0.03)
    phase = 2 * np.pi * np.cumsum(freq) / SAMPLE_RATE
    click = np.exp(-t / 0.002) * 0.3
    return (np.sin(phase) * _envelope(n, 0.001, 0.12) + click) * 0.9


def _noise_burst(rng: np.random.Generator, length: float, decay: float, highpass: float) -> np.ndarray:
    n = int(length * SAMPLE_RATE)
    noise = rng.standard_normal(n)
    spectrum = np.fft.rfft(noise)
    freqs = np.fft.rfftfreq(n, 1 / SAMPLE_RATE)
    spectrum[freqs < highpass] = 0
    filtered = np.fft.irfft(spectrum, n)
    filtered /= np.max(np.abs(filtered)) + 1e-9
    return filtered * _envelope(n, 0.0005, decay)


def _tone(freq: float, length: float, harmonics: int, attack: float, decay: float) -> np.ndarray:
    n = int(length * SAMPLE_RATE)
    t = np.arange(n) / SAMPLE_RATE
    wave = sum(np.sin(2 * np.pi * freq * h * t) / h for h in range(1, harmonics + 1))
    env = _envelope(n, attack, decay)
    release = min(n, int(0.02 * SAMPLE_RATE))
    env[-release:] *= np.linspace(1.0, 0.0, release)
    return wave * env


def _midi_to_hz(note: float) -> float:
    return 440.0 * 2 ** ((note - 69) / 12)


def _chord_notes(root: int, quality: str) -> list[int]:
    third = 3 if quality == "minor" else 4
    base = 60 + ((root - 60) % 12)  # root between C4 and B4
    if base > 66:
        base -= 12
    return [base, base + third, base + 7, base + 12]


def _add(buffer: np.ndarray, sound: np.ndarray, at: float, gain: float) -> None:
    start = int(round(at * SAMPLE_RATE))
    if start >= len(buffer):
        return
    end = min(len(buffer), start + len(sound))
    buffer[start:end] += sound[: end - start] * gain


def render(clip: dict) -> tuple[np.ndarray, ClipReference]:
    rng = np.random.default_rng(1234)
    bpm = float(clip["bpm"])
    beat = 60.0 / bpm
    bar = beat * BEATS_PER_BAR
    first = float(clip["firstDownbeat"])
    total_bars = sum(s["bars"] for s in clip["sections"])
    length = first + total_bars * bar + TAIL_SECONDS
    out = np.zeros(int(length * SAMPLE_RATE))

    kick = _kick()
    clap = _noise_burst(rng, 0.2, 0.05, 1200.0)
    snare = _noise_burst(rng, 0.25, 0.08, 400.0) + _tone(190.0, 0.25, 1, 0.001, 0.05) * 0.5
    hat = _noise_burst(rng, 0.06, 0.015, 7000.0)
    crash = _noise_burst(rng, 2.5, 0.8, 4000.0)

    progression = clip["progression"]
    bar_index = 0
    section_start_bars = []
    for section in clip["sections"]:
        parts = set(section["parts"])
        section_start_bars.append(bar_index + 1)
        _add(out, crash, first + bar_index * bar, 0.25)
        for _ in range(section["bars"]):
            bar_start = first + bar_index * bar
            root, quality = progression[bar_index % len(progression)]
            if "chords" in parts:
                for note in _chord_notes(root, quality):
                    _add(out, _tone(_midi_to_hz(note), bar, 6, 0.01, 3.0), bar_start, 0.07)
            for b in range(BEATS_PER_BAR):
                t = bar_start + b * beat
                if "kick" in parts and (b % 2 == 0 or "snare" not in parts):
                    _add(out, kick, t, 0.8)
                if "clap" in parts and b % 2 == 1:
                    _add(out, clap, t, 0.35)
                if "snare" in parts and b % 2 == 1:
                    _add(out, snare, t, 0.4)
                if "hats" in parts:
                    _add(out, hat, t + beat / 2, 0.18)
                    _add(out, hat, t, 0.08)
                if "bass" in parts:
                    bass_note = _midi_to_hz(36 + (root % 12))
                    _add(out, _tone(bass_note, beat * 0.45, 4, 0.005, 0.2), t + beat / 2, 0.8)
            bar_index += 1

    out /= np.max(np.abs(out)) / 0.89  # about -1 dBFS peak
    stereo = np.stack([out, out], axis=1).astype(np.float32)

    reference = ClipReference(
        name=clip["name"],
        path=Path(),
        bpm=bpm,
        downbeats=[first + i * bar for i in range(total_bars)],
        section_start_bars=section_start_bars,
        total_bars=total_bars,
        key_name=clip["key"]["name"],
        camelot=clip["key"]["camelot"],
        sections=clip["sections"],
    )
    return stereo, reference


def render_all(out_dir: Path) -> list[ClipReference]:
    """Render every clip into out_dir and return the references."""
    out_dir.mkdir(parents=True, exist_ok=True)
    references = []
    for clip in load_manifest():
        audio, reference = render(clip)
        file_format, subtype = FORMATS[clip["format"]]
        path = out_dir / f"{clip['name']}.{clip['format']}"
        sf.write(path, audio, SAMPLE_RATE, format=file_format, subtype=subtype)
        references.append(dataclasses.replace(reference, path=path))
    return references


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--out", type=Path, default=Path(__file__).with_name("clips"))
    args = parser.parse_args()
    for reference in render_all(args.out):
        print(f"{reference.path}  {reference.bpm} BPM  {reference.total_bars} bars  {reference.key_name}")


if __name__ == "__main__":
    main()
