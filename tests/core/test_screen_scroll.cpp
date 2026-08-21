// T11 — Scrolling and row ops: SU/SD scroll the region; IL/DL insert
// and delete lines; ICH/DCH insert and delete characters (port of
// tests/screen/test_scroll.py). Fills follow xterm.js verbatim: lines
// scrolled in by LF/RI/SU/IL/DL and rows reset by ED carry the erase
// fill (default fg, cursor's bg); SD alone fills its fresh top line
// with default attributes.
#include "harness.h"
#include "test_pipeline.h"

using namespace qtermx;
using namespace qtermx::test;

// -- SU: scroll up ------------------------------------------------------

TEST_CASE(scroll_su_scrolls_region_up)
{
    Screen& screen = feedTo(U"1\r\n2\r\n3\r\n4\x1B[S", 4, 4);
    const auto rows = splitLines(screen.render());
    QTERMX_CHECK(strip(rows[0]) == "2");
    QTERMX_CHECK(strip(rows[1]) == "3");
    QTERMX_CHECK(strip(rows[2]) == "4");
    QTERMX_CHECK(strip(rows[3]).empty());
}

TEST_CASE(scroll_su_in_narrowed_region)
{
    Screen& screen = feedTo(U"1\r\n2\r\n3\r\n4\r\n5\x1B[2;4r\x1B[2S", 5, 4);
    const auto rows = splitLines(screen.render());
    QTERMX_CHECK(strip(rows[0]) == "1");
    QTERMX_CHECK(strip(rows[1]) == "4");
    QTERMX_CHECK(strip(rows[2]).empty());
    QTERMX_CHECK(strip(rows[3]).empty());
    QTERMX_CHECK(strip(rows[4]) == "5");
}

TEST_CASE(scroll_su_larger_than_region_blanks_it)
{
    Screen& screen = feedTo(U"1\r\n2\r\n3\x1B[2;4r\x1B[9S", 4, 4);
    const auto rows = splitLines(screen.render());
    QTERMX_CHECK(strip(rows[0]) == "1");
    QTERMX_CHECK(strip(rows[1]).empty());
    QTERMX_CHECK(strip(rows[2]).empty());
    QTERMX_CHECK(strip(rows[3]).empty());
}

TEST_CASE(scroll_su_keeps_cursor_position)
{
    Screen& screen = feedTo(U"abc\x1B[4;2H\x1B[S", 4, 4);
    QTERMX_CHECK(screen.cursor.x == 1);
    QTERMX_CHECK(screen.cursor.y == 3);
}

TEST_CASE(scroll_su_wrapped_marker_rides_with_row)
{
    Screen& screen = feedTo(U"xxxxxx\x1B[2;2H\x1B[S", 3, 5);
    // the wrapped "x" row scrolls up to row 0, keeping its marker
    QTERMX_CHECK(screen.line(0).wrapped);
    QTERMX_CHECK(screen.line(0).cells[0].data == U"x");
}

// -- SD: scroll down ----------------------------------------------------

TEST_CASE(scroll_sd_scrolls_region_down)
{
    Screen& screen = feedTo(U"1\r\n2\r\n3\r\n4\x1B[T", 4, 4);
    const auto rows = splitLines(screen.render());
    QTERMX_CHECK(strip(rows[0]).empty());
    QTERMX_CHECK(strip(rows[1]) == "1");
    QTERMX_CHECK(strip(rows[2]) == "2");
    QTERMX_CHECK(strip(rows[3]) == "3");
}

TEST_CASE(scroll_sd_in_narrowed_region)
{
    Screen& screen = feedTo(U"1\r\n2\r\n3\r\n4\r\n5\x1B[2;4r\x1B[T", 5, 4);
    const auto rows = splitLines(screen.render());
    QTERMX_CHECK(strip(rows[0]) == "1");
    QTERMX_CHECK(strip(rows[1]).empty());
    QTERMX_CHECK(strip(rows[2]) == "2");
    QTERMX_CHECK(strip(rows[3]) == "3");
    QTERMX_CHECK(strip(rows[4]) == "5");
}

// -- IL: insert lines ---------------------------------------------------

TEST_CASE(scroll_il_inserts_blank_line_at_cursor)
{
    Screen& screen = feedTo(U"1\r\n2\r\n3\r\n4\x1B[2;2H\x1B[L", 4, 4);
    const auto rows = splitLines(screen.render());
    QTERMX_CHECK(strip(rows[0]) == "1");
    QTERMX_CHECK(strip(rows[1]).empty());
    QTERMX_CHECK(strip(rows[2]) == "2");
    QTERMX_CHECK(strip(rows[3]) == "3");
}

TEST_CASE(scroll_il_outside_region_is_noop)
{
    Screen& screen = feedTo(U"1\r\n2\r\n3\r\n4\x1B[2;4r\x1B[L", 4, 4);
    const auto rows = splitLines(screen.render());
    QTERMX_CHECK(strip(rows[0]) == "1");
    QTERMX_CHECK(strip(rows[1]) == "2");
    QTERMX_CHECK(strip(rows[2]) == "3");
    QTERMX_CHECK(strip(rows[3]) == "4");
}

TEST_CASE(scroll_il_below_region_untouched)
{
    Screen& screen = feedTo(U"1\r\n2\r\n3\r\n4\r\n5\x1B[2;4r\x1B[2;2H\x1B[L", 5, 4);
    const auto rows = splitLines(screen.render());
    QTERMX_CHECK(strip(rows[0]) == "1");
    QTERMX_CHECK(strip(rows[1]).empty());
    QTERMX_CHECK(strip(rows[2]) == "2");
    QTERMX_CHECK(strip(rows[3]) == "3");
    QTERMX_CHECK(strip(rows[4]) == "5");
}

TEST_CASE(scroll_il_returns_cursor_to_column_zero)
{
    Screen& screen = feedTo(U"a\x1B[2;3H\x1B[L", 4, 4);
    QTERMX_CHECK(screen.cursor.x == 0);
    QTERMX_CHECK(screen.cursor.y == 1);
}

TEST_CASE(scroll_il_inserts_unwrapped_line)
{
    Screen& screen = feedTo(U"xxxxxx\x1B[2;2H\x1B[L", 3, 5);
    QTERMX_CHECK(!screen.line(1).wrapped);
    // the wrapped "x" row moved down, marker intact
    QTERMX_CHECK(screen.line(2).wrapped);
}

TEST_CASE(scroll_il_multiple_lines)
{
    Screen& screen = feedTo(U"1\r\n2\r\n3\r\n4\r\n5\x1B[3;3H\x1B[3L", 5, 4);
    const auto rows = splitLines(screen.render());
    QTERMX_CHECK(strip(rows[0]) == "1");
    QTERMX_CHECK(strip(rows[1]) == "2");
    QTERMX_CHECK(strip(rows[2]).empty());
    QTERMX_CHECK(strip(rows[3]).empty());
    QTERMX_CHECK(strip(rows[4]).empty());
}

// -- DL: delete lines ---------------------------------------------------

TEST_CASE(scroll_dl_deletes_line_at_cursor)
{
    Screen& screen = feedTo(U"1\r\n2\r\n3\r\n4\x1B[2;2H\x1B[M", 4, 4);
    const auto rows = splitLines(screen.render());
    QTERMX_CHECK(strip(rows[0]) == "1");
    QTERMX_CHECK(strip(rows[1]) == "3");
    QTERMX_CHECK(strip(rows[2]) == "4");
    QTERMX_CHECK(strip(rows[3]).empty());
}

TEST_CASE(scroll_dl_outside_region_is_noop)
{
    Screen& screen = feedTo(U"1\r\n2\r\n3\r\n4\x1B[2;4r\x1B[M", 4, 4);
    const auto rows = splitLines(screen.render());
    QTERMX_CHECK(strip(rows[0]) == "1");
    QTERMX_CHECK(strip(rows[1]) == "2");
    QTERMX_CHECK(strip(rows[2]) == "3");
    QTERMX_CHECK(strip(rows[3]) == "4");
}

TEST_CASE(scroll_dl_returns_cursor_to_column_zero)
{
    Screen& screen = feedTo(U"a\x1B[2;3H\x1B[M", 4, 4);
    QTERMX_CHECK(screen.cursor.x == 0);
    QTERMX_CHECK(screen.cursor.y == 1);
}

TEST_CASE(scroll_dl_multiple_lines)
{
    Screen& screen = feedTo(U"1\r\n2\r\n3\r\n4\r\n5\x1B[3;3H\x1B[2M", 5, 4);
    const auto rows = splitLines(screen.render());
    QTERMX_CHECK(strip(rows[0]) == "1");
    QTERMX_CHECK(strip(rows[1]) == "2");
    QTERMX_CHECK(strip(rows[2]) == "5");
    QTERMX_CHECK(strip(rows[3]).empty());
    QTERMX_CHECK(strip(rows[4]).empty());
}

// -- ICH: insert characters ---------------------------------------------

TEST_CASE(scroll_ich_shifts_row_right)
{
    Screen& screen = feedTo(U"ab\x1B[1;2H\x1B[@", 2, 4);
    QTERMX_CHECK(strip(splitLines(screen.render())[0]) == "a b");
    // the cursor does not move (xterm.js insertChars)
    QTERMX_CHECK(screen.cursor.x == 1);
}

TEST_CASE(scroll_ich_multiple_cells)
{
    Screen& screen = feedTo(U"abcd\x1B[1;2H\x1B[2@", 2, 4);
    QTERMX_CHECK(strip(splitLines(screen.render())[0]) == "a  b");
}

TEST_CASE(scroll_ich_blanks_wide_lead_split_by_insertion)
{
    Screen& screen = feedTo(U"中\x1B[1;2H\x1B[@", 2, 4);
    const Row& row = screen.line(0);
    QTERMX_CHECK(row.cells[0].data == U" "); // the split wide lead is blanked
    QTERMX_CHECK(row.cells[1].data == U" ");
    QTERMX_CHECK(row.cells[2].data.empty()); // the orphaned continuation survives
}

// -- DCH: delete characters ---------------------------------------------

TEST_CASE(scroll_dch_shifts_row_left)
{
    Screen& screen = feedTo(U"abcd\x1B[1;3H\x1B[P", 2, 4);
    QTERMX_CHECK(strip(splitLines(screen.render())[0]) == "abd");
}

TEST_CASE(scroll_dch_multiple_cells)
{
    Screen& screen = feedTo(U"abcd\x1B[1;2H\x1B[2P", 2, 4);
    QTERMX_CHECK(strip(splitLines(screen.render())[0]) == "ad");
}

TEST_CASE(scroll_dch_deleting_a_cell_keeps_wide_char)
{
    Screen& screen = feedTo(U"中a\x1B[1;3H\x1B[P", 2, 4);
    QTERMX_CHECK(strip(splitLines(screen.render())[0]) == "中");
}

TEST_CASE(scroll_dch_deleting_wide_lead_blanks_continuation)
{
    Screen& screen = feedTo(U"中x\x1B[1;2H\x1B[P", 2, 4);
    const Row& row = screen.line(0);
    QTERMX_CHECK(row.cells[0].data == U" "); // the wide lead is gone
    QTERMX_CHECK(row.cells[1].data == U"x");
    QTERMX_CHECK(row.cells[2].data == U" "); // no orphaned continuation stub
}

// -- Fill colors of scrolled-in lines (xterm.js-verbatim) ----------------

TEST_CASE(scroll_su_scrolled_line_uses_erase_fill)
{
    Screen& screen = feedTo(U"a\r\nb\x1B[41m\x1B[S", 2, 4);
    QTERMX_CHECK(screen.line(1).cells[0].fg == -1);
    QTERMX_CHECK(screen.line(1).cells[0].bg == 1); // the cursor's bg at scroll time
}

TEST_CASE(scroll_sd_scrolled_line_uses_default_attrs)
{
    Screen& screen = feedTo(U"a\x1B[41m\x1B[T", 2, 4);
    QTERMX_CHECK(screen.line(0).cells[0].bg == -1); // SD alone: default attrs
}

TEST_CASE(scroll_lf_scrolled_line_uses_erase_fill)
{
    Screen& screen = feedTo(U"a\r\nb\x1B[41m\n", 2, 4);
    QTERMX_CHECK(screen.line(1).cells[0].bg == 1);
}

TEST_CASE(scroll_reverse_index_scrolled_line_uses_erase_fill)
{
    Screen& screen = feedTo(U"a\x1B[41m\x1B[1;1H\x1BM", 2, 4);
    QTERMX_CHECK(screen.line(0).cells[0].bg == 1);
}

TEST_CASE(scroll_il_inserted_line_uses_erase_fill)
{
    Screen& screen = feedTo(U"ab\x1B[41m\x1B[1;1H\x1B[L", 2, 4);
    QTERMX_CHECK(screen.line(0).cells[0].bg == 1);
}

TEST_CASE(scroll_dl_appended_line_uses_erase_fill)
{
    Screen& screen = feedTo(U"ab\x1B[41m\x1B[1;1H\x1B[M", 2, 4);
    QTERMX_CHECK(screen.line(1).cells[0].bg == 1);
}

TEST_CASE(scroll_ed_reset_rows_use_erase_fill)
{
    Screen& screen = feedTo(U"a\r\nb\x1B[41m\x1B[1;1H\x1B[0J", 2, 4);
    QTERMX_CHECK(screen.line(0).cells[0].bg == 1);
    QTERMX_CHECK(screen.line(1).cells[0].bg == 1);
}