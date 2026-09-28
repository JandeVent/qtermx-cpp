"""Screen model binding tests (headless, no Qt)."""

import pytest

from qtermx_gui import Screen, extend, isRgb, point, rgb


def test_render():
    s = Screen(24, 80)
    s.print("Hello, world!")
    assert "Hello, world!" in s.render()


def test_cursor():
    s = Screen(24, 80)
    s.print("ab")
    assert s.cursor.x == 2
    s.carriageReturn()
    assert s.cursor.x == 0
    s.lineFeed()
    assert s.cursor.y == 1


def test_take_dirty_rows():
    s = Screen(24, 80)
    s.print("x")
    dirty = s.takeDirtyRows()
    assert isinstance(dirty, set)
    assert 0 in dirty
    assert s.takeDirtyRows() == set()  # consumed


def test_resize():
    s = Screen(24, 80)
    s.resize(40, 120)
    assert s.lines == 40
    assert s.columns == 120


def test_modes_and_rendition():
    s = Screen(24, 80)
    s.setBold()
    s.setFg(rgb(255, 100, 50))
    assert isRgb(rgb(1, 2, 3))
    s.resetRendition()


def test_selection_free_functions():
    sel = point(1, 2)
    assert sel.row1 == 1 and sel.col1 == 2
    ext = extend(0, 0, 3, 4)
    assert ext.row2 == 3 and ext.col2 == 4


def test_scrollback():
    s = Screen(24, 80, scrollbackLimit=100)
    for i in range(30):
        s.print(f"line {i}")
        s.lineFeed()
    assert s.scrollbackLen() > 0
    s.scrollToBottom()
    s.clearScrollback()
    assert s.scrollbackLen() == 0


def test_erase_and_motion():
    s = Screen(24, 80)
    s.print("hello")
    s.cursorBackward(2)
    s.eraseInLine(0)
    assert s.render().startswith("hel")


def test_alt_screen():
    s = Screen(24, 80)
    s.print("main")
    s.enterAltScreen()
    s.print("alt")
    assert "alt" in s.render()
    s.leaveAltScreen()
    assert "main" in s.render()