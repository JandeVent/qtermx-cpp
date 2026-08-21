// T10 — DECSC/DECRC: a single save slot for the cursor (position +
// rendition), the charset slots and active level, and the
// origin/wraparound modes. CSI `s`/`u` are aliases. Tab stops and the
// scroll region are not saved (port of tests/screen/test_save_restore.py).
#include "harness.h"
#include "test_pipeline.h"

using namespace qtermx;
using namespace qtermx::test;

TEST_CASE(save_restore_decsc_decrc_restores_position)
{
    Screen& screen = feedTo(U"abc\x1B" U"7\x1B[2;5H\x1B" U"8def", 4, 8);
    QTERMX_CHECK(screen.line(0).cells[3].data == U"d");
    QTERMX_CHECK(screen.line(0).cells[5].data == U"f");
}

TEST_CASE(save_restore_csi_s_u_are_aliases)
{
    Screen& screen = feedTo(U"abc\x1B[s\x1B[2;5H\x1B[uX", 4, 8);
    QTERMX_CHECK(screen.line(0).cells[3].data == U"X");
}

TEST_CASE(save_restore_decsc_decrc_restores_rendition)
{
    Screen& screen = feedTo(U"\x1B[1;31mX\x1B" U"7\x1B[0m\x1B" U"8Y", 2, 4);
    const Cell& cell = screen.line(0).cells[1];
    QTERMX_CHECK(cell.bold);
    QTERMX_CHECK(cell.fg == 1);
}

TEST_CASE(save_restore_decsc_decrc_restores_charset_level_and_slots)
{
    Screen& screen = feedTo(U"\x1B)0\x0E\x1B" U"7\x0F\x1B" U"8q", 2, 4);
    // G1 = line-drawing, SO made it active; after save + SI, restore
    // brings back the level (and the G1 slot)
    QTERMX_CHECK(screen.line(0).cells[0].data == U"\u2500");
}

TEST_CASE(save_restore_decsc_decrc_restores_origin_mode)
{
    Screen& screen = feedTo(U"\x1B[?6h\x1B" U"7\x1B[?6l\x1B" U"8", 4, 4);
    QTERMX_CHECK(screen.mode(6, true));
}

TEST_CASE(save_restore_decsc_decrc_restores_wraparound_mode)
{
    Screen& screen = feedTo(U"\x1B[?7l\x1B" U"7\x1B[?7h\x1B" U"8", 4, 4);
    QTERMX_CHECK(!screen.mode(7, true));
}

TEST_CASE(save_restore_without_save_is_noop)
{
    Screen& screen = feedTo(U"abc\x1B[2;2H\x1B" U"8X", 2, 4);
    QTERMX_CHECK(screen.line(1).cells[1].data == U"X"); // cursor stayed at (1, 1)
}

TEST_CASE(save_restore_clears_pending_wrap)
{
    Screen& screen = feedTo(U"xxxx\x1B" U"7X\x1B" U"8", 2, 4);
    QTERMX_CHECK(!screen.cursor.pending_wrap);
    QTERMX_CHECK(screen.cursor.x == 3);
    QTERMX_CHECK(screen.cursor.y == 0);
}

TEST_CASE(save_restore_clamps_after_resize)
{
    Screen& screen = feedTo(U"\x1B" U"7\x1B[20;30H", 24, 40);
    screen.resize(10, 20);
    screen.restoreState();
    QTERMX_CHECK(screen.cursor.x == 0);
    QTERMX_CHECK(screen.cursor.y == 0);
}

TEST_CASE(save_restore_clamps_into_region_under_origin)
{
    Screen& screen = feedTo(U"\x1B[?6h\x1B" U"7\x1B[2;5r\x1B[1;1H\x1B" U"8", 8, 8);
    // saved (0, 0) with DECOM on; restore re-applies the mode and
    // clamps the cursor up into the region top
    QTERMX_CHECK(screen.cursor.y == 1);
}

TEST_CASE(save_restore_tab_stops_not_saved)
{
    Screen& screen = feedTo(U"", 24, 24);
    screen.setCursor(3, 0);
    screen.setTabStop();
    screen.saveState();
    screen.clearTabStop(0); // remove only the custom stop at 3
    screen.restoreState();
    screen.carriageReturn();
    screen.tab();
    // had stops been saved, restore would re-add 3 and HT would land there
    QTERMX_CHECK(screen.cursor.x == 8);
}

TEST_CASE(save_restore_scroll_region_not_saved)
{
    Screen& screen = feedTo(U"", 8, 8);
    screen.setScrollRegion(2, 5);
    screen.saveState();
    screen.setScrollRegion(0, 7);
    screen.restoreState();
    QTERMX_CHECK(screen.scrollTop() == 0);
    QTERMX_CHECK(screen.scrollBottom() == 7);
}

TEST_CASE(save_restore_single_slot_no_stack)
{
    Screen& screen = feedTo(U"\x1B" U"7\x1B[2;2H\x1B" U"7\x1B" U"8X", 4, 8);
    // the second save overwrites the first: restore goes to (1, 1)
    QTERMX_CHECK(screen.line(1).cells[1].data == U"X");
}