"""Legacy entry point for current provided-artwork resource encoding.

Historical icon masters are retained as references. No app build/package.
"""
from pathlib import Path
import runpy

runpy.run_path(str(Path(__file__).with_name('encode-sukut-icons.py')), run_name='__main__')
