# qtermx-cpp

A **C++/Qt port of [pyqtermx](https://github.com/JandeVent/pyqtermx)** — a
modern terminal emulator targeting the same **ECMA-48**, **VT102**, and
**xterm** compatibility (the de facto modern standard), with the emulation
pipeline implemented from scratch in C++17.

The port mirrors the [sister project](https://github.com/JandeVent/pyqtermx)
module by module: parser, screen model, PTY layer, renderer — xterm.js remains
a behavioral reference, never a code source. The Python implementation is the
behavioral and architectural source of truth; every layer below ports its
Python counterpart **and its tests**. The result is a terminal you can
actually type into: `./build/qtermx-cpp` spawns your shell.

And it ships in a tiny package: the release executable is **~360 KB** (a
~1.7 MB packaged app), with **no dependencies beyond Qt and the system
libraries** — the entire emulation stack (15-state parser, screen model, PTY
layer, renderer) fits in a few hundred kilobytes, so it deploys as fast as it
runs.

![A live session rendered by the qtermx-cpp pipeline through its SIP/PyQt6 bindings](https://raw.githubusercontent.com/JandeVent/qtermx-cpp/main/python/demo.png)

## Quick Start

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/qtermx-cpp
```

## Screenshot

![qtermx-cpp rendering a live session, screenshotted from a real TerminalWidget](https://raw.githubusercontent.com/JandeVent/qtermx-cpp/main/python/demo.png)

Rendered by the C++ pipeline — parser → emulator → screen → renderer — the
same pipeline the GUI ships, driven through the SIP/PyQt6 bindings
(`python/demo.py`).

## Features

**Size & dependencies**

- A ~1.7 MB packaged app; the release executable itself is ~360 KB —
  the whole terminal, from byte parser to pixel renderer, in a few
  hundred kilobytes.
- **Zero dependencies beyond Qt and the system libraries** — no
  third-party runtime, no vendored C libraries: the 15-state parser,
  incremental UTF-8 decoder, wcwidth (Kuhn table + regional
  indicators), PTY layer, and vector renderer are all implemented from
  scratch in this repo.
- The Qt-free core keeps it portable: the same code that drives the GUI
  builds headless for tests, benchmarks, and embedding (and links into
  the SIP/PyQt6 bindings).

**Emulation core**

- Full byte-stream parser: the 15-state VT500 state machine with
  DCS/APC/SOS/PM parse-and-ignore, so the stream never desyncs. Input
  can be split mid-sequence arbitrarily — it is never line-based.
- Incremental UTF-8 decoding upstream of the parser (`errors="replace"`
  semantics, ADR-0001); C1 controls (e.g. `0x9B` = CSI) arrive directly.
- Complete text-mode CSI: cursor motion (CUU/CUD/CUF/CUB/CUP/CHA/VPA,
  CNL/CPL), erase (ED/EL/ECH), insert/delete (ICH/DCH/IL/DL), scroll
  (SU/SD, DECSTBM scroll regions), SGR rendition (bold, dim, italic,
  underline, blink, reverse, hidden, strike, overline), tab stops.
- 256-color and truecolor cell model (`38;2;r;g;b` / `48;2`), with the
  same documented clamp-to-255 deviation as the reference.
- Deferred (pending) wrap, wrapped-row tracking, and **resize reflow** —
  lines re-wrap at the new width instead of clipping (ADR-0003).
- Wide and combining characters (explicit continuation cells), cell-width
  measurement via a Kuhn-table wcwidth with regional indicators.
- DEC special graphics (line-drawing) charset, G0–G3 designation and
  shifting — `man` and `ls` boxes render correctly.
- Alternate screen (`?47`/`?1047`/`?1049`) with xterm.js semantics:
  per-screen state, erase-fill entry, cursor carry.
- DECALN, DECSCNM reverse video, DECSC/DECRC save/restore,
  application cursor keys (DECCKM), insert/origin/newline modes.
- **Scrollback**: bounded history (1000 rows default), xterm retention
  contract (full-screen scrolls only, ED3 erases), and a viewport the
  GUI scrolls with PgUp/PgDn, the mouse wheel, or a scrollbar.

**Rendering (Qt)**

- `TerminalWidget` — the **shipping CPU backend**: snapshots render
  into a persistent `QImage`, and `paintEvent` blits only the damaged
  region (partial rendering end to end).
- Retina-aware (device-pixel-ratio) backing store; crisp font-smoothed
  glyphs; bold-as-bright applied at render time.
- Box-drawing, block characters, and geometric shapes (squares,
  circles, diamonds, triangles, bullets) drawn as vectors from one
  primitive table — adjacent cells join seamlessly (no font seams in
  `htop` or `tmux`) and tiny glyphs (TUI spinner dots) stay crisp
  instead of antialiasing to a speck. Braille stays in the font.
- Glyphs aligned to the grid at fractional cell width
  (`QFontMetricsF`), so text and vector cells never drift.
- Cursor: a 500 ms blinking block that **inverts the cell it sits on**
  (the glyph stays visible, xterm-style); when the widget loses focus
  it becomes a hollow rectangle outline. The blink is gated on focus,
  and the snapshot's DECTCEM (`?25`) visibility always wins.
- Theming: `set_font()` / `set_palette()` rebuild the backing and
  re-render the last snapshot — no hardcoded defaults. The themed
  colors also drive the OSC 4/10/11 color queries, so TUI apps inside
  the terminal detect the real light/dark theme.

**Input**

- Full key encoding: control codes derived from the *key* (Ctrl+C is
  always SIGINT, even on macOS where text-less events carry no text),
  modifiers as xterm `CSI 1;N` codes, F1–F12, Shift+Tab back-tab,
  Alt+key = ESC-prefix, Insert/Delete.
- Bracketed paste (`?2004`), clipboard paste via Ctrl+Shift+V /
  Shift+Insert, mouse tracking (`?1000`/`?1003`/`?1006` SGR), focus
  reporting (`?1004` → `ESC [ I` / `ESC [ O`).
- Mouse: drag selects, double-click selects a word, triple-click a
  line, Alt-drag a rectangle — a single click cancels any selection;
  ⌘+C (macOS) / Ctrl+Shift+C copies, middle-click pastes.
- Signals flow through the tty line discipline: Ctrl+C/Z/\ deliver real
  SIGINT/SIGTSTP/SIGQUIT — the child gets a controlling terminal.

**Session & PTY (Qt-free)**

- Qt-free `Pty` layer: fork + `setsid`, controlling terminal and
  foreground process group, `TERM=xterm-256color` and
  `COLORTERM=truecolor` forced for the child (COLUMNS/LINES are
  deliberately *not* set — terminals that inspect them would prefer
  them over the `TIOCSWINSZ` ioctl, and they go stale on resize), a
  UTF-8 `LANG`/`LC_CTYPE` fallback injected when the launching
  environment has no locale at all (a Finder/Dock launch — without it
  the child runs in the POSIX locale and `ls` prints non-ASCII
  filenames as `??????`; pyqtermx omits this because it assumes a
  launcher that already exports one), `TIOCSWINSZ` resize propagation,
  graceful close (EOF → SIGTERM → SIGKILL with bounded waits).
- Spawn in a working directory: `Pty(cwd=...)` chdirs the child before
  exec, so a session can start in a requested folder.
- Foreground-job close guard: `Pty::hasForegroundJob()` (via
  `tcgetpgrp`) tells the widget whether a job owns the terminal, so
  closing the window doesn't kill a running foreground process.
- **Single-writer threading** (ADR-0005): one reader thread owns the
  parser and screen; the GUI never touches the model. All mutations
  flow through a command queue; state changes cross the thread
  boundary as immutable snapshots over queued signals — lock-free and
  race-free by construction.
- Query replies: DA1 (`ESC [ c` → `ESC [ ?1;2c`), DSR cursor position
  (`ESC [ 6n` → `ESC [ row;colR`) — terminfo-driven apps hang without
  them. DECRQM (`ESC [ ? Ps $ p`) is *deliberately* unanswered: any
  DECRPM reply reads as "mode supported", which disables Textual's
  SIGWINCH fallback.
- OSC color queries (`4` palette, `10` fg, `11` bg, `12`/`112` cursor
  color): the emulator answers the child's theme-detection queries with
  the current palette/defaults (xterm `rgb:RRRR/GGGG/BBBB` form),
  sourced from the Qt-free `Palette`.

**Python bindings (SIP/PyQt6)**

- `python/` — SIP-generated `qtermx_gui` extension module binding
  `TerminalWidget`, `Session`, `Pty`, `Screen`, and the value types,
  so the C++ terminal is scriptable and screenshottable from Python
  (see `python/README.md`).

## Performance

Headless benchmarks from `bench/run.cpp` (80×24 reference grid,
darwin-clang, Qt 6.11.0 — env stamp in `bench/results/baseline.json`),
compared against the Python baseline (`bench/run.py` in the
[sister project](https://github.com/JandeVent/pyqtermx)):

| Workload | Metric | Python baseline | C++ (qtermx-cpp) |
|---|---|---|---|
| scroll-flood (10k lines) | throughput | 2.45 MB/s · ~416k lines/s | **5.64 MB/s · ~957k lines/s** (2.3×) |
| htop (10 Hz incremental frames) | rasterize | 0.48 ms/frame · 27% rows damaged | **0.24 ms/frame · 27% rows damaged** (2.0×) |
| paste-burst (1 MB bracketed paste) | elapsed | 74 ms · 13.2 MB/s | **20.5 ms · 47.7 MB/s** (3.6×) |

Re-measure after an optimization round with:

```sh
./build/bench/run            # re-run all workloads, refresh baseline
./build/bench/run --json     # machine-readable JSON, no baseline write
```

## Requirements

- CMake ≥ 3.16 and a C++17 compiler
- Qt 6 (`Widgets` component; falls back to Qt 5)
- A POSIX platform (developed on macOS)

## Install

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

The build splits into two targets: the Qt-free core (`qtermx_core` — parser,
dispatcher, emulator, screen, palette, pty, session) and the Qt GUI
(`qtermx-cpp`). The core compiles with zero Qt includes, so it stays
headless-testable.

## Run

```sh
./build/qtermx-cpp
```

Starts your `$SHELL` in an `xterm-256color` session. Close the window to shut
the session down cleanly (EOF/SIGHUP to the child, then SIGTERM/SIGKILL
escalation).

## Architecture

A terminal emulator is not "a window that shows text" — it is a
pipeline with four layers:

```
PTY (shell) → ① byte parser  → ② screen model (grid)  → ③ renderer (Qt)
                  (state machine)   (cells, cursor, modes)   (glyphs → pixels)
                        ←────────────────────────────────────  input path (keys → escape sequences)
```

The seam between ② and ③ is the **snapshot**: the reader thread emits
immutable bundles of dirty rows, the cursor, the viewport offset, and
the input-path mode flags. The GUI renders snapshots and posts commands
back; it never reads the model (ADR-0005).

### Project layout

| File | Layer | Role |
|---|---|---|
| `src/parser.h` / `src/parser.cpp` | ① | Byte-stream state machine (VT500 table), OSC collection |
| `src/dispatcher.h` | ① | The parser→emulator event protocol |
| `src/emulator.h` / `src/emulator.cpp` | ② | CSI/ESC dispatch tables + OSC dispatch; parse events → screen ops; DA1/DSR replies |
| `src/screen.h` / `src/screen.cpp` | ② | The dumb model: cells, cursor, modes, scroll regions, alt screen, scrollback, viewport |
| `src/palette.h` | ② | Qt-free palette: the 16 ANSI colors, cube/grayscale, defaults — shared by the renderer and OSC 4/10/11 replies |
| `src/pty.h` / `src/pty.cpp` | 0 | Qt-free PTY spawn: fork/setsid/winsize/lifecycle |
| `src/session.h` / `src/session.cpp` | glue | Reader thread, command queue, snapshot emission |
| `src/gui/renderer.h` / `src/gui/renderer.cpp` | ③ | Snapshot → pixels: glyphs, vector box/block chars, cursor |
| `src/gui/terminal_widget.h` / `src/gui/terminal_widget.cpp` | ③ | `TerminalWidget` (CPU), input bridge |
| `src/gui/input_encoder.h` / `src/gui/input_encoder.cpp` | ③ | `QKeyEvent` → terminal bytes, paste encoding |
| `src/selection.h` | ③ | Mouse selection state (word/line/rectangle) |
| `src/params.h` | ① | Typed CSI parameters (zero-default-mode, 0xFFFFFFFF cap) |
| `src/utf8_decoder.h` | ① | Incremental decode, `errors="replace"` semantics |
| `src/wcwidth.h` / `src/wcwidth.cpp` | ① | Cell-width measurement (Kuhn table + regional indicators) |
| `main.cpp` / `mainwindow.cpp` | app | Thin glue: window + session lifecycle |

## Compatibility & status

The port follows the [sister project](https://github.com/JandeVent/pyqtermx)'s
phase plan (ECMA-48 → DEC private (`?`) sequences → xterm extensions; see
`ROADMAP.md`):

| Phase | Status |
|---|---|
| Phase 0 — project setup | done |
| Phase 1 — parser + core pipeline | done |
| Phase 2 — text-mode CSI | done |
| Phase 3 — full-screen apps & color | done |
| Phase 4 — PTY, scrollback, GUI | done (a real shell you can type into) |
| Phase 5 — dialogue & conformance | done: DA1/DSR replies, OSC color queries (4/10/11) and cursor color (12/112), mouse tracking (1000/1003/1006), focus reporting; DECRQM deliberately unanswered (Textual regression). OSC title set forms parse-and-ignore — the widget title callback is a follow-up; `vttest` deferred to a real pty session |
| Phase 6 — benchmarks & polish | done (≥ 2× the Python reference on every workload) |

## Development

```sh
cmake --build build
ctest --test-dir build        # 566 assert-based core cases + Qt Test suites (input/render/widget/smoke)
./build/bench/run             # perf harness (see bench/results/baseline.json)
```

- **Testing strategy**: unit tests per sequence family ported from the
  Python suite with the same oracle, plus the xterm fixture corpus
  (`references/escape_sequence_files/`) as the conformance oracle —
  the `.in`/`.text` pairs diff against `render()` (12/13 passing;
  t0004-LF needs a pty).
- **Design decisions** live in `docs/adr/` (test harness,
  single-writer threading, …) and in `AGENTS.md`, mirroring the
  invariants of the sister project.

## Further reading

- [pyqtermx](https://github.com/JandeVent/pyqtermx) — the sister project: the
  behavioral and architectural source of truth for this port
- `ROADMAP.md` — phase-by-phase migration plan with the Python→C++ mapping
- `AGENTS.md` — project conventions, invariants, testing strategy
- `docs/adr/` — architectural decision records
- `references/escape_sequence_files/` — vendored xterm.js fixture corpus
- `python/README.md` — the SIP/PyQt6 bindings

## License

MIT — same license as the sister project
[pyqtermx](https://github.com/JandeVent/pyqtermx) — see [LICENSE](LICENSE).
Copyright (c) 2018-2026 Connet Information Technology Company, Shanghai.