// T06 — Erase: ED (CSI n J), EL (CSI n K), ECH (CSI n X) (port of
// tests/screen/test_erase.py). Erase fill = default foreground, the
// cursor's current background, no attributes. Wrapped markers: a
// full-row erase clears them — EL 2 always, EL 0 only from column 0,
// ED 0 from column 0, ED 1 always on the current row (plus the next
// row when the whole row was erased), ED 2 resets every row. EL 1 and
// ECH never clear the marker.
#include "harness.h"
#include "test_pipeline.h"

using namespace qtermx;
using namespace qtermx::test;

TEST_CASE(erase_el_0_erases_to_end_of_row)
{
    Screen& screen = feedTo(U"abcd\x1B[2D\x1B[0K", 2, 6);
    QTERMX_CHECK(strip(splitLines(screen.render())[0]) == "ab");
}

TEST_CASE(erase_el_1_erases_to_cursor_inclusive)
{
    Screen& screen = feedTo(U"abcd\x1B[2D\x1B[1K", 2, 6);
    QTERMX_CHECK(strip(splitLines(screen.render())[0]) == "d");
}

TEST_CASE(erase_el_2_erases_whole_row)
{
    Screen& screen = feedTo(U"abcd\x1B[2K", 2, 6);
    QTERMX_CHECK(strip(splitLines(screen.render())[0]).empty());
}

TEST_CASE(erase_ech_erases_n_cells_from_cursor)
{
    Screen& screen = feedTo(U"abcd\x1B[2D\x1B[2X", 2, 6);
    QTERMX_CHECK(strip(splitLines(screen.render())[0]) == "ab");
}

TEST_CASE(erase_ech_clamps_to_row_end)
{
    Screen& screen = feedTo(U"ab\x1B[9X", 2, 6);
    QTERMX_CHECK(strip(splitLines(screen.render())[0]) == "ab");
}

TEST_CASE(erase_ed_0_erases_from_cursor_down)
{
    Screen& screen = feedTo(U"a\nb\nc\nd\x1B[1;2H\x1B[0J", 4, 4);
    const auto rows = splitLines(screen.render());
    QTERMX_CHECK(strip(rows[0]) == "a");
    QTERMX_CHECK(strip(rows[1]).empty());
    QTERMX_CHECK(strip(rows[2]).empty());
    QTERMX_CHECK(strip(rows[3]).empty());
}

TEST_CASE(erase_ed_1_erases_from_top_to_cursor)
{
    Screen& screen = feedTo(U"a\nb\nc\nd\x1B[3;4H\x1B[1J", 4, 4);
    const auto rows = splitLines(screen.render());
    QTERMX_CHECK(strip(rows[0]).empty());
    QTERMX_CHECK(strip(rows[1]).empty());
    QTERMX_CHECK(strip(rows[2]).empty());
    QTERMX_CHECK(strip(rows[3]) == "d"); // "d" sits at column 3 (LF keeps x)
}

TEST_CASE(erase_ed_2_erases_everything)
{
    Screen& screen = feedTo(U"a\nb\x1B[2J", 4, 4);
    QTERMX_CHECK(strip(screen.render()).empty());
}

TEST_CASE(erase_fill_uses_cursor_background)
{
    Screen& screen = feedTo(U"\x1B[44mX\x1B[2K", 2, 6);
    const Cell& cell = screen.line(0).cells[2];
    QTERMX_CHECK(cell.fg == -1);
    QTERMX_CHECK(cell.bg == 4);
}

TEST_CASE(erase_el_2_clears_wrapped_marker)
{
    Screen& screen = feedTo(U"xxxxxx\x1B[2K", 2, 5);
    QTERMX_CHECK(!screen.line(1).wrapped);
}

TEST_CASE(erase_ed_0_from_column_zero_clears_marker)
{
    Screen& screen = feedTo(U"xxxxxx\r\x1B[0J", 2, 5);
    // CR puts the cursor at column 0 of the wrapped row; ED 0 from
    // column 0 erases the whole row and clears its marker
    QTERMX_CHECK(!screen.line(1).wrapped);
}

TEST_CASE(erase_el_0_from_mid_row_keeps_marker)
{
    Screen& screen = feedTo(U"xxxxxx\x1B[2C\x1B[0K", 2, 5);
    QTERMX_CHECK(screen.line(1).wrapped);
}

TEST_CASE(erase_el_1_keeps_wrapped_marker)
{
    Screen& screen = feedTo(U"xxxxxx\x1B[1K", 2, 5);
    QTERMX_CHECK(screen.line(1).wrapped);
}

TEST_CASE(erase_ech_keeps_wrapped_marker)
{
    Screen& screen = feedTo(U"xxxxxx\x1B[1X", 2, 5);
    QTERMX_CHECK(screen.line(1).wrapped);
}