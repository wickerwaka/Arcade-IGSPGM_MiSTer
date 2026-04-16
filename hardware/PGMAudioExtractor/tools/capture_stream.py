#!/usr/bin/env /usr/bin/python3
import runpy
from pathlib import Path


if __name__ == '__main__':
    script = Path(__file__).resolve().parents[3] / 'utils' / 'capture_stream.py'
    runpy.run_path(str(script), run_name='__main__')
