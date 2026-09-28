# qtermx-cpp Python bindings (SIP / PyQt6)

SIP-generated bindings for the qtermx-cpp terminal emulator. One
extension module — `qtermx_gui` — that binds the Qt GUI layer plus the
minimal session/model surface the widget API needs.

## Scope

| Layer | Bound |
|-------|-------|
| Qt GUI | `TerminalWidget`, `TerminalRenderer`, `InputEncoder` |
| Session plumbing | `Session`, `Pty`, `Screen` |
| Value types | `Cell`, `Row`, `Cursor`, `Selection`, `Snapshot` |
| Free functions | `rgb`, `isRgb`, `point`, `extend` |
| Internal | parser, emulator, params — owned by `Session`, never exposed |

There is deliberately **no separate `qtermx_core` module**. Python
drives the core through `Session` (commands in, `screen()` / snapshots
out); everything else is reachable through the widget.

## Prerequisites

```sh
pip install PyQt-builder PyQt6
```

`PyQt6` wheels do not ship `qmake`, which sip-build needs. Use the Qt
installation the C++ project builds against (adjust the path):

```sh
~/Qt/6.11.0/macos/bin/qmake --version
```

## Building

```sh
cd python
sip-build --qmake ~/Qt/6.11.0/macos/bin/qmake
```

The extension lands in `python/build/qtermx_gui/`. Use it in place:

```sh
PYTHONPATH=build/qtermx_gui python3 -c "import qtermx_gui; ..."
```

or install it (qmake must be on `PATH` for pip's build step):

```sh
PATH="$HOME/Qt/6.11.0/macos/bin:$PATH" pip install .
```

Note: `pip install -e .` (editable) is **not** supported — the sipbuild
backend has no PEP 660 hook. For a dev loop, rebuild with `sip-build`
and use `PYTHONPATH=build/qtermx_gui`. The wheel is platform-specific
and links against the local Qt installation (absolute framework paths),
so it is not redistributable as-is.

## Usage

### Qt widget

```python
from PyQt6.QtWidgets import QApplication, QMainWindow
from PyQt6.QtGui import QFont
from qtermx_gui import Pty, Session, TerminalWidget

app = QApplication([])

pty = Pty(24, 80)              # spawns $SHELL on a real pty
session = Session(pty)         # reader thread owns the model
session.start()

widget = TerminalWidget(session)
widget.setFont(QFont("Menlo", 13))

window = QMainWindow()
window.setCentralWidget(widget)
window.resize(800, 600)
window.show()

app.exec()
session.close()
```

### Headless (no QApplication)

```python
from qtermx_gui import Pty, Session

pty = Pty(["/bin/sh", "-c", "echo hello"])   # or Pty(24, 80) for $SHELL
session = Session(pty, 24, 80)
session.start()

# Drive the pipeline synchronously (test/bench seam):
session.process("Hello from qtermx!\r\n$ ")
print(session.screen().render())

session.sendData("ls\r")
session.close()
```

### Screen model

```python
from qtermx_gui import Screen, rgb

s = Screen(24, 80)
s.print("Hello, world!")
s.setFg(rgb(255, 100, 50))   # truecolor
s.setBg(0)                   # palette index
s.setBold()
print(s.render())            # plain-text dump
print(s.cursor.x, s.cursor.y)
print(s.takeDirtyRows())     # set[int], consumed
```

## Testing

```sh
cd python
python -m pytest tests -v
```

The suite (50 tests) covers the API surface plus the lifetime and
memory-management contract — the part that matters most for a SIP
binding:

- **`/KeepReference/` pins** — `Session` pins its `Pty`, `TerminalWidget`
  pins its `Session`; dropping the last Python reference of the pinned
  object must not delete the C++ object under the running reader thread
  (use-after-free). Verified with `weakref` + `gc.collect()`.
- **Borrowed returns never own** — `session.pty` and `session.screen()`
  are borrowed views; GC of the wrapper must not delete the C++ object.
- **Teardown orders** — widget-before-session, session-before-widget,
  and the plugin-style `close()`-while-widget-alive scenario must not
  crash or double-delete.
- **Value types are copies** — `Cell`/`Row`/`Cursor`/`Selection`/
  `Snapshot` copies are independent; `Screen.line()`/`cursor` are live
  references (documented `Row&` semantics).

Widget tests run on the offscreen platform (`QT_QPA_PLATFORM=offscreen`,
set in `tests/conftest.py`) and pump the event loop — snapshots arrive
via `Qt::QueuedConnection` (ADR-0005).

## Files

```
python/
  pyproject.toml            # sip-build configuration (single module)
  example_widget.py
  example_headless.py
  sip/
    qtermx_gui/
      qtermx_gui.sip        # module: imports, includes, free functions
      conversions.sip       # std::string/u32string/vector/optional mappings
      Cell.sip  Row.sip  Cursor.sip  Selection.sip  Snapshot.sip
      Pty.sip  Screen.sip  Session.sip
      InputEncoder.sip  Renderer.sip  TerminalWidget.sip
```

## Type mappings (`conversions.sip`)

| C++ | Python |
|-----|--------|
| `std::string`, `std::u32string` | `str` |
| `std::vector<int>` | `list[int]` |
| `std::vector<std::string>` | `list[str]` |
| `std::vector<Cell>`, `std::vector<Row>` | `list[Cell]`, `list[Row]` |
| `std::optional<bool>` | `bool \| None` |
| `std::optional<std::string>` | `str \| None` |
| `QString`, `QByteArray` | `str`, `bytes` |
| `QFont`, `QColor`, `QImage`, `QWidget` | PyQt6 classes |

`PyObject *` returns with `%MethodCode` (avoid STL/optional pitfalls in
generated signatures): `Screen.takeDirtyRows` → `set[int]`,
`InputEncoder.encodeKey` → `bytes | None`, `Pty.read`/`wait`/
`foregroundProgram` → `str | None` / `int | None` / `str | None`.

## Build notes learned the hard way

- `[tool.sip.bindings.<name>]` holds `sources`, `include-dirs`,
  `extra-compile-args`, `exceptions`, `headers`; `qmake-settings` goes
  in `[tool.sip.builder]` (`QT += widgets` — the widget needs
  QtWidgets).
- `sip-file` must be set explicitly (PyQt-builder otherwise expects the
  `<name>/<name>mod.sip` PyQt layout).
- `%Module(name=qtermx_gui, keyword_arguments="All")` enables kwargs.
- `exceptions = true` is required — `Screen::resize` throws.
- `headers = ["../src/gui/terminal_widget.h"]` triggers qmake's moc
  (CMake AUTOMOC does this in the C++ build).
- In SIP 6.16 (ABI 13), `%ConvertToTypeCode` uses `sipCppPtr` (a
  `Type **`), not `sipCpp`; construct with `*sipCppPtr = new Type(...)`.
- Non-copyable classes (`Session`, `Pty`) need a private copy ctor +
  assignment operator declaration in the `.sip` file.
- Constructor `%MethodCode` assigns to `sipCpp`; `%SetCode` must not
  early-return (the generated wrapper returns `sipErr ? -1 : 0`).
- `%GetCode`/`%SetCode` bodies are written inside `{ ... }` after the
  member declaration.

## Known gaps

- `TerminalRenderer.staticText` (`std::pair<QStaticText, double>`) is
  not bound.
- `Session` snapshot callbacks are wired in C++ by
  `TerminalWidget.setSession()`; there is no Python `on_snapshot`
  callable (would need a `sipCallMethod` trampoline). Python reads
  state via `Session.screen()` and the widget test seams.
- `Pty`'s `env` constructor parameter is not exposed (only `command`,
  `cwd` and size).

## Lifetime notes (read before using)

- `Session(Pty)` and `TerminalWidget(Session)` keep the argument alive
  (`/KeepReference/`) — the C++ side stores raw pointers and the reader
  thread dereferences the pty on every read. The pin is released when
  the *owning* wrapper dies, and SIP runs the C++ destructor before
  releasing pins, so teardown in any order is safe.
- `session.pty` is a borrowed property: it returns the same Python
  object passed to the constructor, but never owns it.
- `session.screen()`, `Screen.line(y)` and `Screen.cursor` are live
  references into the session/screen — keep the owner alive while using
  them (standard SIP borrowed-reference semantics).
