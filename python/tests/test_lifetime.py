"""C++ <-> Python object lifetime and memory management tests.

These are the tests that matter most for the binding: SIP ownership
semantics decide whether a GC'd Python wrapper deletes the C++ object
under a running reader thread (use-after-free) or leaks it.

Ownership contract (see the .sip files):

  Pty            — Python-owned (created in Python; GC deletes the C++
                   object). The reader thread dereferences it on every
                   read, so it must outlive the session.
  Session        — Python-owned; /KeepReference/ pins its Pty for the
                   session's lifetime.
  TerminalWidget — /KeepReference/ pins its Session (the widget touches
                   the raw Session* in key events, resize, destructor).
  session.pty    — borrowed property: the same wrapper as the ctor
                   argument (SIP object map), never owns.
  session.screen() — borrowed view into the session, never owns.
  Cell/Row/Cursor/Selection/Snapshot — value copies, independent.
"""

import gc
import weakref

import pytest
from PyQt6.QtGui import QColor, QFont

from qtermx_gui import Cell, Cursor, Pty, Row, Screen, Selection, Session, Snapshot, TerminalWidget


def _collect():
    gc.collect()
    gc.collect()


# ── Wrapper identity ────────────────────────────────────────────────

def test_session_pty_is_same_wrapper():
    """session.pty must be the very object passed to the constructor
    (pyqtermx parity) — the SIP object map returns the existing wrapper
    for a borrowed pointer."""
    pty = Pty(["/bin/sh", "-c", "exit 0"], 24, 80)
    session = Session(pty, 24, 80)
    assert session.pty is pty
    assert session.pty is session.pty
    assert session.pty.pid() == pty.pid()


# ── /KeepReference/ pins ────────────────────────────────────────────

def test_keep_reference_pins_pty():
    """Dropping the last Python Pty reference must NOT delete the C++
    Pty while the session lives — the reader thread would dereference a
    dangling PtyLike* (use-after-free). /KeepReference/ pins the Python
    wrapper itself, so it survives until the session dies."""
    pty = Pty(["/bin/sh", "-c", "sleep 0.2; echo done"], 24, 80)
    session = Session(pty, 24, 80)
    session.start()
    ref = weakref.ref(pty)
    del pty
    _collect()
    # The wrapper is pinned by the session's /KeepReference/ — alive,
    # and it is the very object session.pty hands back.
    assert ref() is not None
    assert session.pty is ref()
    assert session.pty.pid() > 0
    assert session.pty.isRunning() or session.pty.wait() is not None
    session.close()
    del session
    _collect()
    assert ref() is None  # freed once the session is gone


def test_keep_reference_releases_on_session_death():
    """Once the session is gone, the pin is released and the Pty is
    freed with its wrapper — no leak."""
    pty = Pty(["/bin/sh", "-c", "exit 0"], 24, 80)
    session = Session(pty, 24, 80)
    pty_ref = weakref.ref(pty)
    session_ref = weakref.ref(session)
    del pty, session
    _collect()
    assert pty_ref() is None
    assert session_ref() is None


def test_widget_pins_session(qapp):
    """Dropping the last Python Session reference must NOT delete the
    C++ Session while the widget lives — the widget touches the raw
    Session* in its destructor and on every key event. /KeepReference/
    pins the Python wrapper itself, so it survives until the widget
    dies."""
    pty = Pty(["/bin/sh", "-c", "exit 0"], 24, 80)
    session = Session(pty, 24, 80)
    widget = TerminalWidget(session)
    session_ref = weakref.ref(session)
    del session
    _collect()
    # Pinned by the widget's /KeepReference/ — still alive.
    assert session_ref() is not None
    # The widget still works; gridLines/gridColumns come from the
    # session's screen, so this proves the C++ Session is alive.
    assert widget.gridLines() == 24
    assert widget.gridColumns() == 80
    widget.setFont(QFont("Menlo", 13))
    widget.setPalette(QColor("#ffffff"), QColor("#000000"))
    assert widget.sizeHint().width() > 0
    assert widget.gridLines() > 0
    assert widget.gridColumns() > 0
    del widget
    _collect()
    assert session_ref() is None  # freed once the widget is gone


# ── Borrowed returns never own ──────────────────────────────────────

def test_borrowed_pty_does_not_own():
    """session.pty is borrowed: dropping the borrowed wrapper must not
    delete the C++ Pty (the session still owns it)."""
    pty = Pty(["/bin/sh", "-c", "exit 0"], 24, 80)
    session = Session(pty, 24, 80)
    borrowed = session.pty
    del borrowed
    _collect()
    assert session.pty.pid() == pty.pid()
    session.close()


def test_borrowed_screen_does_not_own():
    """session.screen() is a borrowed view: dropping the wrapper must
    not delete the session's screen."""
    pty = Pty(["/bin/sh", "-c", "exit 0"], 24, 80)
    session = Session(pty, 24, 80)
    screen = session.screen()
    del screen
    _collect()
    assert session.screen().lines == 24
    session.close()


def test_borrowed_wrapper_gc_after_owner_death():
    """A borrowed wrapper held past its owner's death is dangling, but
    GC of the wrapper itself must not crash (it never deletes the C++
    object). Accessing it afterwards is undefined — we only exercise
    the collection path."""
    pty = Pty(["/bin/sh", "-c", "exit 0"], 24, 80)
    session = Session(pty, 24, 80)
    screen = session.screen()
    del session
    _collect()
    del screen
    _collect()


# ── Teardown orders ─────────────────────────────────────────────────

def test_widget_then_session_teardown(qapp):
    """Widget destroyed before the session: the destructor detaches the
    snapshot callback from a still-alive session."""
    pty = Pty(["/bin/sh", "-c", "exit 0"], 24, 80)
    session = Session(pty, 24, 80)
    widget = TerminalWidget(session)
    del widget
    _collect()
    session.close()  # still fully usable
    del session
    _collect()


def test_session_then_widget_teardown(qapp):
    """Session wrapper dropped before the widget: /KeepReference/ keeps
    the C++ Session alive until the widget dies (the widget destructor
    calls setSnapshotCallback on it)."""
    pty = Pty(["/bin/sh", "-c", "exit 0"], 24, 80)
    session = Session(pty, 24, 80)
    widget = TerminalWidget(session)
    del session
    _collect()
    del widget
    _collect()  # must not crash


def test_xside_close_scenario(qapp):
    """The exact plugin.py teardown: session.close() while the widget
    still references it, then all wrappers dropped."""
    pty = Pty(["/bin/sh", "-c", "exit 0"], 24, 80)
    session = Session(pty, 24, 80)
    widget = TerminalWidget(session)
    session.start()
    session.close()  # reader thread joined, pty closed
    assert not session.isAlive()
    del pty, session, widget
    _collect()


def test_no_double_delete(qapp):
    """The full pty+session+widget graph must tear down without
    double-deleting any C++ object (SIP ownership is unambiguous)."""
    pty = Pty(["/bin/sh", "-c", "exit 0"], 24, 80)
    session = Session(pty, 24, 80)
    widget = TerminalWidget(session)
    _ = session.pty      # borrowed wrapper floating around
    _ = session.screen()  # borrowed wrapper floating around
    del pty, session, widget
    _collect()


# ── Value types are copies ──────────────────────────────────────────

def test_value_types_are_copies():
    """Cell/Row/Cursor/Selection/Snapshot are value types: Python-side
    copies are independent of their C++ source. (Note: `s.cursor` and
    `s.line(y)` are live references into the screen — see
    test_line_is_live_reference.)"""
    # Cursor: fresh instances are independent
    c = Cursor()
    c.x = 99
    assert Cursor().x != 99

    # Cell: blank() returns a fresh copy
    cell = Cell.blank()
    cell.data = "X"
    assert Cell.blank().data != "X"

    # Selection: fresh instances are independent
    sel = Selection()
    sel.row1 = 3
    assert Selection().row1 != 3

    # Snapshot.rows: the vector conversion copies each Row
    snap = Snapshot()
    snap.rows = [Row()]
    row_copy = snap.rows[0]
    row_copy.cells = [Cell.blank()]
    row_copy.cells[0].data = "X"
    assert snap.rows[0].cells == []  # the stored copy is untouched


def test_line_is_live_reference():
    """Screen.line() returns a live reference (Row& semantics): the
    wrapper reflects later screen changes. `s.cursor` behaves the same
    way (a live view of the member)."""
    s = Screen(24, 80)
    s.print("hello")
    row = s.line(0)
    s.print("X")  # appends at the cursor (col 5), same row
    assert row.cells[5].data == "X"

    c = s.cursor
    s.setCursor(7, 3)
    assert c.x == 7 and c.y == 3