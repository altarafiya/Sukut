"""Compatibility entry point for the current approved cross-platform artwork."""
from pathlib import Path
import runpy
runpy.run_path(str(Path(__file__).resolve().parents[2]/"tools/export-branding.py"),run_name="__main__")
