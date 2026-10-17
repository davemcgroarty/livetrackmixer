"""Command line: djanalyze analyze <audio-file> --out <cache-dir> [--stems]."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from . import __version__
from .pipeline import Options, analyze
from .progress import Progress


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="djanalyze", description="Analyse audio files for the DJ Mixer app.")
    parser.add_argument("--version", action="version", version=f"djanalyze {__version__}")
    commands = parser.add_subparsers(dest="command", required=True)

    run = commands.add_parser("analyze", help="analyse one audio file into <cache-dir>/<hash>.json and .wfm")
    run.add_argument("audio_file", type=Path)
    run.add_argument("--out", type=Path, required=True, metavar="CACHE_DIR")
    run.add_argument("--stems", action="store_true", help="separate stems (v1.1; not implemented yet)")
    run.add_argument("--force", action="store_true", help="re-analyse even if a current cache entry exists")
    run.add_argument(
        "--engine",
        choices=("auto", "builtin"),
        default="auto",
        help="auto: use allin1, Beat This! and Essentia when installed (default); builtin: librosa only",
    )
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    progress = Progress()

    if not args.audio_file.is_file():
        progress.emit("error", 0.0, message=f"no such file: {args.audio_file}")
        return 2
    if args.stems:
        print("djanalyze: --stems is planned for v1.1 and is ignored", file=sys.stderr)

    try:
        analyze(args.audio_file, args.out, Options(engine=args.engine, force=args.force), progress)
    except Exception as e:  # reported to the app as a progress line, not a traceback
        progress.emit("error", 0.0, message=f"{type(e).__name__}: {e}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
