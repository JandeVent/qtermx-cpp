# Agent instructions — qtermx-cpp

## Project overview

qtermx-cpp is a **C++/Qt port of [pyqtermx](https://github.com/JandeVent/pyqtermx)** — a
modern terminal emulator targeting **ECMA-48**, **VT102**, and **xterm** compatibility.
The emulation pipeline (parser, screen model, PTY layer, renderer) is implemented from
scratch; xterm.js is a behavioral reference, not a code source.

The Python project is the **behavioral and architectural source of truth**. When in
doubt about how something should behave, read the Python implementation and its tests —
the C++ code must match that behavior, not invent new semantics.

## Reference project

`/Users/jan/Devel/pyqtermx` — read these before touching the corresponding C++ layer:

| File | Use for |
|---|---|
| `README.md` | Feature overview, architecture, project layout, performance numbers |
| `ROADMAP.md` | Phase-by-phase plan (5 phases) and current status |
| `docs/adr/` | Architectural decision records 0001–0007 (see invariants below) |
| `ECMA-48.md` | The grammar and repertoire spec |
| `references/xterm.js/` | Vendored behavioral reference + conformance fixture corpus (`.in`/`.text` pairs) |
| `pyqtermx/*.py` | The reference implementation, module by module |

## Build & run

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/qtermx-cpp
```

- CMake ≥ 3.16, C++17, Qt 6 (falls back to Qt 5), `Widgets` component only
- `CMAKE_AUTOMOC` / `AUTOUIC` / `AUTORCC` are on — no manual `moc` invocations
- New source files must be added to `PROJECT_SOURCES` in `CMakeLists.txt`

## Architecture

A terminal emulator is a pipeline with four layers:

```
PTY (shell) → ① byte parser  → ② screen model (grid)  → ③ renderer (Qt)
                  (state machine)   (cells, cursor, modes)   (glyphs → pixels)
                        ←────────────────────────────────────  input path (keys → escape sequences)
```

The seam between ② and ③ is the **snapshot**: immutable bundles of dirty rows, the
cursor, the viewport offset, and input-path mode flags. The GUI renders snapshots and
posts commands back; it never reads the model.

### Invariants that must survive the port

1. **Single-writer threading (ADR-0005)** — one reader thread owns the parser and
   screen; the GUI never touches the model. All mutations flow through a command
   queue; state changes cross the thread boundary as immutable snapshots over queued
   signals — lock-free and race-free by construction. In C++: the reader thread owns
   the model, the GUI thread owns the widget; communicate via queued
   `QMetaObject::invokeMethod` / signals with snapshot payloads.
2. **Never line-based parsing** — the byte-stream state machine (15-state VT500
   table) must not desync; input can be split mid-sequence arbitrarily.
3. **UTF-8 decoding upstream of the parser** — incremental decode, C1 controls
   (e.g. `0x9B` = CSI) arrive directly.
4. **Deferred (pending) wrap + resize reflow** — lines re-wrap at the new width
   instead of clipping (ADR-0003).
5. **Bounded scrollback (1000 rows default)** — xterm retention contract: full-screen
   scrolls only, ED3 erases (ADR-0006).
6. **Qt-free core** — parser, screen, palette, and PTY layers must not depend on Qt
   (mirrors pyqtermx's Qt-free `Pty`/`palette` modules); only the renderer and widget
   use Qt. This keeps the core testable headlessly.

## Migration mapping

Python module → C++ counterpart (follow this naming when creating files):

| Python (pyqtermx) | C++ (qtermx-cpp) | Layer | Role |
|---|---|---|---|
| `parser.py` | `Parser` | ① | Byte-stream state machine (VT500 table), OSC collection |
| `dispatcher.py` | `Dispatcher` | ① | Parser→emulator event protocol |
| `emulator.py` | `Emulator` | ② | CSI/ESC dispatch tables; parse events → screen ops |
| `screen.py` | `Screen` | ② | Cells, cursor, modes, scroll regions, alt screen, scrollback, viewport |
| `palette.py` | `Palette` | ② | 16 ANSI colors, cube/grayscale, defaults — shared by renderer and OSC 4/10/11 replies |
| `ptyspawn.py` | `Pty` | 0 | fork/setsid/winsize/lifecycle (Qt-free) |
| `session.py` | `Session` | glue | Reader thread, command queue, snapshot emission |
| `render.py` | `Renderer` | ③ | Snapshot → pixels: glyphs, vector box/block chars, cursor |
| `widget.py` | `TerminalWidget` | ③ | CPU renderer widget, input bridge |
| `input.py` | `InputEncoder` | ③ | `QKeyEvent` → terminal bytes, paste encoding |
| `selection.py` | `Selection` | ③ | Mouse selection state (word/line/rectangle) |
| `params.py` | `Params`/`ParamsBuilder` (`src/params.h`) | ① | Typed CSI parameters (zero-default-mode, 0xFFFFFFFF cap) |
| UTF-8 decoding | `Utf8Decoder` (`src/utf8_decoder.h`) | ① | Incremental decode, `errors="replace"` semantics (ADR-0001) |
| `wcwidth` (dep) | `wcwidth` (`src/wcwidth.h`) | ① | Cell-width measurement (Kuhn table + regional indicators) |
| `__main__.py` | `main.cpp` | app | Window + session lifecycle |

Cython fast paths (`_render_fast.pyx`, `_screen_fast.pyx`, `parser.pyx`) are
performance optimizations — in C++ the base implementation *is* the fast path; do not
replicate the Cython layer.

## Conventions

- **C++17** with Qt idioms: signals/slots over callbacks, `QString`/`QByteArray` for
  text, `QVector`/`std::vector` for grids, smart pointers (`std::unique_ptr`) for
  ownership, raw pointers only for non-owning views (e.g. `QWidget* parent`)
- **Header hygiene**: include guards (`#ifndef X_H`), forward declarations where
  possible, minimal includes in headers
- **Naming**: classes `PascalCase`, methods `camelCase`, members `m_camelCase`,
  files match the class name (`screen.h`/`screen.cpp`)
- **Threading**: only the widget and session glue touch Qt event-loop objects; the
  core (parser/screen/palette/pty) is plain C++ with no Qt dependencies
- **No hardcoded defaults**: theming (`set_font`/`set_palette`) must rebuild the
  backing store and re-render — mirror the Python API surface
- **Document WHY**: port decisions that deviate from the Python reference (e.g.
  C++-specific memory/ownership choices) need a comment or ADR

## Testing strategy

Port the Python test suite layer by layer, keeping the same oracle:

- **Unit tests per sequence family** — parser, screen, emulator, pty, input. Use
  Qt Test (`QTest`) for Qt-touching code; plain asserts or a lightweight harness for
  the Qt-free core (keep it headless-runnable)
- **xterm fixture corpus as the conformance oracle** — the `.in`/`.text` pairs in
  `references/xterm.js/test/fixtures/escape_sequence_files/` feed through the full
  pipeline and diff against `render()`. Port the fixture runner alongside the parser
- **Real programs at each milestone** — `ls`, `less`, `man`, `vim`, `htop`, `tmux`,
  then a shell; `vttest` at the end
- **Benchmarks** — pyqtermx has headless benchmarks (`bench/run.py`, ~2.45 MB/s
  scroll-flood, 0.48 ms/frame htop rasterize); re-establish comparable numbers in C++
  and keep them in a `bench/` directory

## Current status

- Phase 0 (project setup) done: git repo, directory layout (`src/`, `src/gui/`,
  `tests/`, `bench/`, `references/`), test harnesses (assert-based core harness +
  QTest), vendored xterm.js fixture corpus, `ctest` green
- Phase 1 (core pipeline) done: `Parser` (15-state VT500), `Dispatcher`,
  `Params`, `Screen` (dumb subset: print, C0, wrap, width, resize reflow,
  scrollback read API), `Emulator` (C0 + full CSI/ESC tables), Qt-free
  incremental UTF-8 decoder, wcwidth (Kuhn table); 235 tests green incl. the
  xterm fixture corpus (12/13 — t0004-LF needs a pty)
- Skeleton app: `main.cpp` + empty `MainWindow`, CMake build working
- Next steps: port the Phase 2 tests (CSI family: motion, erase, SGR, modes,
  regions, charsets, save/restore) — the code is already ported, the tests
  verify it — then screen (②), then PTY + session (glue), then renderer +
  widget (③) — follow `ROADMAP.md` phase order (Phase 2 → 6)