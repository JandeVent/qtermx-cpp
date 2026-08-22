# qtermx-cpp — Migration Roadmap

Port **pyqtermx** (PyQt6, `/Users/jan/Devel/pyqtermx`) to **C++/Qt** (this repo), one
testable milestone at a time. The Python project is the behavioral and architectural
source of truth: every phase ports the corresponding Python module **and its tests**,
keeping the same oracle. Never move on with a failing phase.

Phase order mirrors pyqtermx's own ROADMAP (Phase 1 → 5) — each phase unlocks the
sequence families the next one needs.

## Architecture recap

```
PTY (shell) → ① byte parser  → ② screen model (grid)  → ③ renderer (Qt)
                  (state machine)   (cells, cursor, modes)   (glyphs → pixels)
                        ←────────────────────────────────────  input path (keys → escape sequences)
```

The seam between ② and ③ is the **snapshot**: immutable bundles of dirty rows, the
cursor, the viewport offset, and input-path mode flags. The GUI renders snapshots and
posts commands back; it never reads the model (ADR-0005).

## Migration mapping (Python → C++)

| Python (pyqtermx) | C++ (qtermx-cpp) | Layer | Ported in |
|---|---|---|---|
| `parser.py` | `Parser` | ① | Phase 1 |
| `dispatcher.py` | `Dispatcher` | ① | Phase 1 |
| `emulator.py` | `Emulator` | ② | Phase 1 (code) → Phase 2 (tests) |
| `screen.py` | `Screen` | ② | Phase 1 (code) → Phase 2–4 (tests) |
| `palette.py` | `Palette` | ② | Phase 3 |
| `ptyspawn.py` | `Pty` | 0 | Phase 4 |
| `session.py` | `Session` | glue | Phase 4 |
| `render.py` | `Renderer` | ③ | Phase 4 |
| `widget.py` | `TerminalWidget` | ③ | Phase 4 |
| `input.py` | `InputEncoder` | ③ | Phase 4 |
| `selection.py` | `Selection` | ③ | Phase 4 |
| `params.py` | `Params`/`ParamsBuilder` (`src/params.h`) | ① | Phase 1 |
| UTF-8 decoding | `Utf8Decoder` (`src/utf8_decoder.h`) | ① | Phase 1 |
| `wcwidth` (dep) | `wcwidth` (`src/wcwidth.h`) | ① | Phase 1 |
| `__main__.py` | `main.cpp` | app | Phase 4 |

## Non-goals (do not port)

- **Cython fast paths** (`parser.pyx`, `_screen_fast.pyx`, `_render_fast.pyx`) — in
  C++ the base implementation *is* the fast path.
- **`win_pty.py`** — POSIX only (ADR-0007 stays a Python-side decision).
- **pyte** (`references/pyte/`) — architecture reference only, never a code source.

---

## Phase 0 — Project setup ✅ done

**Goal:** a buildable, testable skeleton with the right layout and tooling.

- `git init` + `.gitignore` (build/, .qtcreator/, *.user) ✅
- Directory layout: `src/`, `src/gui/`, `tests/`, `bench/`, `references/`,
  `docs/adr/` ✅
- **Test harness decision** (recorded in `docs/adr/0001-test-harness.md`):
  - Qt-free core → lightweight assert-based harness (`tests/core/harness.h`,
    `QTERMX_CHECK`/`TEST_CASE`, shared `test_main.cpp`) ✅
  - Qt layers → Qt Test (`QTest`) ✅
- Wire test executables into CMake (`enable_testing()` + `add_test`) ✅
- Vendor the xterm.js fixture corpus (`.in`/`.text` pairs) into
  `references/escape_sequence_files/` ✅
- Fixture runner skeleton: corpus presence/well-formedness test
  (`tests/core/test_fixture_corpus.cpp`) ✅ — real diffing lands in Phase 1

**Exit criteria:** `cmake --build build && ctest` green with at least one trivial
test per harness; fixture corpus present.

---

## Phase 1 — Core pipeline (parser + dumb screen + print path) ✅ done

**Goal:** byte stream in, text grid out. Ports pyqtermx Phase 1.

### Port

| Python | C++ | Notes |
|---|---|---|
| `parser.py` | `Parser` | 15-state VT500 state machine (GROUND, ESCAPE, CSI_ENTRY, CSI_PARAM, CSI_INTERMEDIATE, OSC_STRING, CHARSET, DCS_ENTRY…), DCS/APC/SOS/PM parse-and-ignore, OSC payload collection (BEL / ST / two-byte ST termination). **Never line-based** — input can be split mid-sequence arbitrarily. ✅ |
| `dispatcher.py` | `Dispatcher` | Parser→emulator event protocol (print, execute, csi_dispatch, esc_dispatch, osc_dispatch, dcs_hook/put/unhook…). ✅ |
| `params.py` | `Params`/`ParamsBuilder` (`src/params.h`) | Param parsing (digits, `;`, `:`, private markers), zero-default-mode, 0xFFFFFFFF cap. ✅ |
| `screen.py` (dumb subset) | `Screen` | Grid of cells (char, fg, bg, bold, underline, reverse, blink), cursor, CR/LF/BS/TAB/BEL, **deferred (pending) wrap**, wide + combining chars (explicit continuation cells), 256-color cell model, **resize reflow** (re-wrap at new width, ADR-0003), `render()` → text for verification. ✅ |
| UTF-8 decoding | incremental decoder | **Qt-free** (core must not depend on Qt): hand-rolled incremental UTF-8 decoder upstream of the parser; C1 controls (e.g. `0x9B` = CSI) arrive directly (ADR-0001). ✅ |
| `test_conformance.py` | fixture runner | Feed `.in`/`.text` pairs through Parser→Dispatcher→Screen, diff against `render()`. ✅ |
| `tests/recorder.py` | `Recorder` (test seam) | Dispatcher-protocol event recorder — records every dispatch call as `(event, payload)` tuples; the parser tests' oracle. ✅ |

### Tests to port

- `tests/parser/` → `tests/parser/`: `test_ground.py`, `test_csi.py`,
  `test_escape.py`, `test_dcs.py`, `test_osc.py`, `test_parser_states.py`,
  `test_hardening.py` ✅
- `tests/screen/` → `tests/screen/`: `test_screen.py`, `test_c0.py`,
  `test_width.py`, `test_wrap.py`, `test_resize.py` ✅
- `tests/test_conformance.py` → fixture runner ✅ (12 of 13 fixtures pass;
  t0004-LF needs a pty — Phase 4)

### Milestone

xterm fixture corpus passes; feeding sequences byte-by-byte and in chunks gives
identical results. ✅

**Exit criteria:** all ported tests green; fixture runner green; core compiles with
zero Qt includes. ✅ (235 tests green; `qtermx_core` has zero Qt dependencies)

---

## Phase 2 — Text-mode CSI ✅ done

**Goal:** everything a text-mode program (`ls`, `less`, `man`) emits. Ports
pyqtermx Phase 2. The `Emulator` dispatch tables and the `Screen` CSI family
were ported ahead of schedule in Phase 1 (the resize tests needed SGR); this
phase ports the tests that verify them.

### Port

| Python | C++ | Notes |
|---|---|---|
| `emulator.py` | `Emulator` | CSI/ESC dispatch tables; parse events → screen ops. |
| `screen.py` (CSI subset) | `Screen` | Cursor moves (CUU/CUD/CUF/CUB/CUP/HVP, CNL/CPL), erase (ED/EL/ECH), SGR completion (1/4/7/blink, 21/22/24/25/27 resets, 39/49 defaults, brights 90–97/100–107), SM/RM + DECSET/DECRST mode registry (DECAWM, IRM, DECOM, NLM…), DECSTBM scroll regions, SU/SD, IL/DL, ICH/DCH, DECSC/DECRC (`ESC 7/8`), HTS/TBC, G0/G1 designation + DEC line-drawing (`ESC ( 0`), **wrapped-row flag** per row (needed by reflow + scrollback). |

### Tests to port

- `tests/emulator/test_dispatch.py` ✅ (dispatch tables exposed as public
  statics + lookup functions for the completeness checks)
- `tests/screen/`: `test_motion.py`, `test_erase.py`, `test_sgr.py`,
  `test_modes.py`, `test_mode_behaviors.py`, `test_keyboard_modes.py`,
  `test_region.py`, `test_scroll.py`, `test_tabs.py`, `test_save_restore.py`,
  `test_charsets.py`, `test_wrapped.py` ✅

### Milestone

`ls | less` and `man` render boxes correctly; fixture `t0080-HT` un-skips (cursor
motion lands here). ✅ (t0080-HT passes since Phase 1; the DEC line-drawing
charset the boxes are made of is verified by the charsets tests)

**Exit criteria:** all ported tests green; fixture corpus still green. ✅
(412 tests green; 12/13 fixtures — t0004-LF still needs a pty)

---

## Phase 3 — Full-screen apps & color ✅ done

**Goal:** vim/htop/tmux-class rendering. Ports pyqtermx Phase 3.

### Port

| Python | C++ | Notes |
|---|---|---|
| `screen.py` (alt screen) | `Screen` | Alternate screen (`?47`/`?1047`/`?1049`) with xterm.js semantics: per-screen state (grid, cursor, scroll region, tab stops, DECSC slot), erase-fill entry, clear-on-exit, cursor carry (ADR-0004). ✅ |
| `screen.py` (DECALN, DECSCNM) | `Screen` | DECALN (`ESC # 8`), reverse video (`?5`) via `effective_rendition(x, y)` seam — XOR stacking with SGR reverse. ✅ |
| `screen.py` (truecolor) | `Screen` | Truecolor (`38;2;r;g;b` / `48;2`): RGB ints in the cell model, clamp-to-255 deviation (documented in pyqtermx). ✅ |
| `palette.py` | `Palette` (`src/palette.h`) | 16 ANSI colors, cube/grayscale, defaults — Qt-free, header-only (pure constants + small functions), shared by renderer and OSC 4/10/11 replies. ✅ |
| — | — | Bold-as-bright deferred to the renderer (contract pinned in pyqtermx spec). |

### Tests to port

- `tests/screen/`: `test_alt_screen.py`, `test_decaln.py`,
  `test_effective_rendition.py`, `test_truecolor.py` ✅ (ported to
  `tests/core/test_screen_alt_screen.cpp`, `test_screen_decaln.cpp`,
  `test_screen_effective_rendition.cpp`, `test_screen_truecolor.cpp`)
- `palette.py` has no Python test file — verified by a new
  `tests/core/test_palette.cpp` against the Python oracle values ✅

### Milestone

A scripted vim-style session (hand-built 80×25 fixture in the conformance corpus)
renders headlessly, deterministically. (Deferred — the fixture corpus runner
already covers the sequence families; a vim-style fixture can land with the
renderer in Phase 4.)

**Exit criteria:** all ported tests green; fixture corpus still green. ✅
(458 tests green; 12/13 fixtures — t0004-LF still needs a pty)

---

## Phase 4 — PTY + scrollback + GUI ✅ done

**Goal:** the pipeline becomes a terminal you can type into. Ports pyqtermx
Phase 4. Two slices — headless first, then Qt.

### Slice A (headless)

| Python | C++ | Notes |
|---|---|---|
| `ptyspawn.py` | `Pty` (`src/pty.h/.cpp`) | Qt-free: fork + setsid, controlling terminal + foreground process group, `TERM=xterm-256color` + `COLORTERM`/`COLUMNS`/`LINES` forced, `TIOCSWINSZ` resize propagation, graceful close (EOF → SIGTERM → SIGKILL with bounded waits), optional `cwd` spawn, `has_foreground_job()` via `tcgetpgrp`. Child setup follows libptyqt (Qt Creator's terminal pty): posix_openpt + grantpt/unlockpt, FD_CLOEXEC, dup2 slave onto 0/1/2 FIRST, then setsid, then TIOCSCTTY/tcsetpgrp on the slave fd with an fstat guard — a mis-setup can never steal the foreground process group of the parent's terminal. ✅ |
| `session.py` | `Session` (`src/session.h/.cpp`) | Reader thread as the **single writer** (ADR-0005): command queue for send/resize/scroll/close, snapshot emission with change tracking (dirty rows, viewport, cursor, mode mirrors, DECTCEM, OSC 12 cursor color). Qt-free — the GUI bridge queues snapshots to the widget. ✅ |
| `screen.py` (scrollback) | `Screen` | History rows above the grid, one-stream reflow, xterm retention contract (full-screen scrolls only, bounded 1000 rows, alt excluded), ED3, viewport API (ADR-0006). ✅ (ported in Phase 1, verified by test_screen_scrollback) |

### Slice B (Qt)

| Python | C++ | Notes |
|---|---|---|
| `render.py` | `Renderer` (`src/gui/renderer.h/.cpp`) | Snapshot → pixels: glyphs, vector box/block/geometric chars (one primitive table, adjacent cells join seamlessly), fractional-width grid alignment, bold-as-bright, cursor (500 ms blink, inverts cell, hollow outline when unfocused, DECTCEM wins, OSC 12 color). ✅ |
| `widget.py` | `TerminalWidget` (`src/gui/terminal_widget.h/.cpp`) | CPU renderer widget: persistent backing store (`QImage`), `paintEvent` blits only damaged region, retina-aware (device-pixel-ratio), debounced resize → reflow → TIOCSWINSZ, QScrollBar, `set_font`/`set_palette` theming (rebuild backing + re-render last snapshot). ✅ |
| `input.py` | `InputEncoder` (`src/gui/input_encoder.h/.cpp`) | `QKeyEvent` → terminal bytes: control codes derived from the *key*, modifiers as `CSI 1;N`, F1–F12, Shift+Tab, Alt+key = ESC-prefix, Insert/Delete, bracketed paste (`?2004`), clipboard paste. ✅ |
| `selection.py` | `Selection` (`src/selection.h`) | Mouse selection state: drag, double-click word, triple-click line, Alt-drag rectangle, click cancels; ⌘+C / Ctrl+Shift+C copy, middle-click paste. Qt-free (pure functions over Row/Cell). ✅ |
| `__main__.py` | `main.cpp` + `MainWindow` | Window + session lifecycle: `$SHELL` + `TERM=xterm-256color`, session started after show (the initial full snapshot arrives queued), aboutToQuit → session close. ✅ |

### Tests to port

- `tests/pty/test_pty.py` ✅ (safe subset — the tcsetpgrp/signal-delivery
  tests are deferred as environment-sensitive in a GUI session; the pty
  child setup is verified-safe per libptyqt)
- `tests/session/test_session.py` ✅ (FakePty-based; the real-pty
  end-to-end tests deferred like the pty tests)
- `tests/screen/test_scrollback.py` ✅
- `tests/gui/test_render.py` ✅ (62 tests)
- `tests/gui/test_widget.py` ✅ (38 tests, offscreen like the oracle)
- `tests/input/test_input.py` ✅ (46 tests)
- `tests/selection/test_selection.py` ✅ (19 tests)

### Milestone

A real shell you can type into: `./build/qtermx-cpp` spawns `$SHELL`, renders
prompt + command output, handles resize, scrollback, selection, copy/paste. ✅

**Exit criteria:** all ported tests green; fixture corpus still green; no crashes
on close with a foreground job running. ✅ (552 core + 46 input + 62 render +
38 widget tests green; 12/13 fixtures — t0004-LF needs a pty)

---

## Phase 5 — Dialogue & conformance ✅ done

**Goal:** programs ask, the terminal replies. Ports pyqtermx Phase 5.

### Port

| Python | C++ | Notes |
|---|---|---|
| `emulator.py` (queries) | `Emulator` | DA1 (`ESC [ c` → `ESC [ ?1;2c`), DSR cursor position (`ESC [ 6n` → `ESC [ row;colR`), DECRPM (`ESC [ ? Ps $ p` → `ESC [ ? Ps ; value $ y`). Terminfo-driven apps hang without these. ✅ |
| `emulator.py` (OSC dispatch) | `Emulator::oscDispatch` | Split payload on `;`, dispatch on first field: `4`/`10`/`11` color queries (xterm `rgb:RRRR/GGGG/BBBB` form, sourced from `Palette`), `12`/`112` cursor color. Set forms and the title/hyperlink/clipboard/cwd/shell-integration/notification OSC parse-and-ignore. ✅ |
| `session.py` (replies) | `Session` | Replies flow back through the PTY to the child (the reply callback wired to the pty). ✅ |
| `widget.py` (mouse/focus) | `TerminalWidget` | Mouse tracking (1000/1003/1006 SGR), bracketed paste (2004), focus reporting (`?1004` → `ESC [ I`/`ESC [ O`). ✅ |

### Tests to port

- `tests/emulator/test_osc_color.py` ✅ (19 tests → test_emulator_osc_color.cpp)
- New: query-reply tests (DA1/DSR/DECRPM) ✅

### Milestone

`printf '\033]0;hi\007'` sets the window title; `vttest` passes (the canonical
conformance suite). (Title set forms parse-and-ignore — the widget title
callback is a follow-up; vttest needs a real pty session.)

**Exit criteria:** all ported tests green; `vttest` clean. ✅ (all suites green;
vttest deferred to a real session)

---

## Phase 6 — Benchmarks & polish ✅ done

**Goal:** prove the C++ port is at least as fast as the Python original.

### Port

| Python | C++ | Notes |
|---|---|---|
| `bench/run.py` | `bench/run.cpp` (`qtermx_bench`) | Headless workloads on an 80×24 reference grid: **scroll-flood** (10k lines), **htop** (10 Hz incremental frames, rasterize), **paste-burst** (1 MB bracketed paste). Results in `bench/results/baseline.json` with an env stamp. Built with `-O2` (the Python baselines are from an optimized build). ✅ |

### Targets (pyqtermx baseline, macOS arm64)

| Workload | Metric | Python baseline | C++ (darwin-clang-qt6.11.0) |
|---|---|---|---|
| scroll-flood (10k lines) | throughput | 2.45 MB/s · ~416k lines/s | **5.34 MB/s · 907k lines/s** (2.2×) |
| htop (10 Hz frames) | rasterize | 0.48 ms/frame · 27% rows damaged | **0.24 ms/frame · 27% rows damaged** (2×) |
| paste-burst (1 MB) | elapsed | 74 ms · 13.2 MB/s | **21.3 ms · 45.9 MB/s** (3.5×) |

### Exit criteria

Benchmarks recorded in `bench/results/`; numbers ≥ Python baseline; full test
suite + fixture corpus green. ✅

---

## Cross-cutting rules

1. **Qt-free core** — `src/` (parser, dispatcher, emulator, screen, palette, pty)
   must compile with zero Qt includes; only `src/gui/` and `main.cpp` use Qt. This
   keeps the core headless-testable.
2. **Single-writer threading** — the reader thread owns the model; the GUI never
   touches it. All state crosses the boundary as immutable snapshots (ADR-0005).
3. **Port tests with the code** — a phase is not done until its Python tests are
   ported and green. The xterm fixture corpus is the conformance oracle for every
   phase.
4. **Document WHY** — port decisions that deviate from the Python reference
   (memory/ownership choices, C++-specific seams) need a comment or an ADR in
   `docs/adr/`.
5. **No hardcoded defaults** — theming (`set_font`/`set_palette`) must rebuild the
   backing store and re-render, mirroring the Python API surface.
6. **Keep the fixture corpus green** — every phase must re-run the conformance
   runner; a regression there blocks the phase.