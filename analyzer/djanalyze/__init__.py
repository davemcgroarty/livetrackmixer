"""djanalyze: offline track analysis for the DJ Mixer app.

The app runs ``djanalyze analyze <audio-file> --out <cache-dir>`` as a
subprocess and reads back one JSON file (schemas/analysis.v1.schema.json) and
one .wfm waveform file per track.
"""

__version__ = "0.1.2"
SCHEMA_VERSION = 1
