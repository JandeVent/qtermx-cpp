// T05 — Cursor motion and addressing (clamp, not scroll) (port of
// tests/screen/test_motion.py). CUU/CUD clamp at the region top/bottom
// when the cursor is inside the region, at the screen edges when
// outside it; CUF clamps at the last column, CUB at 0; CNL/CPL combine
// the vertical move with a return to column 0; CUP/HVP are 1-based
// absolute addressing, region-relative under origin mode (DECOM); IND/
// NEL/RI scroll the region at its edges. All motions clear a pending
// wrap.
#include "harness.h"
#include "test_pipeline.h"

using namespace qtermx;
using namespace qtermx::test;

TEST_CASE(motion_cuu_clamps_at_region_top)
{
    Screen& screen = feedTo(U"\x1B[3;5r\x1B[4B\x1B[5A", 6, 4);
    // CUD 4 from (0,0) → y=4; CUU 5 → clamped at region top (2)
    QTERMX_CHECK(screen.cursor.x == 0);
    QTERMX_CHECK(screen.cursor.y == 2);
}

TEST_CASE(motion_cud_clamps_at_region_bottom)
{
    Screen& screen = feedTo(U"\x1B[3;5r\x1B[2B\x1B[3B", 6, 4);
    // CUD 2 → y=2 (region top); CUD 3 → clamped at region bottom (4)
    QTERMX_CHECK(screen.cursor.y == 4);
}

TEST_CASE(motion_cud_below_region_moves_to_screen_bottom)
{
    Screen& screen = feedTo(U"\x1B[3;5r\x1B[6;1H\x1B[2B", 6, 4);
    // CUP to (0,5) — below the region; CUD 2 moves freely, clamped at rows-1
    QTERMX_CHECK(screen.cursor.x == 0);
    QTERMX_CHECK(screen.cursor.y == 5);
}

TEST_CASE(motion_cuf_cub_clamp_at_screen_edges)
{
    Screen& screen = feedTo(U"a\x1B[3C\x1B[5D\x1B[1D", 2, 4);
    // CUF 3 → x=3; CUB 5 → x=0; CUB 1 → still 0
    QTERMX_CHECK(screen.cursor.x == 0);
    QTERMX_CHECK(screen.cursor.y == 0);
}

TEST_CASE(motion_cuf_clamps_at_last_column)
{
    Screen& screen = feedTo(U"\x1B[10C", 2, 4);
    QTERMX_CHECK(screen.cursor.x == 3);
}

TEST_CASE(motion_cnl_cpl_move_lines_and_to_column_zero)
{
    Screen& screen = feedTo(U"ab\x1B[2E\x1B[F", 6, 4);
    // ab → (2,0); CNL 2 → (0,2); CPL 1 → (0,1)
    QTERMX_CHECK(screen.cursor.x == 0);
    QTERMX_CHECK(screen.cursor.y == 1);
}

TEST_CASE(motion_cup_single_param_moves_to_column_zero)
{
    Screen& screen = feedTo(U"\x1B[5;2H\x1B[3H", 6, 4);
    // CUP 5;2 → (1,4); CUP 3 (one parameter) → (0,2)
    QTERMX_CHECK(screen.cursor.x == 0);
    QTERMX_CHECK(screen.cursor.y == 2);
}

TEST_CASE(motion_cup_defaults_to_home)
{
    Screen& screen = feedTo(U"\x1B[2;2H\x1B[H", 6, 4);
    QTERMX_CHECK(screen.cursor.x == 0);
    QTERMX_CHECK(screen.cursor.y == 0);
}

TEST_CASE(motion_cup_clamps)
{
    Screen& screen = feedTo(U"\x1B[99;99H", 6, 4);
    QTERMX_CHECK(screen.cursor.x == 3);
    QTERMX_CHECK(screen.cursor.y == 5);
}

TEST_CASE(motion_hvp_is_cup_alias)
{
    Screen& screen = feedTo(U"\x1B[3;4f", 6, 4);
    QTERMX_CHECK(screen.cursor.x == 3);
    QTERMX_CHECK(screen.cursor.y == 2);
}

TEST_CASE(motion_cup_origin_relative_to_region_top)
{
    Screen& screen = feedTo(U"\x1B[3;5r\x1B[?6h\x1B[2;2H", 6, 4);
    // region [2,4]; CUP 2;2 → (1, scroll_top + 1) = (1, 3)
    QTERMX_CHECK(screen.cursor.x == 1);
    QTERMX_CHECK(screen.cursor.y == 3);
}

TEST_CASE(motion_cha_moves_to_column_keeping_row)
{
    Screen& screen = feedTo(U"ab\x1B[5Gc", 3, 6);
    // ab → (2,0); CHA 5 → (4,0); c written there
    QTERMX_CHECK(strip(splitLines(screen.render())[0]) == "ab  c");
    QTERMX_CHECK(screen.cursor.x == 5);
    QTERMX_CHECK(screen.cursor.y == 0);
}

TEST_CASE(motion_cha_defaults_to_column_zero)
{
    Screen& screen = feedTo(U"ab\x1B[G", 3, 6);
    QTERMX_CHECK(screen.cursor.x == 0);
    QTERMX_CHECK(screen.cursor.y == 0);
}

TEST_CASE(motion_cha_clamps_at_last_column)
{
    Screen& screen = feedTo(U"ab\x1B[99G", 3, 6);
    QTERMX_CHECK(screen.cursor.x == 5);
    QTERMX_CHECK(screen.cursor.y == 0);
}

TEST_CASE(motion_vpa_moves_to_row_keeping_column)
{
    Screen& screen = feedTo(U"ab\x1B[4dc", 6, 6);
    // ab → (2,0); VPA 4 → (2,3); c written at (3,3)
    QTERMX_CHECK(strip(splitLines(screen.render())[3]) == "c");
    QTERMX_CHECK(screen.cursor.x == 3);
    QTERMX_CHECK(screen.cursor.y == 3);
}

TEST_CASE(motion_vpa_defaults_to_row_zero)
{
    Screen& screen = feedTo(U"ab\x1B[3;1H\x1B[d", 6, 6);
    QTERMX_CHECK(screen.cursor.x == 0);
    QTERMX_CHECK(screen.cursor.y == 0);
}

TEST_CASE(motion_vpa_clamps_at_last_row)
{
    Screen& screen = feedTo(U"\x1B[99d", 6, 6);
    QTERMX_CHECK(screen.cursor.x == 0);
    QTERMX_CHECK(screen.cursor.y == 5);
}

TEST_CASE(motion_vpa_origin_relative_to_region_top)
{
    Screen& screen = feedTo(U"\x1B[3;5r\x1B[?6h\x1B[2d", 6, 4);
    // region [2,4]; VPA 2 → (0, scroll_top + 1) = (0, 3)
    QTERMX_CHECK(screen.cursor.x == 0);
    QTERMX_CHECK(screen.cursor.y == 3);
}

TEST_CASE(motion_origin_mode_set_homes_to_region_top)
{
    Screen& screen = feedTo(U"\x1B[3;5r\x1B[2;2H\x1B[?6h", 6, 4);
    QTERMX_CHECK(screen.cursor.x == 0);
    QTERMX_CHECK(screen.cursor.y == 2);
}

TEST_CASE(motion_origin_mode_reset_homes_to_screen_top)
{
    Screen& screen = feedTo(U"\x1B[3;5r\x1B[?6h\x1B[2;2H\x1B[?6l", 6, 4);
    QTERMX_CHECK(screen.cursor.x == 0);
    QTERMX_CHECK(screen.cursor.y == 0);
}

TEST_CASE(motion_ind_moves_down_keeping_column)
{
    Screen& screen = feedTo(U"ab\x1B" U"Dc", 2, 4);
    QTERMX_CHECK(screen.cursor.x == 3);
    QTERMX_CHECK(screen.cursor.y == 1);
}

TEST_CASE(motion_nel_returns_to_column_zero_and_moves_down)
{
    Screen& screen = feedTo(U"ab\x1B" U"Ec", 2, 4);
    QTERMX_CHECK(screen.cursor.x == 1);
    QTERMX_CHECK(screen.cursor.y == 1);
}

TEST_CASE(motion_nel_at_region_bottom_scrolls_region)
{
    // NEL is CR + index (xterm.js nextLine): at the region bottom the
    // index scrolls — unlike CNL, whose clamped motion just stops.
    Screen& screen = feedTo(U"\x1B[2;4r" "a\r\nb\r\nc\x1B[4;1H" "d\x1B" "E", 5, 4);
    const auto rows = splitLines(screen.render());
    QTERMX_CHECK(strip(rows[0]) == "a"); // above the region: untouched
    QTERMX_CHECK(rows[1][0] == 'c'); // region scrolled up
    QTERMX_CHECK(rows[2][0] == 'd');
    QTERMX_CHECK(rows[3].find_first_not_of(' ') == std::string::npos); // fresh blank
    QTERMX_CHECK(screen.cursor.x == 0);
    QTERMX_CHECK(screen.cursor.y == 3);
}

TEST_CASE(motion_nel_preserves_wrapped_marker)
{
    // NEL is CR + index; index does not clear the wrapped marker
    // (xterm.js nextLine → index), unlike LF which clears it.
    Screen& screen = feedTo(U"abcd\x1B[1;1H\x1B" U"E", 4, 2);
    // "abcd" wraps: row 1 holds "cd" with the wrapped marker; the NEL
    // lands on it via index, which leaves the marker alone.
    QTERMX_CHECK(screen.line(1).wrapped);
}

TEST_CASE(motion_ri_moves_up_keeping_column)
{
    Screen& screen = feedTo(U"a\nb\x1BM", 2, 4);
    // 'b' sits at column 1 (LF keeps the column); RI moves up, keeping it
    QTERMX_CHECK(screen.cursor.x == 2);
    QTERMX_CHECK(screen.cursor.y == 0);
}

TEST_CASE(motion_ri_at_region_top_scrolls_region_down)
{
    Screen& screen = feedTo(U"\x1B[3;5r" "a\r\nb\r\nc\r\nd\r\ne\x1B[2A\x1BM", 6, 4);
    const auto rows = splitLines(screen.render());
    QTERMX_CHECK(rows[0][0] == 'a'); // above the region: untouched
    QTERMX_CHECK(rows[1][0] == 'b');
    QTERMX_CHECK(rows[2].find_first_not_of(' ') == std::string::npos); // fresh blank
    QTERMX_CHECK(rows[3][0] == 'c'); // region shifted down
    QTERMX_CHECK(rows[4][0] == 'd');
}

TEST_CASE(motion_clears_pending_wrap)
{
    Screen& screen = feedTo(U"aaaa\x1B[1A", 2, 4);
    QTERMX_CHECK(!screen.cursor.pending_wrap);
    QTERMX_CHECK(screen.cursor.x == 3);
    QTERMX_CHECK(screen.cursor.y == 0);
}