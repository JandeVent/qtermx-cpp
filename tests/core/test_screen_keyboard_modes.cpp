// Phase 4 — Keyboard modes `?1` (DECCKM) and `?2004` (bracketed paste)
// (port of tests/screen/test_keyboard_modes.py). The DEC private mode
// registry stores any private number generically; the GUI's input path
// consults these two through the existing mode(n, private=True) read
// seam. This pins that set/reset through the emulator works and that
// the read surface reports the mode.
#include "harness.h"
#include "test_pipeline.h"

using namespace qtermx;
using namespace qtermx::test;

namespace {
constexpr int kDecckm = 1;
constexpr int kBracketedPaste = 2004;
} // namespace

TEST_CASE(keyboard_modes_decckm_set_and_reset)
{
    Screen& screen = feedTo(U"\x1B[?1h", 3, 4);
    QTERMX_CHECK(screen.mode(kDecckm, true));
    Screen& screen2 = feedTo(U"\x1B[?1h\x1B[?1l", 3, 4);
    QTERMX_CHECK(!screen2.mode(kDecckm, true));
}

TEST_CASE(keyboard_modes_bracketed_paste_set_and_reset)
{
    Screen& screen = feedTo(U"\x1B[?2004h", 3, 4);
    QTERMX_CHECK(screen.mode(kBracketedPaste, true));
    Screen& screen2 = feedTo(U"\x1B[?2004h\x1B[?2004l", 3, 4);
    QTERMX_CHECK(!screen2.mode(kBracketedPaste, true));
}

TEST_CASE(keyboard_modes_default_off)
{
    Screen& screen = feedTo(U"", 3, 4);
    QTERMX_CHECK(!screen.mode(kDecckm, true));
    QTERMX_CHECK(!screen.mode(kBracketedPaste, true));
}

TEST_CASE(keyboard_modes_do_not_disturb_screen_behavior)
{
    // Setting the keyboard modes must not move the cursor or touch the
    // grid — they are keyboard-side state only.
    Screen& screen = feedTo(U"abc\x1B[?1h\x1B[?2004h", 3, 4);
    QTERMX_CHECK(screen.cursor.x == 3);
    QTERMX_CHECK(screen.cursor.y == 0);
    QTERMX_CHECK(strip(splitLines(screen.render())[0]) == "abc");
}