"""Validation against schemas/analysis.v1.schema.json, the contract with the app."""

from __future__ import annotations

import json
import os
from functools import lru_cache
from pathlib import Path
from typing import Any

SCHEMA_FILE = "analysis.v1.schema.json"


def schema_dir() -> Path | None:
    """The repo's schemas/ directory: $DJANALYZE_SCHEMA_DIR, or found above this package."""
    override = os.environ.get("DJANALYZE_SCHEMA_DIR")
    if override:
        return Path(override)
    for parent in Path(__file__).resolve().parents:
        candidate = parent / "schemas"
        if (candidate / SCHEMA_FILE).exists():
            return candidate
    return None


@lru_cache(maxsize=1)
def analysis_schema() -> dict[str, Any] | None:
    directory = schema_dir()
    if directory is None:
        return None
    return json.loads((directory / SCHEMA_FILE).read_text())


def validate(document: dict[str, Any]) -> None:
    """Raise jsonschema.ValidationError if `document` breaks the schema.

    Does nothing when the schema cannot be found (an install outside the repo).
    """
    import jsonschema

    schema = analysis_schema()
    if schema is None:
        return
    jsonschema.validate(document, schema, cls=jsonschema.Draft202012Validator)
