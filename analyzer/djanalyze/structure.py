"""Bars, sections and phrases.

Sections are boundaries only. allin1 segment times are snapped to bars and its
labels discarded; without allin1 a bar-level novelty curve finds the boundaries.
"""

from __future__ import annotations

from dataclasses import dataclass

import numpy as np

from .beats import BEATS_PER_BAR, BeatGrid

PHRASE_BARS = 8
LONG_PHRASE_BARS = 16
# Sections at least this long, and a whole number of long phrases, use 16-bar phrases.
LONG_SECTION_BARS = 32
MIN_SECTION_BARS = 4
NOVELTY_KERNEL_BARS = 4  # half-width of the checkerboard kernel


@dataclass(frozen=True)
class Bar:
    n: int  # 1-based
    start: float
    first_beat: int  # 0-based beat index


@dataclass(frozen=True)
class Section:
    start_bar: int  # 1-based
    length_bars: int
    start: float
    end: float


@dataclass(frozen=True)
class Phrase:
    n: int
    start_bar: int
    length_bars: int
    section: int  # 0-based


def bars_from_grid(grid: BeatGrid) -> list[Bar]:
    return [
        Bar(n=i + 1, start=float(grid.beats[b]), first_beat=int(b)) for i, b in enumerate(grid.downbeat_indices)
    ]


def bar_end(bars: list[Bar], index: int, beats: np.ndarray, duration: float) -> float:
    """End time of bars[index] (0-based): the next bar's start, or a bar's length later."""
    if index + 1 < len(bars):
        return bars[index + 1].start
    period = float(np.median(np.diff(beats))) if len(beats) > 1 else 0.5
    return min(duration, bars[index].start + BEATS_PER_BAR * period)


def sections_from_boundaries(
    boundary_bars: list[int], bars: list[Bar], beats: np.ndarray, duration: float
) -> list[Section]:
    """Sections from 1-based start bars. Bar 1 always starts a section."""
    starts = sorted({1, *[b for b in boundary_bars if 1 < b <= len(bars)]})
    sections = []
    for i, start in enumerate(starts):
        next_start = starts[i + 1] if i + 1 < len(starts) else len(bars) + 1
        sections.append(
            Section(
                start_bar=start,
                length_bars=next_start - start,
                start=bars[start - 1].start,
                end=bar_end(bars, next_start - 2, beats, duration),
            )
        )
    return sections


def snap_times_to_bars(times: list[float], bars: list[Bar]) -> list[int]:
    """Nearest 1-based bar number for each time."""
    if not bars:
        return []
    starts = np.array([b.start for b in bars])
    return sorted({int(np.abs(starts - t).argmin()) + 1 for t in times})


def drop_short_sections(boundary_bars: list[int], total_bars: int, minimum: int = MIN_SECTION_BARS) -> list[int]:
    """Remove boundaries that would leave a section shorter than `minimum` bars."""
    kept: list[int] = []
    previous = 1
    for b in sorted(set(boundary_bars)):
        if b - previous >= minimum and total_bars + 1 - b >= minimum:
            kept.append(b)
            previous = b
    return kept


def novelty_boundaries(bar_features: np.ndarray, kernel: int = NOVELTY_KERNEL_BARS) -> tuple[list[int], float]:
    """Section starts from a bar-by-bar feature matrix (features × bars).

    Foote novelty on a cosine self-similarity matrix. Returns 1-based start bars
    (excluding bar 1) and a 0–1 confidence from how clearly the peaks stand out.
    """
    n = bar_features.shape[1]
    if n < 2 * kernel + 1:
        return [], 0.0

    features = bar_features - bar_features.mean(axis=1, keepdims=True)
    features /= features.std(axis=1, keepdims=True) + 1e-9
    unit = features / (np.linalg.norm(features, axis=0, keepdims=True) + 1e-9)
    ssm = unit.T @ unit

    sign = np.ones((2 * kernel, 2 * kernel))
    sign[:kernel, kernel:] = -1
    sign[kernel:, :kernel] = -1
    padded = np.pad(ssm, kernel, mode="edge")
    novelty = np.zeros(n)
    for i in range(n):
        # Boundary at the start of bar i: kernel centred between bars i-1 and i.
        window = padded[i : i + 2 * kernel, i : i + 2 * kernel]
        novelty[i] = np.sum(window * sign)
    novelty[0] = 0.0
    novelty = np.maximum(novelty, 0.0)
    if novelty.max() <= 0:
        return [], 0.0
    novelty /= novelty.max()

    threshold = max(0.3, float(np.mean(novelty) + 0.5 * np.std(novelty)))
    peaks = [
        i
        for i in range(1, n)
        if novelty[i] >= threshold
        and novelty[i] == novelty[max(0, i - MIN_SECTION_BARS + 1) : i + MIN_SECTION_BARS].max()
    ]
    starts = [p + 1 for p in peaks]

    if peaks:
        others = np.delete(novelty, peaks)
        background = float(np.median(others)) if len(others) else 0.0
        confidence = float(np.clip(np.mean(novelty[peaks]) - background, 0.0, 1.0))
    else:
        confidence = 0.3
    return starts, confidence


def phrases_for(sections: list[Section]) -> list[Phrase]:
    """8-bar phrases (16 in long sections), restarting at every section boundary."""
    phrases = []
    for index, section in enumerate(sections):
        length = (
            LONG_PHRASE_BARS
            if section.length_bars >= LONG_SECTION_BARS and section.length_bars % LONG_PHRASE_BARS == 0
            else PHRASE_BARS
        )
        bar = section.start_bar
        end = section.start_bar + section.length_bars
        while bar < end:
            phrases.append(Phrase(n=len(phrases) + 1, start_bar=bar, length_bars=min(length, end - bar), section=index))
            bar += length
    return phrases


def allin1_section_confidence(boundary_bars: list[int]) -> float:
    """Share of boundaries that fall on a 4-bar grid from bar 1: well-placed sections usually do."""
    if not boundary_bars:
        return 0.5
    on_grid = [(b - 1) % 4 == 0 for b in boundary_bars]
    return float(0.5 + 0.4 * np.mean(on_grid))
