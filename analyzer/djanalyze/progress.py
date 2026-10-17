"""JSON progress lines on stdout, one object per line, for the app's progress bar.

Each line looks like {"stage": "beats", "progress": 0.4}, where progress is the
overall fraction of the run done (0 to 1), not the fraction of that stage.
"""

from __future__ import annotations

import json
import sys
from typing import Any, TextIO

# Share of the whole run each stage takes, in pipeline order.
STAGE_WEIGHTS = {
    "hash": 0.02,
    "decode": 0.08,
    "beats": 0.30,
    "structure": 0.10,
    "descriptors": 0.25,
    "key": 0.15,
    "waveform": 0.05,
    "write": 0.05,
}


class Progress:
    def __init__(self, stream: TextIO | None = None) -> None:
        self.stream = stream if stream is not None else sys.stdout

    def emit(self, stage: str, progress: float, **extra: Any) -> None:
        line = {"stage": stage, "progress": round(min(max(progress, 0.0), 1.0), 3), **extra}
        self.stream.write(json.dumps(line) + "\n")
        self.stream.flush()

    def stage(self, stage: str, within: float = 0.0) -> None:
        """Report that `stage` is `within` (0 to 1) of the way through."""
        before = 0.0
        for name, weight in STAGE_WEIGHTS.items():
            if name == stage:
                self.emit(stage, before + weight * within)
                return
            before += weight
        raise KeyError(stage)
