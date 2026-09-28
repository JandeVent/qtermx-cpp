"""Shared fixtures for the qtermx_gui binding tests.

The extension lands in <repo>/python/build/qtermx_gui/ after sip-build,
or is pip-installed into the environment. Set QTERMX_GUI_PATH to point
at a specific location (e.g. the installed site-packages copy).

Run from the python/ directory:

    python -m pytest tests -v
"""

import os
import sys
import time
from pathlib import Path

_env = os.environ.get("QTERMX_GUI_PATH")
if _env:
    sys.path.insert(0, _env)
else:
    _build = Path(__file__).resolve().parent.parent / "build" / "qtermx_gui"
    if _build.is_dir():
        sys.path.insert(0, str(_build))

# Widget tests must not open a window.
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")

import pytest
from PyQt6.QtWidgets import QApplication


@pytest.fixture(scope="session")
def qapp():
    """One offscreen QApplication for the whole suite."""
    app = QApplication.instance() or QApplication([])
    yield app


def wait_until(predicate, timeout=5.0, interval=0.02, pump=None):
    """Poll `predicate` until it is true or `timeout` seconds pass.

    `pump` is an optional callable invoked each iteration (e.g. a
    QApplication.processEvents bound method) — required whenever the
    result depends on queued events (widget snapshots are delivered via
    Qt::QueuedConnection, ADR-0005).
    """
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate():
            return True
        if pump is not None:
            pump()
        time.sleep(interval)
    return predicate()