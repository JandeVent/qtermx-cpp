// T03 — The first behavioral consumers of the mode registry (port of
// tests/screen/test_mode_behaviors.py). DECAWM off (?7 l): printing at
// the last column overwrites in place, a wide character that does not
// fit is dropped, and no pending wrap is set. IRM on (4 h): each
// printed character shifts the rest of the row right by its width. NLM
// on (20 h): line feed also returns the cursor to column 0.
#include "harness.h"
#include "test_pipeline.h"

using namespace qtermx;
using namespace qtermx::test;

TEST_CASE(mode_behaviors_decawm_off_overwrites_at_last_column)
{
    Screen& screen = feedTo(U"\x1B[?7l" "abcde", 2, 4);
    const auto rows = splitLines(screen.render());
    QTERMX_CHECK(strip(rows[0]) == "abce"); // "e" overwrote "d" in place
    QTERMX_CHECK(strip(rows[1]).empty());   // no wrap
    QTERMX_CHECK(screen.cursor.x == 3);
    QTERMX_CHECK(screen.cursor.y == 0);
}

TEST_CASE(mode_behaviors_decawm_off_drops_wide_char_at_last_column)
{
    Screen& screen = feedTo(U"\x1B[?7l" "abc中", 2, 4);
    QTERMX_CHECK(strip(splitLines(screen.render())[0]) == "abc"); // 中 dropped
    QTERMX_CHECK(screen.cursor.x == 3);
    QTERMX_CHECK(screen.cursor.y == 0);
}

TEST_CASE(mode_behaviors_decawm_default_wraps)
{
    Screen& screen = feedTo(U"abcde", 2, 4);
    const auto rows = splitLines(screen.render());
    QTERMX_CHECK(strip(rows[0]) == "abcd");
    QTERMX_CHECK(strip(rows[1]) == "e");
}

TEST_CASE(mode_behaviors_irm_shifts_row_right_on_print)
{
    Screen& screen = feedTo(U"\x1B[4h" "ab\rXY", 2, 5);
    QTERMX_CHECK(strip(splitLines(screen.render())[0]) == "XYab");
}

TEST_CASE(mode_behaviors_irm_drops_trailing_cells)
{
    Screen& screen = feedTo(U"\x1B[4h" "abcde\rZ", 2, 5);
    QTERMX_CHECK(strip(splitLines(screen.render())[0]) == "Zabcd"); // "e" fell off
}

TEST_CASE(mode_behaviors_irm_shifts_wide_char_pair)
{
    Screen& screen = feedTo(U"\x1B[4h" "中\rX", 2, 6);
    QTERMX_CHECK(strip(splitLines(screen.render())[0]) == "X中");
}

TEST_CASE(mode_behaviors_irm_blanks_wide_lead_landing_on_last_cell)
{
    Screen& screen = feedTo(U"\x1B[4h" "abc中\rZ", 2, 5);
    QTERMX_CHECK(strip(splitLines(screen.render())[0]) == "Zabc"); // 中 blanked
}

TEST_CASE(mode_behaviors_irm_combining_marks_do_not_shift)
{
    Screen& screen = feedTo(U"\x1B[4h" "a\u0301b", 2, 5);
    QTERMX_CHECK(strip(splitLines(screen.render())[0]) == "a\u0301b");
}

TEST_CASE(mode_behaviors_nlm_lf_returns_to_column_zero)
{
    Screen& screen = feedTo(U"\x1B[20h" "a\nb", 2, 5);
    QTERMX_CHECK(strip(splitLines(screen.render())[1]) == "b");
}

TEST_CASE(mode_behaviors_nlm_off_lf_keeps_column)
{
    Screen& screen = feedTo(U"\x1B[20h\x1B[20l" "a\nb", 2, 5);
    QTERMX_CHECK(strip(splitLines(screen.render())[1]) == "b"); // "b" at column 1
    QTERMX_CHECK(screen.cursor.x == 2);
    QTERMX_CHECK(screen.cursor.y == 1);
}