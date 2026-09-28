"""Pty binding tests: construction, cwd, read/write, lifecycle."""

import tempfile
import time

import pytest

from qtermx_gui import Pty


def test_default_shell_spawn():
    pty = Pty(24, 80)  # spawns $SHELL
    assert pty.pid() > 0
    assert pty.isRunning()
    pty.close()


def test_command_spawn():
    pty = Pty(["/bin/sh", "-c", "echo hello"], 24, 80)
    assert pty.pid() > 0
    pty.close()


def test_cwd():
    """The child must be chdir'd into cwd before exec."""
    with tempfile.TemporaryDirectory() as d:
        pty = Pty(["/bin/sh", "-c", "pwd"], 24, 80, cwd=d)
        time.sleep(0.3)
        out = pty.read() or ""
        assert d in out
        pty.close()


def test_read_write_roundtrip():
    pty = Pty(["/bin/sh", "-c", "cat"], 24, 80)
    pty.sendData("ping\r")
    time.sleep(0.3)
    data = ""
    while True:
        chunk = pty.read()
        if not chunk:
            break
        data += chunk
    assert "ping" in data
    pty.close()


def test_wait_reaps():
    pty = Pty(["/bin/sh", "-c", "exit 3"], 24, 80)
    status = None
    deadline = time.monotonic() + 5.0
    while status is None and time.monotonic() < deadline:
        status = pty.wait()
        time.sleep(0.05)
    assert status == 3
    assert not pty.isRunning()


def test_set_window_size():
    pty = Pty(["/bin/sh", "-c", "sleep 0.5"], 24, 80)
    pty.setWindowSize(40, 120)  # must not raise
    pty.close()


def test_foreground_program_exercised():
    """foregroundProgram() returns str or None (platform-dependent
    resolution) — just exercise it while a job runs."""
    pty = Pty(["/bin/sh", "-c", "sleep 0.5"], 24, 80)
    time.sleep(0.1)
    pty.foregroundProgram()
    pty.close()


def test_has_foreground_job_exercised():
    pty = Pty(["/bin/sh", "-c", "sleep 0.5"], 24, 80)
    time.sleep(0.1)
    pty.hasForegroundJob()  # bool, platform-dependent — must not raise
    pty.close()


def test_signal():
    pty = Pty(["/bin/sh", "-c", "sleep 5"], 24, 80)
    pty.signal(15)  # SIGTERM — must not raise
    pty.close()