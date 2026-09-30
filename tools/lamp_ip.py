#!/usr/bin/env python3
"""Print the lamp's current IP address (for dashboard.bat)."""
import contextlib
import io
import sys

from healthcheck import discover

with contextlib.redirect_stdout(io.StringIO()):
    ip = discover("b9-c9-fc")

if ip:
    print(ip)
    sys.exit(0)
sys.exit(1)
