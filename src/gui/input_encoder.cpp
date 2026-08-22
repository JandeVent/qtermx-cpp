#include "input_encoder.h"

#include <stdexcept>

namespace qtermx::gui {

namespace {

// The modifier Qt reports for the physical Ctrl key. On macOS ⌃ maps to
// MetaModifier, while ControlModifier is the ⌘ Command key — which must
// never reach the terminal.
#ifdef Q_OS_MACOS
constexpr Qt::KeyboardModifier kCtrlMod = Qt::MetaModifier;
#else
constexpr Qt::KeyboardModifier kCtrlMod = Qt::ControlModifier;
#endif

// Key → CSI final for the cursor keys (application mode adds SS3).
std::optional<char> cursorFinal(int qkey)
{
    switch (qkey) {
    case Qt::Key_Up: return 'A';
    case Qt::Key_Down: return 'B';
    case Qt::Key_Right: return 'C';
    case Qt::Key_Left: return 'D';
    case Qt::Key_Home: return 'H';
    case Qt::Key_End: return 'F';
    default: return std::nullopt;
    }
}

// The arrow keys — the only cursor keys that switch to SS3 under
// application cursor mode (Home/End stay CSI, matching encodeKey).
bool isArrowKey(int qkey)
{
    return qkey == Qt::Key_Up || qkey == Qt::Key_Down
        || qkey == Qt::Key_Right || qkey == Qt::Key_Left;
}

// ⌃+symbol/digit keys → control codes (xterm's table on the US layout).
// Letters need no table: Key_A..Key_Z are the ASCII codes, so & 0x1f is
// the control character — ⌃+C = VINTR 0x03 (SIGINT), ⌃+Z = VSUSP 0x1a
// (SIGTSTP), … . ⌃+\ is the one symbol key in the signal set: VQUIT
// 0x1c (SIGQUIT).
std::optional<char> ctrlKeyCode(int qkey)
{
    switch (qkey) {
    case Qt::Key_Backslash: return '\x1c';  // VQUIT (SIGQUIT)
    case Qt::Key_BracketLeft: return '\x1b';  // ESC
    case Qt::Key_BracketRight: return '\x1d';  // GS
    case Qt::Key_AsciiCircum: return '\x1e';  // RS
    case Qt::Key_Underscore: return '\x1f';  // US
    case Qt::Key_Slash: return '\x1f';  // US (0x2f & 0x1f would be SI — xterm maps /)
    case Qt::Key_Question: return '\x7f';  // DEL
    case Qt::Key_Minus: return '\x1f';  // US (xterm maps ⌃+- to 0x1f, not 0x0d)
    case Qt::Key_Space: return '\x00';  // NUL
    case Qt::Key_At: return '\x00';  // ⌃+@ = NUL
    case Qt::Key_2: return '\x00';  // ⌃+2 = NUL
    case Qt::Key_3: return '\x1b';  // ESC
    case Qt::Key_4: return '\x1c';  // FS
    case Qt::Key_5: return '\x1d';  // GS
    case Qt::Key_6: return '\x1e';  // RS
    case Qt::Key_7: return '\x1f';  // US
    case Qt::Key_8: return '\x7f';  // DEL
    default: return std::nullopt;
    }
}

// Insert/Delete → CSI finals (xterm). Shift+Insert is paste — handled
// before this table.
std::optional<QByteArray> editFinal(int qkey)
{
    switch (qkey) {
    case Qt::Key_Insert: return QByteArray("2~");
    case Qt::Key_Delete: return QByteArray("3~");
    default: return std::nullopt;
    }
}

// F1–F12 → xterm sequences. F1–F4 send SS3 P..S plain; F5+ send CSI n~.
// Any modifier switches to CSI 1;N + final.
std::optional<QByteArray> fkeyFinal(int qkey)
{
    switch (qkey) {
    case Qt::Key_F1: return QByteArray("P");
    case Qt::Key_F2: return QByteArray("Q");
    case Qt::Key_F3: return QByteArray("R");
    case Qt::Key_F4: return QByteArray("S");
    case Qt::Key_F5: return QByteArray("15~");
    case Qt::Key_F6: return QByteArray("17~");
    case Qt::Key_F7: return QByteArray("18~");
    case Qt::Key_F8: return QByteArray("19~");
    case Qt::Key_F9: return QByteArray("20~");
    case Qt::Key_F10: return QByteArray("21~");
    case Qt::Key_F11: return QByteArray("23~");
    case Qt::Key_F12: return QByteArray("24~");
    default: return std::nullopt;
    }
}

// ⌃+A..Z → the ASCII control character (⌃+C = VINTR 0x03).
std::optional<char> ctrlLetter(int qkey)
{
    if (qkey >= Qt::Key_A && qkey <= Qt::Key_Z) {
        return static_cast<char>(qkey & 0x1F);
    }
    return std::nullopt;
}

// xterm CSI modifier code: 1 + shift(1) + alt(2) + ctrl(4).
int modifierCode(bool shift, bool alt, bool ctrl)
{
    return 1 + (shift ? 1 : 0) + (alt ? 2 : 0) + (ctrl ? 4 : 0);
}

// Wheel buttons leave no room for the standard modifier bits (the X10
// wheel codes 4/5 collide with shift/meta) — the xterm convention maps
// shift→2, ctrl→4, meta→8 (SGR 66/67 shift-wheel, 68/69 ctrl-wheel,
// 70/71 shift-ctrl).
int wheelMods(int mods)
{
    return ((mods & 4) >> 1) | ((mods & 16) >> 2) | (mods & 8);
}

} // namespace

QByteArray InputEncoder::encodeArrowKey(int qkey, bool decCkm)
{
    const std::optional<char> final = cursorFinal(qkey);
    if (!final || !isArrowKey(qkey)) {
        throw std::invalid_argument("not an arrow key");
    }
    if (decCkm) {
        return QByteArray("\x1bO") + *final;
    }
    return QByteArray("\x1b[") + *final;
}

std::optional<QByteArray> InputEncoder::encodeKey(const QKeyEvent& event,
                                                  bool decCkm,
                                                  int scrollbackLen)
{
    const Qt::KeyboardModifiers mods = event.modifiers();
#ifdef Q_OS_MACOS
    if (mods.testFlag(Qt::ControlModifier)) {
        // ⌘ is Command on macOS — application shortcuts (paste/copy),
        // never a control code for the shell.
        return std::nullopt;
    }
#endif
    const bool ctrl = mods.testFlag(kCtrlMod);
    const bool shift = mods.testFlag(Qt::ShiftModifier);
    const bool alt = mods.testFlag(Qt::AltModifier);
    const QString text = event.text();
    const int qkey = event.key();

    // Paste/copy shortcuts come first: they must not fall through to the
    // Ctrl+letter control-code path.
    if (ctrl && shift && (qkey == Qt::Key_V || qkey == Qt::Key_C)) {
        return std::nullopt;  // paste (clipboard) or copy (no-op) — GUI-side
    }
    if (shift && !ctrl && !alt && qkey == Qt::Key_Insert) {
        return std::nullopt;  // Shift+Insert paste — GUI-side
    }

    // PgUp/PgDn: scroll the viewport when history exists (normal screen);
    // in the alt screen (no history) they become CSI 5~/6~.
    if (qkey == Qt::Key_PageUp) {
        if (scrollbackLen > 0) {
            return std::nullopt;
        }
        return QByteArray("\x1b[5~");
    }
    if (qkey == Qt::Key_PageDown) {
        if (scrollbackLen > 0) {
            return std::nullopt;
        }
        return QByteArray("\x1b[6~");
    }

    if (qkey == Qt::Key_Return || qkey == Qt::Key_Enter) {
        return alt ? QByteArray("\x1b\r") : QByteArray("\r");
    }
    if (qkey == Qt::Key_Tab || qkey == Qt::Key_Backtab) {
        // Shift+Tab is back-tab (CSI Z — Qt reports Key_Backtab on some
        // platforms); ctrl/alt add the usual modifier code. Plain Tab is
        // the only n==1 case.
        const bool effShift = shift || qkey == Qt::Key_Backtab;
        const int n = modifierCode(effShift, alt, ctrl);
        if (n == 2) {
            return QByteArray("\x1b[Z");
        }
        if (n == 1) {
            return QByteArray("\t");
        }
        return QByteArray("\x1b[1;") + QByteArray::number(n) + "Z";
    }
    if (qkey == Qt::Key_Backspace) {
        return alt ? QByteArray("\x1b\x7f") : QByteArray("\x7f");
    }
    if (const std::optional<QByteArray> final = editFinal(qkey)) {
        const int n = modifierCode(shift, alt, ctrl);
        if (n == 1) {
            return QByteArray("\x1b[") + *final;
        }
        return QByteArray("\x1b[1;") + QByteArray::number(n) + *final;
    }
    if (const std::optional<QByteArray> final = fkeyFinal(qkey)) {
        const int n = modifierCode(shift, alt, ctrl);
        if (n == 1 && qkey <= Qt::Key_F4) {
            return QByteArray("\x1bO") + *final;  // SS3 P..S
        }
        if (n == 1) {
            return QByteArray("\x1b[") + *final;
        }
        return QByteArray("\x1b[1;") + QByteArray::number(n) + *final;
    }

    if (const std::optional<char> final = cursorFinal(qkey)) {
        const int n = modifierCode(shift, alt, ctrl);
        if (n == 1 && isArrowKey(qkey)) {
            return encodeArrowKey(qkey, decCkm);  // SS3 in app mode
        }
        if (n == 1) {
            return QByteArray("\x1b[") + *final;
        }
        return QByteArray("\x1b[1;") + QByteArray::number(n) + *final;
    }

    if (ctrl) {
        if (alt) {
            // xterm metaSendsEscape with ctrl: ESC + the control code.
            std::optional<char> base = ctrlKeyCode(qkey);
            if (!base) {
                base = ctrlLetter(qkey);
            }
            if (base) {
                return QByteArray("\x1b") + *base;
            }
        }
        if (text.size() == 1 && text.at(0).unicode() < 0x20) {
            return text.toUtf8();  // Ctrl+letter → the control code itself
        }
        // ⌃+letter events often carry no text (macOS) — derive the
        // control code from the key, so Ctrl+C is always VINTR (SIGINT)
        // and Ctrl+\ always VQUIT (SIGQUIT). The tty line discipline
        // turns these bytes into signals; the terminal only sends them.
        std::optional<char> code = ctrlKeyCode(qkey);
        if (!code) {
            code = ctrlLetter(qkey);
        }
        if (code) {
            return QByteArray(1, *code);
        }
    }
    if (alt && !ctrl && text.size() == 1 && text.at(0).unicode() >= 0x20) {
        return QByteArray("\x1b") + text.toUtf8();  // Alt+letter → ESC + char
    }
    if (!text.isEmpty()) {
        return text.toUtf8();
    }
    return std::nullopt;  // dead keys, unhandled keys
}

QByteArray InputEncoder::encodePaste(const QString& text, bool bracketedPaste)
{
    const QByteArray data = text.toUtf8();
    if (bracketedPaste) {
        return QByteArray("\x1b[200~") + data + QByteArray("\x1b[201~");
    }
    return data;
}

QByteArray InputEncoder::encodeMouseX10(int x, int y, int button,
                                        MouseAction action, int mods)
{
    int b = 0;
    switch (action) {
    case MouseAction::Release: b = 3 + mods; break;
    case MouseAction::Motion: b = button + 32 + mods; break;
    case MouseAction::Press: b = button + mods; break;
    case MouseAction::WheelUp: b = 4 + wheelMods(mods); break;
    case MouseAction::WheelDown: b = 5 + wheelMods(mods); break;
    }
    QByteArray out("\x1b[M");
    out += static_cast<char>(32 + b);
    out += static_cast<char>(32 + x);
    out += static_cast<char>(32 + y);
    return out;
}

QByteArray InputEncoder::encodeSgrMouse(int x, int y, int button,
                                        MouseAction action, int mods)
{
    int code = 0;
    char final = 'M';
    switch (action) {
    case MouseAction::Release: code = button + mods; final = 'm'; break;
    case MouseAction::Motion: code = 32 + button + mods; break;
    case MouseAction::Press: code = button + mods; break;
    case MouseAction::WheelUp: code = 64 + wheelMods(mods); break;
    case MouseAction::WheelDown: code = 65 + wheelMods(mods); break;
    }
    QByteArray out("\x1b[<");
    out += QByteArray::number(code);
    out += ';';
    out += QByteArray::number(x);
    out += ';';
    out += QByteArray::number(y);
    out += final;
    return out;
}

} // namespace qtermx::gui
