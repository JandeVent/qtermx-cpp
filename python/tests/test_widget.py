"""TerminalWidget binding tests (Qt layer, offscreen platform).

Snapshots are delivered from the reader thread via
Qt::QueuedConnection (ADR-0005), so every test that waits for widget
state pumps the event loop.
"""

import time

import pytest
from PyQt6.QtGui import QColor, QFont

from qtermx_gui import Pty, Session, TerminalWidget

from conftest import wait_until


def test_construct_without_session(qapp):
    widget = TerminalWidget()
    assert widget.sizeHint().width() > 0
    widget.setFont(QFont("Menlo", 13))


def test_construct_with_session(qapp):
    pty = Pty(["/bin/sh", "-c", "exit 0"], 24, 80)
    session = Session(pty, 24, 80)
    widget = TerminalWidget(session)
    assert widget.gridLines() == 24
    assert widget.gridColumns() == 80
    session.close()


def test_snapshot_delivery(qapp):
    """Snapshots from the reader thread reach the widget via queued
    invocations — pump the event loop and observe the text."""
    pty = Pty(["/bin/sh", "-c", "echo widget-snapshot"], 24, 80)
    session = Session(pty, 24, 80)
    widget = TerminalWidget(session)
    session.start()
    assert wait_until(
        lambda: widget.hasText("widget-snapshot"),
        pump=qapp.processEvents,
    )
    session.close()


def test_set_font_and_palette(qapp):
    pty = Pty(["/bin/sh", "-c", "exit 0"], 24, 80)
    session = Session(pty, 24, 80)
    widget = TerminalWidget(session)
    widget.setFont(QFont("Menlo", 13))
    widget.setPalette(QColor("#ffffff"), QColor("#000000"))
    assert widget.cellW() > 0
    assert widget.cellH() > 0
    session.close()


def test_backing_image(qapp):
    pty = Pty(["/bin/sh", "-c", "exit 0"], 24, 80)
    session = Session(pty, 24, 80)
    widget = TerminalWidget(session)
    widget.resize(800, 600)
    qapp.processEvents()
    img = widget.backingImage()
    assert img.width() > 0
    assert img.height() > 0
    session.close()


def test_scrollback_and_viewport(qapp):
    pty = Pty(["/bin/sh", "-c", "exit 0"], 24, 80)
    session = Session(pty, 24, 80)
    widget = TerminalWidget(session)
    assert widget.scrollbackLen() >= 0
    assert widget.viewportOffset() >= 0
    session.close()


def test_mode_flags(qapp):
    pty = Pty(["/bin/sh", "-c", "exit 0"], 24, 80)
    session = Session(pty, 24, 80)
    widget = TerminalWidget(session)
    # All mode flags must be readable booleans
    for flag in (widget.decCkm, widget.bracketedPaste, widget.mouseEnabled,
                 widget.focusReport, widget.altScreen):
        assert isinstance(flag(), bool)
    session.close()