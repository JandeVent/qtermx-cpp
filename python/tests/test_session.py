"""Session binding tests: lifecycle, headless process(), screen access."""

import time

import pytest

from qtermx_gui import Pty, Session


def test_process_headless():
    """process() drives the pipeline synchronously — no thread."""
    pty = Pty(["/bin/sh", "-c", "exit 0"], 24, 80)
    session = Session(pty, 24, 80)
    session.process("Hello from qtermx!\r\n$ ")
    assert "Hello from qtermx!" in session.screen().render()
    session.close()


def test_start_close_lifecycle():
    pty = Pty(["/bin/sh", "-c", "echo hi"], 24, 80)
    session = Session(pty, 24, 80)
    assert not session.isAlive()
    session.start()
    assert session.isAlive()
    session.close()
    assert not session.isAlive()


def test_close_is_idempotent():
    pty = Pty(["/bin/sh", "-c", "echo hi"], 24, 80)
    session = Session(pty, 24, 80)
    session.start()
    session.close()
    session.close()  # must not raise


def test_threaded_output_reaches_screen():
    pty = Pty(["/bin/sh", "-c", "echo threaded-output"], 24, 80)
    session = Session(pty, 24, 80)
    session.start()
    deadline = time.monotonic() + 5.0
    while "threaded-output" not in session.screen().render() and time.monotonic() < deadline:
        time.sleep(0.05)
    assert "threaded-output" in session.screen().render()
    session.close()


def test_send_data_through_thread():
    pty = Pty(["/bin/sh", "-c", "cat"], 24, 80)
    session = Session(pty, 24, 80)
    session.start()
    session.sendData("echo-me\r")
    deadline = time.monotonic() + 5.0
    while "echo-me" not in session.screen().render() and time.monotonic() < deadline:
        time.sleep(0.05)
    assert "echo-me" in session.screen().render()
    session.close()


def test_resize():
    # `cat` keeps the child alive so the reader loop keeps draining
    # commands (the loop exits when the child exits).
    pty = Pty(["/bin/sh", "-c", "cat"], 24, 80)
    session = Session(pty, 24, 80)
    session.start()
    session.resize(40, 120)
    deadline = time.monotonic() + 5.0
    while session.screen().lines != 40 and time.monotonic() < deadline:
        time.sleep(0.05)
    assert session.screen().lines == 40
    assert session.screen().columns == 120
    session.close()


def test_scroll_and_palette():
    pty = Pty(["/bin/sh", "-c", "exit 0"], 24, 80)
    session = Session(pty, 24, 80)
    session.scroll(3)
    session.scrollToBottom()
    session.setPalette("#ffffff", "#000000")  # must not raise
    session.close()