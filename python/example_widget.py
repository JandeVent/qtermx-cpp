#!/usr/bin/env python3
"""
Qt widget terminal via the qtermx_gui bindings.
"""

import os
import sys

# The extension lands in <repo>/python/build/qtermx_gui/.
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "build", "qtermx_gui"))

from PyQt6.QtWidgets import QApplication, QMainWindow
from PyQt6.QtGui import QFont

from qtermx_gui import Pty, Session, TerminalWidget


def main():
    app = QApplication(sys.argv)

    # Spawn the default shell on a real pty.
    pty = Pty(24, 80)
    session = Session(pty, 24, 80)
    session.start()

    # The widget wires itself to the session's snapshots.
    widget = TerminalWidget(session)
    widget.setFont(QFont("Menlo", 13))

    window = QMainWindow()
    window.setWindowTitle("qtermx-cpp (Python)")
    window.setCentralWidget(widget)
    window.resize(800, 600)
    window.show()

    exit_code = app.exec()

    session.close()
    sys.exit(exit_code)


if __name__ == "__main__":
    main()