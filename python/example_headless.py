#!/usr/bin/env python3
"""
Headless terminal session via the qtermx_gui bindings.

Drives the C++ core through Session without a QApplication — handy for
tests and scripts. (There is no separate core module: the session/model
surface lives in the qtermx_gui extension.)
"""

import os
import sys

# The extension lands in <repo>/python/build/qtermx_gui/.
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "build", "qtermx_gui"))

from qtermx_gui import Pty, Session


def main():
    # Spawn the default shell on a real pty.
    pty = Pty(24, 80)
    session = Session(pty, 24, 80)
    session.start()

    # Feed bytes through the pipeline synchronously (test/bench seam).
    session.process("Hello from qtermx!\r\n$ ")

    screen = session.screen()
    print(f"Screen: {screen.lines}x{screen.columns}")
    print(screen.render())

    # Post a command to the reader thread.
    session.sendData("ls\r")

    session.close()
    print("Done.")


if __name__ == "__main__":
    main()