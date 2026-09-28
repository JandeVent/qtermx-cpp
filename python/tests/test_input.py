"""InputEncoder binding tests (static, pure)."""

import pytest
from PyQt6.QtCore import Qt
from PyQt6.QtGui import QKeyEvent

from qtermx_gui import InputEncoder


def test_encode_arrow_key():
    data = InputEncoder.encodeArrowKey(Qt.Key.Key_Up)
    assert data == b"\x1b[A"


def test_encode_arrow_key_dec_ckm():
    data = InputEncoder.encodeArrowKey(Qt.Key.Key_Up, decCkm=True)
    assert data == b"\x1bOA"


def test_encode_paste():
    data = InputEncoder.encodePaste("hello")
    assert b"hello" in data


def test_encode_key_returns_bytes_or_none():
    event = QKeyEvent(QKeyEvent.Type.KeyPress, Qt.Key.Key_A,
                      Qt.KeyboardModifier.NoModifier)
    result = InputEncoder.encodeKey(event)
    assert result is None or isinstance(result, bytes)


def test_mouse_encoders():
    # The binding returns QByteArray (PyQt6 type) for these — bytes()
    # converts it.
    data = bytes(InputEncoder.encodeSgrMouse(10, 5, 0, InputEncoder.MouseAction.Press))
    assert isinstance(data, bytes) and len(data) > 0
    data = bytes(InputEncoder.encodeMouseX10(10, 5, 0, InputEncoder.MouseAction.Press))
    assert isinstance(data, bytes) and len(data) > 0