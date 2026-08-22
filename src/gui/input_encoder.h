#ifndef QTERMX_GUI_INPUT_ENCODER_H
#define QTERMX_GUI_INPUT_ENCODER_H

// The input encoder — QKeyEvent → terminal bytes (port of pyqtermx
// input.py, the GUI layer's input path). Pure: mode state is passed in
// (the widget mirrors it from snapshots — it never reads the model,
// ADR-0005): decCkm (DECCKM ?1), bracketedPaste (?2004), and
// scrollbackLen for the PgUp/PgDn viewport policy (history exists
// implies the normal screen — the alt screen has none).
//
// encodeKey returns the bytes to send to the child, or std::nullopt
// when the GUI must act itself instead: PgUp/PgDn scrolling the
// viewport (posted as a scroll command), paste (clipboard →
// encodePaste), copy (no-op). Every sent key is followed by a
// scrollToBottom() post (spec Q6).
//
// Signals: the terminal's only signal channel is control characters —
// the child's tty line discipline turns them into SIGINT/SIGQUIT/
// SIGTSTP (⌃+C / ⌃+\ / ⌃+Z / ⌃+Y, ISIG), stops output (⌃+S, IXON), or
// reports EOF (⌃+D). The encoder derives these from the *key*, so a
// text-less ⌃+letter event (macOS) still sends the right byte.
//
// Combo keys (the modern-terminal set): Shift+Tab is back-tab (CSI Z),
// Insert/Delete are CSI 2~/3~, F1–F12 are SS3/CSI, ⌥ prefixes editing
// keys with ESC, and modifiers on cursor/function keys use xterm's
// CSI 1;N modifier code (1+shift+2·alt+4·ctrl).
//
// The Python oracle has no IME candidate-window handling — the encoder
// is purely key/text driven, so none is ported here either.

#include <optional>

#include <QByteArray>
#include <QKeyEvent>
#include <QString>

namespace qtermx::gui {

// The input encoder (Qt layer — lives in src/gui/, unlike the Qt-free
// core). All methods are static and pure: same input, same output.
class InputEncoder
{
public:
    // The mouse action for the X10/SGR mouse encoders. The Python
    // oracle takes a string ("press"/"release"/"motion"/"wheel_up"/
    // "wheel_down"); an enum is the C++-idiomatic equivalent — a
    // deliberate deviation, documented per AGENTS.md.
    enum class MouseAction {
        Press,
        Release,
        Motion,
        WheelUp,
        WheelDown,
    };

    // A plain arrow key (no modifiers) — the wheel → cursor policy for
    // full-screen apps without mouse tracking: CSI final, or SS3 in
    // application cursor mode (DECCKM). Throws std::invalid_argument
    // for a non-arrow key (mirrors the Python ValueError).
    static QByteArray encodeArrowKey(int qkey, bool decCkm = false);

    // Encode a key press into terminal bytes, or std::nullopt for
    // GUI-side actions (viewport scroll, paste, copy).
    static std::optional<QByteArray> encodeKey(const QKeyEvent& event,
                                               bool decCkm = false,
                                               int scrollbackLen = 0);

    // The paste payload: raw UTF-8, or wrapped in bracketed-paste
    // markers when the app requested ?2004 (spec Q8).
    static QByteArray encodePaste(const QString& text, bool bracketedPaste = false);

    // X10 mouse (DECSET ?1000): `CSI M` + three bytes, each +32 —
    // button, column, row. Coordinates are 1-based; the caller clamps
    // them to the grid.
    static QByteArray encodeMouseX10(int x, int y, int button,
                                     MouseAction action, int mods = 0);

    // SGR mouse (DECSET ?1006): `CSI < b ; x ; y M`, final `m` for
    // release. Coordinates are 1-based; the caller clamps them to the
    // grid.
    static QByteArray encodeSgrMouse(int x, int y, int button,
                                     MouseAction action, int mods = 0);
};

} // namespace qtermx::gui

#endif // QTERMX_GUI_INPUT_ENCODER_H
