#!/usr/bin/env python3
"""
qtermx_gui — SIP/PyQt6 bindings demo.

Shows the compiled extension driving the C++ terminal pipeline:

  1. Screen model    — direct grid access + SGR rendition through SIP
  2. Pty + Session   — bytes -> parser -> emulator -> screen (real child)
  3. TerminalWidget  — Qt rendering of a live session, screenshotted
"""

import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "build", "qtermx_gui"))

from PyQt6.QtWidgets import QApplication
from PyQt6.QtGui import QFont

import qtermx_gui
from qtermx_gui import Pty, Session, Screen, TerminalWidget, rgb, isRgb


def banner(text):
    print()
    print("=" * 72)
    print(text)
    print("=" * 72)


def part1_screen_model():
    banner("1. Screen model — direct C++ grid access through SIP")
    s = Screen(6, 46)
    s.print("plain ")
    s.setBold()
    s.setFg(rgb(255, 80, 80))
    s.print("bold-red ")
    s.resetRendition()
    s.setUnderline()
    s.setFg(4)
    s.print("underline-blue")
    s.carriageReturn()
    s.lineFeed()
    s.setReverse()
    s.print("reverse")
    s.resetRendition()
    s.print(" ")
    s.setFg(rgb(255, 165, 0))
    s.print("truecolor-orange")
    s.carriageReturn()
    s.lineFeed()
    s.print("box: \u250c\u2500\u252c\u2500\u2510 \u2588\u2588\u2588\u2588\u2591\u2591")
    print(s.render())

    line0 = s.line(0)
    colored = line0.cells[7]  # inside "bold-red "
    print(f"cell(0,7) fg={colored.fg} isRgb={isRgb(colored.fg)} bold={colored.bold}")
    print(f"cursor=({s.cursor.x},{s.cursor.y})  dirty={s.takeDirtyRows()} (consumed)")


def part2_session():
    banner("2. Pty + Session — bytes -> parser -> emulator -> screen")
    pty = Pty([
        "/bin/sh", "-c",
        "printf '\\033[1;32m\u2713 parsed through the C++ pipeline\\033[0m\\r\\n'; "
        "printf '\\033[33mTERM\\033[0m=%s \\033[33mCOLUMNS\\033[0m=%s "
        "\\033[33mLINES\\033[0m=%s\\r\\n' \"$TERM\" \"$COLUMNS\" \"$LINES\"; "
        "uname -sm",
    ], 6, 50)
    session = Session(pty, 6, 50)
    session.start()

    deadline = time.time() + 3.0
    while time.time() < deadline and pty.isRunning():
        time.sleep(0.05)
    time.sleep(0.2)

    print(session.screen().render())
    session.close()
    print("session closed cleanly (reader thread joined)")


def part3_widget():
    banner("3. TerminalWidget — Qt rendering of a live session")
    pty = Pty([
        "/bin/sh", "-c",
        "printf '\\033[1;36m\u256d\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500"
        "\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500"
        "\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u256e\\033[0m\\r\\n'; "
        "printf '\\033[1;36m\u2502\\033[0m  \\033[1;32mqtermx-cpp\\033[0m \u00b7 "
        "\\033[1;33mSIP/PyQt6\\033[0m bindings  \\033[1;36m\u2502\\033[0m\\r\\n'; "
        "printf '\\033[1;36m\u2570\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500"
        "\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500"
        "\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u256f\\033[0m\\r\\n'; "
        "printf '\\033[7m reverse \\033[0m \\033[4munderline\\033[0m "
        "\\033[38;2;255;128;0mtruecolor\\033[0m\\r\\n'; "
        "printf '\\033[31mred \\033[32mgreen \\033[33myellow \\033[34mblue "
        "\\033[35mmagenta \\033[36mcyan\\033[0m\\r\\n'; "
        "printf '\\033[2mdim\\033[0m \\033[9mstrike\\033[0m "
        "\u2588\u2593\u2592\u2591 \\u2581\u2582\u2583\u2584\u2585\u2586\u2587\u2588\\r\\n'",
    ], 8, 40)
    session = Session(pty, 8, 40)
    session.start()

    widget = TerminalWidget(session)
    widget.setFont(QFont("Menlo", 14))
    widget.resize(600, 220)
    widget.show()

    deadline = time.time() + 3.0
    while time.time() < deadline and pty.isRunning():
        app.processEvents()
        time.sleep(0.05)
    for _ in range(10):
        app.processEvents()
        time.sleep(0.05)

    out = os.path.join(HERE, "demo.png")
    widget.grab().save(out)
    print(f"widget grid: {widget.gridLines()}x{widget.gridColumns()}  "
          f"cell: {widget.cellW():.1f}x{widget.cellH():.1f}  "
          f"backing image: {widget.backingWidth()}x{widget.backingHeight()}")
    print(f"screenshot saved: {out}")
    session.close()


if __name__ == "__main__":
    print(f"loaded extension: {qtermx_gui.__file__}")
    app = QApplication(sys.argv)
    part1_screen_model()
    part2_session()
    part3_widget()
    banner("demo complete")