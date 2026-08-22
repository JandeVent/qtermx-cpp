# Task Context: qtermx-cpp Phase 4 — PTY + scrollback + GUI

Session ID: 2026-08-22-phase4
Created: 2026-08-22
Status: in_progress

## Current Request

Port pyqtermx Phase 4 to the C++/Qt project at /Users/jan/Devel/CppQt/qtermx-cpp.
The Python project at /Users/jan/Devel/pyqtermx is the behavioral and architectural
source of truth — port the Python module AND its tests, keeping the same oracle.

## Context Files (Standards to Follow)

- /Users/jan/.config/opencode/context/core/standards/code-quality.md
- /Users/jan/Devel/CppQt/qtermx-cpp/AGENTS.md  (project conventions — READ FIRST)

## Reference Files (Source Material)

Python reference (the oracle) — /Users/jan/Devel/pyqtermx:
- pyqtermx/ptyspawn.py → C++ Pty (Qt-free)
- pyqtermx/session.py → C++ Session (glue)
- pyqtermx/render.py → C++ Renderer (Qt)
- pyqtermx/widget.py → C++ TerminalWidget (Qt)
- pyqtermx/input.py → C++ InputEncoder (Qt)
- pyqtermx/selection.py → C++ Selection (Qt)
- pyqtermx/screen.py → C++ Screen (scrollback already ported)
- tests/pty/test_pty.py, tests/session/test_session.py, tests/screen/test_scrollback.py,
  tests/gui/test_render.py, tests/gui/test_widget.py, tests/input/test_input.py,
  tests/selection/test_selection.py

Existing C++ code — /Users/jan/Devel/CppQt/qtermx-cpp:
- src/screen.h, src/screen.cpp (Screen — scrollback/viewport API already present)
- src/emulator.h, src/emulator.cpp, src/parser.h, src/parser.cpp, src/dispatcher.h,
  src/params.h, src/utf8_decoder.h, src/wcwidth.h, src/palette.h
- src/gui/ (empty — Qt layer goes here)
- tests/core/harness.h (assert-based harness: TEST_CASE, QTERMX_CHECK)
- tests/core/test_pipeline.h (feedTo/makePipeline/rowText/splitLines/strip helpers)
- tests/gui/test_smoke.cpp (QTest example)
- CMakeLists.txt (qtermx_core static lib; add new core sources here)
- tests/CMakeLists.txt (core_tests + gui_tests executables)

## Constraints

1. **Qt-free core** — src/ (parser, dispatcher, emulator, screen, palette, pty) must
   compile with zero Qt includes; only src/gui/ and main.cpp use Qt. Pty is Qt-free.
2. **Single-writer threading (ADR-0005)** — the reader thread owns the model; the GUI
   never touches it. All state crosses the boundary as immutable snapshots over queued
   signals (QMetaObject::invokeMethod / signals with snapshot payloads).
3. **Port tests with the code** — a phase is not done until its Python tests are
   ported and green.
4. **Document WHY** — port decisions that deviate from the Python reference need a
   comment.
5. **C++17, Qt 6 (falls back to Qt 5), Widgets component only.** CMAKE_AUTOMOC on.
6. **Naming**: classes PascalCase, methods camelCase, members m_camelCase, files match
   the class name (screen.h/screen.cpp).
7. **Test harness**: Qt-free core → assert-based harness (tests/core/harness.h);
   Qt layers → Qt Test (QTest).
8. Do NOT modify CMakeLists.txt or tests/CMakeLists.txt — the orchestrator wires up
   new files after you finish. Do NOT modify files other than the ones assigned to you.

## Exit Criteria

- All assigned Python tests ported and green (build + run the specific test executable).
- New C++ files follow the project conventions and compile cleanly.
