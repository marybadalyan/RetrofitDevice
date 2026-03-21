#!/bin/bash
PYTHONPATH="$(dirname "$0")/.pkgs-hub" \
/opt/homebrew/Cellar/python@3.14/3.14.0_1/Frameworks/Python.framework/Versions/3.14/Resources/Python.app/Contents/MacOS/Python \
-m uvicorn thermohub:app --host 0.0.0.0 --port 5000
