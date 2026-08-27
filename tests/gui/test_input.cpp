// Port of pyqtermx tests/input/test_input.py — the input encoder
// (QKeyEvent → terminal bytes), unit-tested with synthetic QKeyEvents.
// No widget, no QApplication needed (QTEST_MAIN provides one anyway).
// Mode state is passed in (the widget mirrors it from snapshots; it
// never reads the model): decCkm (?1 application cursor mode),
// bracketedPaste (?2004), and scrollbackLen for the PgUp/PgDn viewport
// policy (history exists implies the normal screen — the alt screen has
// none, ADR-0006).
//
// Return value contract: bytes to send to the child, or nullopt when
// the GUI must act itself (PgUp/PgDn scrolling the viewport, paste,
// copy).
#include <initializer_list>
#include <optional>

#include <QKeyEvent>
#include <QtTest>

#include "../../src/gui/input_encoder.h"

using qtermx::gui::InputEncoder;

namespace {

// The modifier Qt reports for the physical Ctrl key: ⌃ (MetaModifier)
// on macOS, ControlModifier elsewhere.
Qt::KeyboardModifiers ctrl()
{
#ifdef Q_OS_MACOS
    return Qt::MetaModifier;
#else
    return Qt::ControlModifier;
#endif
}

// A synthetic key press, mirroring the Python test's `key()` helper.
QKeyEvent key(int code, const QString& text = QString(),
              Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
    return QKeyEvent(QEvent::KeyPress, code, modifiers, text);
}

// Assert encodeKey returns exactly `expected`.
void checkEncode(const QKeyEvent& event, const QByteArray& expected,
                 bool decCkm = false, int scrollbackLen = 0)
{
    const std::optional<QByteArray> result =
        InputEncoder::encodeKey(event, decCkm, scrollbackLen);
    QVERIFY2(result.has_value(), "expected bytes, got nullopt");
    QCOMPARE(*result, expected);
}

// Assert encodeKey returns nullopt (GUI-side action).
void checkEncodeNone(const QKeyEvent& event, bool decCkm = false,
                     int scrollbackLen = 0)
{
    const std::optional<QByteArray> result =
        InputEncoder::encodeKey(event, decCkm, scrollbackLen);
    QVERIFY2(!result.has_value(), "expected nullopt, got bytes");
}

// Build a QByteArray from raw byte values (for the +32 mouse encodings).
QByteArray bytes(std::initializer_list<int> values)
{
    QByteArray out;
    for (int v : values) {
        out.append(static_cast<char>(v));
    }
    return out;
}

const Qt::KeyboardModifiers kAlt = Qt::AltModifier;
const Qt::KeyboardModifiers kShift = Qt::ShiftModifier;

// The terminal→program signal set: every entry must survive a text-less
// ⌃+event — the live macOS case that dropped Ctrl+C entirely (htop
// never got SIGINT).
struct SignalCase {
    int key;
    char expected;
};
const SignalCase kSignalMatrix[] = {
    { Qt::Key_C, '\x03' },         // VINTR → SIGINT
    { Qt::Key_Backslash, '\x1c' }, // VQUIT → SIGQUIT
    { Qt::Key_Z, '\x1a' },         // VSUSP → SIGTSTP
    { Qt::Key_Y, '\x19' },         // VDSUSP → SIGTSTP (delayed)
    { Qt::Key_D, '\x04' },         // VEOF → EOF on read
    { Qt::Key_S, '\x13' },         // VSTOP → output stops (IXON)
    { Qt::Key_Q, '\x11' },         // VSTART → output resumes
    { Qt::Key_O, '\x0f' },         // VDISCARD → discard output
    { Qt::Key_R, '\x12' },         // VREPRINT
    { Qt::Key_V, '\x16' },         // VLNEXT
    { Qt::Key_W, '\x17' },         // VWERASE
    { Qt::Key_U, '\x15' },         // VKILL
    { Qt::Key_H, '\x08' },         // VERASE
    { Qt::Key_T, '\x14' },         // VSTATUS (macOS)
};

} // namespace

class TestInput : public QObject
{
    Q_OBJECT

private slots:
    // -- Printables --
    void printableCharacter();
    void shiftGivesUppercase();
    void utf8Text();
    void deadKeyWithoutTextIsNone();

    // -- Editing keys --
    void enterIsCarriageReturn();
    void shiftEnterIsLineFeed();
    void tabAndBackspace();

    // -- Modifier encodings --
    void ctrlLetterIsControlCode();

    // -- Control characters: the terminal→program signal set --
    void ctrlSignalSetWithText();
    void ctrlSignalSetWithoutText();
    void ctrlLettersCoverTheFullAsciiRange();
    void copyShortcutWithoutTextIsStillNoop();

    // -- Combo keys: the modern-terminal set --
    void shiftTabIsBacktabCsiZ();
    void tabWithModifiers();
    void insertAndDelete();
    void functionKeys();
    void altPrefixesEditingKeys();
    void ctrlAltLetterIsEscControl();
    void ctrlSymbolsAndDigits();
    void altLetterIsEscChar();

    // -- Cursor keys: CSI, SS3 (DECCKM), and modifier encodings --
    void arrowsPlainCsi();
    void arrowsDecckmSs3();
    void arrowSingleModifiers();
    void arrowCombinedModifiers();
    void homeEndPlainAndModifier();

    // -- Page keys: viewport policy (spec Q8) --
    void pgupScrollsViewportWhenHistoryExists();
    void pgupSendsCsiWhenNoHistory();
    void pgdnScrollsViewportWhenHistoryExists();

    // -- Paste and copy --
    void pasteShortcutsReturnNone();
    void copyShortcutIsNoop();
    void encodePastePlainAndBracketed();

    // -- macOS: Command never reaches the terminal --
#ifdef Q_OS_MACOS
    void commandKeyNeverReachesTheTerminal();
    void controlKeyIsMetaModifierOnMacos();
#endif

    // -- Mouse protocol encoders (DECSET ?1000 X10 / ?1006 SGR) --
    void x10MousePressEncodes32OffsetBytes();
    void x10MouseButtonsAndRelease();
    void x10MouseMotionAdds32();
    void x10MouseModifierBits();
    void x10WheelUsesButtons4And5();
    void x10WheelUsesShiftedModifierBits();
    void sgrMousePressLeft();
    void sgrMouseReleaseUsesLowercaseM();
    void sgrMouseMotionAdds32();
    void sgrMouseWheelUpDown();
    void sgrMouseWheelUsesShiftedModifierBits();
    void sgrMouseModifiersSumIntoButton();
};

// -- Printables -----------------------------------------------------------

void TestInput::printableCharacter()
{
    checkEncode(key(Qt::Key_A, QStringLiteral("a")), QByteArray("a"));
}

void TestInput::shiftGivesUppercase()
{
    checkEncode(key(Qt::Key_A, QStringLiteral("A"), kShift), QByteArray("A"));
}

void TestInput::utf8Text()
{
    checkEncode(key(Qt::Key_E, QStringLiteral("héllo")),
                QStringLiteral("héllo").toUtf8());
}

void TestInput::deadKeyWithoutTextIsNone()
{
    checkEncodeNone(key(Qt::Key_Dead_Grave, QString()));
}

// -- Editing keys ---------------------------------------------------------

void TestInput::enterIsCarriageReturn()
{
    checkEncode(key(Qt::Key_Return, QStringLiteral("\r")), QByteArray("\r"));
    checkEncode(key(Qt::Key_Enter, QStringLiteral("\r")), QByteArray("\r"));
}

void TestInput::shiftEnterIsLineFeed()
{
    checkEncode(key(Qt::Key_Return, QStringLiteral("\r"), kShift), QByteArray("\n"));
    checkEncode(key(Qt::Key_Enter, QStringLiteral("\r"), kShift), QByteArray("\n"));
}

void TestInput::tabAndBackspace()
{
    checkEncode(key(Qt::Key_Tab, QStringLiteral("\t")), QByteArray("\t"));
    checkEncode(key(Qt::Key_Backspace, QStringLiteral("\x7f")), QByteArray("\x7f"));
}

// -- Modifier encodings ---------------------------------------------------

void TestInput::ctrlLetterIsControlCode()
{
    checkEncode(key(Qt::Key_A, QStringLiteral("\x01"), ctrl()), QByteArray("\x01"));
    checkEncode(key(Qt::Key_C, QStringLiteral("\x03"), ctrl()), QByteArray("\x03"));
}

// -- Control characters: the terminal→program signal set -----------------

void TestInput::ctrlSignalSetWithText()
{
    // Qt events carry the control char in text (X11-style events).
    for (const SignalCase& c : kSignalMatrix) {
        checkEncode(key(c.key, QString(QChar(c.expected)), ctrl()),
                    QByteArray(1, c.expected));
    }
}

void TestInput::ctrlSignalSetWithoutText()
{
    // macOS ⌃+letter events carry no text — the code must come from the
    // key alone (the regression that broke Ctrl+C on htop).
    for (const SignalCase& c : kSignalMatrix) {
        checkEncode(key(c.key, QString(), ctrl()), QByteArray(1, c.expected));
    }
}

void TestInput::ctrlLettersCoverTheFullAsciiRange()
{
    for (int i = 0; i < 26; ++i) {
        const int qkey = Qt::Key_A + i;
        checkEncode(key(qkey, QString(), ctrl()),
                    QByteArray(1, static_cast<char>(i + 1)));
    }
}

void TestInput::copyShortcutWithoutTextIsStillNoop()
{
    // The ⌃⇧C copy shortcut must keep winning over the key-derived
    // control code even when the event carries no text.
    checkEncodeNone(key(Qt::Key_C, QString(), ctrl() | kShift));
}

// -- Combo keys: the modern-terminal set ---------------------------------

void TestInput::shiftTabIsBacktabCsiZ()
{
    // The regression: Qt reports Key_Backtab (no text) for Shift+Tab.
    checkEncode(key(Qt::Key_Backtab, QString()), QByteArray("\x1b[Z"));
    checkEncode(key(Qt::Key_Tab, QStringLiteral("\t"), kShift), QByteArray("\x1b[Z"));
}

void TestInput::tabWithModifiers()
{
    checkEncode(key(Qt::Key_Tab, QStringLiteral("\t")), QByteArray("\t"));
    checkEncode(key(Qt::Key_Tab, QStringLiteral("\t"), ctrl()), QByteArray("\x1b[1;5Z"));
    checkEncode(key(Qt::Key_Tab, QStringLiteral("\t"), kAlt), QByteArray("\x1b[1;3Z"));
    checkEncode(key(Qt::Key_Backtab, QString(), ctrl()), QByteArray("\x1b[1;6Z"));
    checkEncode(key(Qt::Key_Backtab, QString(), kAlt), QByteArray("\x1b[1;4Z"));
    checkEncode(key(Qt::Key_Tab, QStringLiteral("\t"), ctrl() | kAlt), QByteArray("\x1b[1;7Z"));
}

void TestInput::insertAndDelete()
{
    checkEncode(key(Qt::Key_Insert, QString()), QByteArray("\x1b[2~"));
    checkEncode(key(Qt::Key_Delete, QString()), QByteArray("\x1b[3~"));
    checkEncode(key(Qt::Key_Delete, QString(), ctrl()), QByteArray("\x1b[3;5~"));
    checkEncode(key(Qt::Key_Insert, QString(), kAlt), QByteArray("\x1b[2;3~"));
}

void TestInput::functionKeys()
{
    checkEncode(key(Qt::Key_F1, QString()), QByteArray("\x1bOP"));
    checkEncode(key(Qt::Key_F4, QString()), QByteArray("\x1bOS"));
    checkEncode(key(Qt::Key_F5, QString()), QByteArray("\x1b[15~"));
    checkEncode(key(Qt::Key_F10, QString()), QByteArray("\x1b[21~"));
    checkEncode(key(Qt::Key_F12, QString()), QByteArray("\x1b[24~"));
    checkEncode(key(Qt::Key_F1, QString(), kShift), QByteArray("\x1b[1;2P"));
    checkEncode(key(Qt::Key_F5, QString(), ctrl()), QByteArray("\x1b[1;515~"));
    checkEncode(key(Qt::Key_F10, QString(), kAlt), QByteArray("\x1b[1;321~"));
}

void TestInput::altPrefixesEditingKeys()
{
    checkEncode(key(Qt::Key_Backspace, QStringLiteral("\x7f"), kAlt), QByteArray("\x1b\x7f"));
    checkEncode(key(Qt::Key_Return, QStringLiteral("\r"), kAlt), QByteArray("\x1b\r"));
}

void TestInput::ctrlAltLetterIsEscControl()
{
    // xterm metaSendsEscape with ctrl: ESC + the control code.
    checkEncode(key(Qt::Key_C, QString(), ctrl() | kAlt), QByteArray("\x1b\x03"));
    checkEncode(key(Qt::Key_C, QStringLiteral("\x03"), ctrl() | kAlt), QByteArray("\x1b\x03"));
}

void TestInput::ctrlSymbolsAndDigits()
{
    checkEncode(key(Qt::Key_Space, QString(), ctrl()), QByteArray("\x00", 1));  // NUL
    checkEncode(key(Qt::Key_At, QString(), ctrl()), QByteArray("\x00", 1));     // ⌃+@
    checkEncode(key(Qt::Key_2, QString(), ctrl()), QByteArray("\x00", 1));
    checkEncode(key(Qt::Key_3, QString(), ctrl()), QByteArray("\x1b"));      // ESC
    checkEncode(key(Qt::Key_Minus, QString(), ctrl()), QByteArray("\x1f"));  // US
    checkEncode(key(Qt::Key_Slash, QString(), ctrl()), QByteArray("\x1f"));  // US
    checkEncode(key(Qt::Key_8, QString(), ctrl()), QByteArray("\x7f"));      // DEL
}

void TestInput::altLetterIsEscChar()
{
    checkEncode(key(Qt::Key_A, QStringLiteral("a"), kAlt), QByteArray("\x1b") + "a");
}

// -- Cursor keys: CSI, SS3 (DECCKM), and modifier encodings --------------

void TestInput::arrowsPlainCsi()
{
    checkEncode(key(Qt::Key_Up, QString()), QByteArray("\x1b[A"));
    checkEncode(key(Qt::Key_Down, QString()), QByteArray("\x1b[B"));
    checkEncode(key(Qt::Key_Right, QString()), QByteArray("\x1b[C"));
    checkEncode(key(Qt::Key_Left, QString()), QByteArray("\x1b[D"));
}

void TestInput::arrowsDecckmSs3()
{
    checkEncode(key(Qt::Key_Up, QString()), QByteArray("\x1bOA"), /*decCkm=*/true);
    checkEncode(key(Qt::Key_Down, QString()), QByteArray("\x1bOB"), /*decCkm=*/true);
    checkEncode(key(Qt::Key_Right, QString()), QByteArray("\x1bOC"), /*decCkm=*/true);
    checkEncode(key(Qt::Key_Left, QString()), QByteArray("\x1bOD"), /*decCkm=*/true);
}

void TestInput::arrowSingleModifiers()
{
    checkEncode(key(Qt::Key_Up, QString(), kShift), QByteArray("\x1b[1;2A"));
    checkEncode(key(Qt::Key_Up, QString(), kAlt), QByteArray("\x1b[1;3A"));
    checkEncode(key(Qt::Key_Up, QString(), ctrl()), QByteArray("\x1b[1;5A"));
}

void TestInput::arrowCombinedModifiers()
{
    checkEncode(key(Qt::Key_Up, QString(), ctrl() | kShift), QByteArray("\x1b[1;6A"));
    checkEncode(key(Qt::Key_Up, QString(), ctrl() | kAlt), QByteArray("\x1b[1;7A"));
}

void TestInput::homeEndPlainAndModifier()
{
    checkEncode(key(Qt::Key_Home, QString()), QByteArray("\x1b[H"));
    checkEncode(key(Qt::Key_End, QString()), QByteArray("\x1b[F"));
    checkEncode(key(Qt::Key_Home, QString(), ctrl()), QByteArray("\x1b[1;5H"));
}

// -- Page keys: viewport policy (spec Q8) --------------------------------

void TestInput::pgupScrollsViewportWhenHistoryExists()
{
    checkEncodeNone(key(Qt::Key_PageUp, QString()), /*decCkm=*/false, /*scrollbackLen=*/5);
}

void TestInput::pgupSendsCsiWhenNoHistory()
{
    checkEncode(key(Qt::Key_PageUp, QString()), QByteArray("\x1b[5~"),
                /*decCkm=*/false, /*scrollbackLen=*/0);
    checkEncode(key(Qt::Key_PageDown, QString()), QByteArray("\x1b[6~"),
                /*decCkm=*/false, /*scrollbackLen=*/0);
}

void TestInput::pgdnScrollsViewportWhenHistoryExists()
{
    checkEncodeNone(key(Qt::Key_PageDown, QString()), /*decCkm=*/false, /*scrollbackLen=*/3);
}

// -- Paste and copy -------------------------------------------------------

void TestInput::pasteShortcutsReturnNone()
{
    checkEncodeNone(key(Qt::Key_V, QStringLiteral("\x16"), ctrl() | kShift));
    checkEncodeNone(key(Qt::Key_Insert, QString(), kShift));
}

void TestInput::copyShortcutIsNoop()
{
    checkEncodeNone(key(Qt::Key_C, QStringLiteral("\x03"), ctrl() | kShift));
}

void TestInput::encodePastePlainAndBracketed()
{
    QCOMPARE(InputEncoder::encodePaste(QStringLiteral("hi")), QByteArray("hi"));
    QCOMPARE(InputEncoder::encodePaste(QStringLiteral("héllo")),
             QStringLiteral("héllo").toUtf8());
    QCOMPARE(InputEncoder::encodePaste(QStringLiteral("hi"), /*bracketedPaste=*/true),
             QByteArray("\x1b[200~hi\x1b[201~"));
}

// -- macOS: Command never reaches the terminal ---------------------------

#ifdef Q_OS_MACOS
void TestInput::commandKeyNeverReachesTheTerminal()
{
    // Qt reports ⌘ as ControlModifier on macOS — it must never become a
    // control code (⌘+C is copy, not SIGINT).
    checkEncodeNone(key(Qt::Key_C, QStringLiteral("\x03"), Qt::ControlModifier));
    checkEncodeNone(key(Qt::Key_V, QString(), Qt::ControlModifier));
    checkEncodeNone(key(Qt::Key_Up, QString(), Qt::ControlModifier));
}

void TestInput::controlKeyIsMetaModifierOnMacos()
{
    // ⌃ is the real Ctrl on macOS (Qt: MetaModifier) → control codes.
    checkEncode(key(Qt::Key_C, QStringLiteral("\x03"), Qt::MetaModifier), QByteArray("\x03"));
    checkEncode(key(Qt::Key_Up, QString(), Qt::MetaModifier), QByteArray("\x1b[1;5A"));
}
#endif

// -- Mouse protocol encoders (DECSET ?1000 X10 / ?1006 SGR) --------------

void TestInput::x10MousePressEncodes32OffsetBytes()
{
    // CSI M + three bytes, each +32: button, column, row.
    QCOMPARE(InputEncoder::encodeMouseX10(3, 5, 0, InputEncoder::MouseAction::Press),
             QByteArray("\x1b[M") + bytes({32, 35, 37}));
}

void TestInput::x10MouseButtonsAndRelease()
{
    QCOMPARE(InputEncoder::encodeMouseX10(1, 1, 1, InputEncoder::MouseAction::Press),
             QByteArray("\x1b[M!!!"));  // middle
    QCOMPARE(InputEncoder::encodeMouseX10(1, 1, 2, InputEncoder::MouseAction::Press),
             QByteArray("\x1b[M\"!!"));  // right
    QCOMPARE(InputEncoder::encodeMouseX10(2, 2, 0, InputEncoder::MouseAction::Release),
             QByteArray("\x1b[M") + bytes({35, 34, 34}));
}

void TestInput::x10MouseMotionAdds32()
{
    QCOMPARE(InputEncoder::encodeMouseX10(2, 2, 0, InputEncoder::MouseAction::Motion),
             QByteArray("\x1b[M") + bytes({64, 34, 34}));
}

void TestInput::x10MouseModifierBits()
{
    // shift 4, meta/alt 8, ctrl 16 — summed into the button byte.
    QCOMPARE(InputEncoder::encodeMouseX10(1, 1, 0, InputEncoder::MouseAction::Press, /*mods=*/4),
             QByteArray("\x1b[M") + bytes({36, 33, 33}));
    QCOMPARE(InputEncoder::encodeMouseX10(1, 1, 0, InputEncoder::MouseAction::Press, /*mods=*/28),
             QByteArray("\x1b[M") + bytes({60, 33, 33}));
}

void TestInput::x10WheelUsesButtons4And5()
{
    // The X11 wheel convention xterm reports in legacy modes.
    QCOMPARE(InputEncoder::encodeMouseX10(1, 1, 0, InputEncoder::MouseAction::WheelUp),
             QByteArray("\x1b[M") + bytes({36, 33, 33}));
    QCOMPARE(InputEncoder::encodeMouseX10(1, 1, 0, InputEncoder::MouseAction::WheelDown),
             QByteArray("\x1b[M") + bytes({37, 33, 33}));
}

void TestInput::x10WheelUsesShiftedModifierBits()
{
    // Wheel buttons 4/5 leave no room for the standard modifier bits —
    // the xterm mapping: shift→2, ctrl→4, meta→8.
    QCOMPARE(InputEncoder::encodeMouseX10(1, 1, 0, InputEncoder::MouseAction::WheelUp, /*mods=*/4),
             QByteArray("\x1b[M") + bytes({38, 33, 33}));
    QCOMPARE(InputEncoder::encodeMouseX10(1, 1, 0, InputEncoder::MouseAction::WheelDown, /*mods=*/16),
             QByteArray("\x1b[M") + bytes({41, 33, 33}));
    QCOMPARE(InputEncoder::encodeMouseX10(1, 1, 0, InputEncoder::MouseAction::WheelUp, /*mods=*/8),
             QByteArray("\x1b[M") + bytes({44, 33, 33}));
}

void TestInput::sgrMousePressLeft()
{
    QCOMPARE(InputEncoder::encodeSgrMouse(4, 7, 0, InputEncoder::MouseAction::Press),
             QByteArray("\x1b[<0;4;7M"));
}

void TestInput::sgrMouseReleaseUsesLowercaseM()
{
    QCOMPARE(InputEncoder::encodeSgrMouse(4, 7, 2, InputEncoder::MouseAction::Release),
             QByteArray("\x1b[<2;4;7m"));
}

void TestInput::sgrMouseMotionAdds32()
{
    QCOMPARE(InputEncoder::encodeSgrMouse(4, 7, 0, InputEncoder::MouseAction::Motion),
             QByteArray("\x1b[<32;4;7M"));
}

void TestInput::sgrMouseWheelUpDown()
{
    QCOMPARE(InputEncoder::encodeSgrMouse(4, 7, 0, InputEncoder::MouseAction::WheelUp),
             QByteArray("\x1b[<64;4;7M"));
    QCOMPARE(InputEncoder::encodeSgrMouse(4, 7, 0, InputEncoder::MouseAction::WheelDown),
             QByteArray("\x1b[<65;4;7M"));
}

void TestInput::sgrMouseWheelUsesShiftedModifierBits()
{
    // Shifted wheel is 66/67 — the standard shift bit (4) would collide
    // with the button code; the xterm mapping: shift→2, ctrl→4, meta→8.
    QCOMPARE(InputEncoder::encodeSgrMouse(4, 7, 0, InputEncoder::MouseAction::WheelUp, /*mods=*/4),
             QByteArray("\x1b[<66;4;7M"));
    QCOMPARE(InputEncoder::encodeSgrMouse(4, 7, 0, InputEncoder::MouseAction::WheelDown, /*mods=*/4),
             QByteArray("\x1b[<67;4;7M"));
    QCOMPARE(InputEncoder::encodeSgrMouse(4, 7, 0, InputEncoder::MouseAction::WheelUp, /*mods=*/16),
             QByteArray("\x1b[<68;4;7M"));
    QCOMPARE(InputEncoder::encodeSgrMouse(4, 7, 0, InputEncoder::MouseAction::WheelUp, /*mods=*/4 | 16),
             QByteArray("\x1b[<70;4;7M"));
}

void TestInput::sgrMouseModifiersSumIntoButton()
{
    QCOMPARE(InputEncoder::encodeSgrMouse(4, 7, 0, InputEncoder::MouseAction::Press, /*mods=*/4),
             QByteArray("\x1b[<4;4;7M"));
    QCOMPARE(InputEncoder::encodeSgrMouse(4, 7, 0, InputEncoder::MouseAction::Press, /*mods=*/4 | 8 | 16),
             QByteArray("\x1b[<28;4;7M"));
    QCOMPARE(InputEncoder::encodeSgrMouse(4, 7, 0, InputEncoder::MouseAction::Motion, /*mods=*/16),
             QByteArray("\x1b[<48;4;7M"));
}

QTEST_MAIN(TestInput)
#include "test_input.moc"
