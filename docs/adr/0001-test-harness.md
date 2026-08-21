# Test harness: lightweight asserts for the Qt-free core, QTest for Qt layers

qtermx-cpp ports pyqtermx's 700+ pytest suite layer by layer. The core
(parser, dispatcher, emulator, screen, palette, pty) must stay Qt-free and
headless-runnable, while the renderer/widget/input layers are Qt code. We
chose two harnesses matching that split: a minimal assert-based harness
(`tests/core/harness.h`) for the core, and Qt Test (`QTest`) for the Qt
layers.

## The model

- **Core tests** (`tests/core/`): plain C++ executables. Each test file
  defines `TEST_CASE(name)` blocks using `QTERMX_CHECK` /
  `QTERMX_CHECK_EQ` macros and a `main()` that calls
  `qtermx::test::runAll()`. The harness registers cases at static-init
  time, runs them all, reports `[PASS]`/`[FAIL]` per case, and returns a
  nonzero exit code on any failure. Zero dependencies — compiles with the
  same C++17 toolchain as the core, runs headless in `ctest`.
- **Qt layer tests** (`tests/gui/`): standard `QTEST_MAIN` executables
  linked against `Qt::Test`. Used from Phase 4 onward (renderer, widget,
  input, selection).
- **Fixture corpus**: the xterm.js `.in`/`.text` pairs are vendored into
  `references/escape_sequence_files/` (copied from pyqtermx's
  `tests/fixtures/`), not fetched from the submodule — the corpus is small
  (13 pairs, ~108 KB) and the repo stays self-contained. The conformance
  runner that diffs them against `render()` lands in Phase 1.
- Both harnesses are wired into CMake via `enable_testing()` +
  `add_test`; `ctest` runs everything.

## Considered options

- **Catch2 / doctest for the core**: mature, rich assertions — but a
  third-party dependency for a codebase whose core is deliberately
  dependency-free, and the harness needs are trivial (check + report).
  Rejected for now; the harness header is small enough to swap later if
  needs grow.
- **QTest for everything**: couples the core tests to Qt, breaking the
  Qt-free, headless-runnable invariant. Rejected.
- **GoogleTest**: heavyweight dependency, same objection as Catch2.
  Rejected.

## Accepted limitations

- The harness reports failures per case but does not stop at the first
  failure (matches pytest's continue-on-failure behavior).
- `QTERMX_CHECK_EQ` prints values only via `operator==`; no pretty-printing
  of mismatched values yet — acceptable while tests are simple, extend the
  harness when Phase 1 porting needs it.