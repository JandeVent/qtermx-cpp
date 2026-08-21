// T9 — Tab stops: a set seeded every 8, mutable via HTS/TBC, consulted
// by HT, navigated by CHT/CBT, reset to defaults by resize (port of
// tests/screen/test_tabs.py).
#include "harness.h"
#include "test_pipeline.h"

using namespace qtermx;
using namespace qtermx::test;

TEST_CASE(tabs_ht_advances_to_next_default_stop)
{
    Screen& screen = feedTo(U"a\tb");
    QTERMX_CHECK(screen.line(0).cells[0].data == U"a");
    QTERMX_CHECK(screen.line(0).cells[8].data == U"b");
}

TEST_CASE(tabs_ht_from_stop_goes_to_next_stop)
{
    Screen& screen = feedTo(U"\t\tX", 24, 24);
    QTERMX_CHECK(screen.line(0).cells[16].data == U"X"); // landed at stop 16
}

TEST_CASE(tabs_ht_at_last_column_clamps)
{
    Screen& screen = feedTo(U"xxxxxxxxxxxxxxxxxxxx\t", 24, 20);
    QTERMX_CHECK(screen.cursor.x == 19);
}

TEST_CASE(tabs_ht_at_wrap_position_keeps_pending_wrap)
{
    Screen& screen = feedTo(U"xxxxx\t", 2, 5);
    // after the 5th x the cursor sits with a pending wrap; a tab past
    // the last stop lands at the wrap position and keeps it — the next
    // char wraps (real xterm: cur_col = cols, wrap_pending survives)
    QTERMX_CHECK(screen.cursor.pending_wrap);
    QTERMX_CHECK(screen.cursor.x == 4);
}

TEST_CASE(tabs_ht_to_wrap_position_then_overwrite)
{
    Screen& screen = feedTo(U"xxxx\tY", 2, 5);
    // no pending wrap: the tab lands at the last column without one,
    // so the next char clamps and overwrites (real xterm)
    QTERMX_CHECK(screen.line(0).cells[4].data == U"Y");
    QTERMX_CHECK(screen.cursor.y == 0);
}

TEST_CASE(tabs_hts_sets_stop_at_cursor)
{
    Screen& screen = feedTo(U"x\x1B[1;4H\x1BH\x1B[1;1H\t", 24, 24);
    // HTS at column 3; HT from column 0 lands there
    QTERMX_CHECK(screen.cursor.x == 3);
}

TEST_CASE(tabs_tbc_0_clears_stop_at_cursor)
{
    Screen& screen = feedTo(U"x\x1B[1;4H\x1BH\x1B[0g\x1B[1;1H\tY", 24, 24);
    // stop at 3 cleared; HT falls through to the default stop at 8
    QTERMX_CHECK(screen.line(0).cells[8].data == U"Y");
}

TEST_CASE(tabs_tbc_3_clears_all_stops)
{
    Screen& screen = feedTo(U"\x1B[3g\tY", 24, 24);
    // no stops left: HT clamps at the last column
    QTERMX_CHECK(screen.cursor.x == 23);
}

TEST_CASE(tabs_tbc_2_is_unsupported_noop)
{
    Screen& screen = feedTo(U"\x1B[2g\tY", 24, 24);
    QTERMX_CHECK(screen.line(0).cells[8].data == U"Y"); // default stops intact
}

TEST_CASE(tabs_cht_moves_forward_n_stops)
{
    Screen& screen = feedTo(U"\x1B[2I", 24, 24);
    QTERMX_CHECK(screen.cursor.x == 16);
}

TEST_CASE(tabs_cbt_moves_backward_n_stops)
{
    Screen& screen = feedTo(U"\x1B[1;17H\x1B[Z", 24, 24);
    QTERMX_CHECK(screen.cursor.x == 8);
}

TEST_CASE(tabs_cbt_at_column_zero_stays)
{
    Screen& screen = feedTo(U"\x1B[Z", 24, 24);
    QTERMX_CHECK(screen.cursor.x == 0);
}

TEST_CASE(tabs_cbt_with_custom_stops)
{
    Screen& screen = feedTo(U"\x1B[1;4H\x1BH\x1B[1;12H\x1B[2Z", 24, 24);
    // stops at 3 and 11; from 11 back two stops → 3
    QTERMX_CHECK(screen.cursor.x == 3);
}

TEST_CASE(tabs_resize_reseeds_default_stops)
{
    Screen& screen = feedTo(U"x\x1B[1;4H\x1BH", 24, 24);
    screen.resize(24, 24);
    QTERMX_CHECK(screen.cursor.x == 3); // cursor untouched by resize reseed
    screen.carriageReturn();
    screen.tab();
    QTERMX_CHECK(screen.cursor.x == 8); // custom stop at 3 is gone
}