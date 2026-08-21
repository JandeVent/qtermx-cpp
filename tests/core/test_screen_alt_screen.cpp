// T10 — Alternate screen (?47/?1047/?1048/?1049) — ADR-0004 (port of
// tests/screen/test_alt_screen.py). The semantics are xterm.js-verbatim;
// the first five tests are ported 1:1 from xterm.js InputHandler.test.ts
// (the "alt screen buffer" describe block) through the feed seam. What
// the tests pin:
//
// - 47/1047 switch to a fresh alternate screen and back, the cursor
//   position carrying both ways, the rendition shared;
// - 1048 saves/restores the cursor only — no switch;
// - 1049 saves the cursor, enters the alternate screen, and on exit
//   clears it and restores;
// - the DECSC slot is per-screen (a save inside the alt screen restores
//   there later, independently);
// - entry fills the alt screen with the erase fill (cursor's bg);
// - resize reflows both grids under ADR-0003.
#include "harness.h"
#include "test_pipeline.h"

using namespace qtermx;
using namespace qtermx::test;

TEST_CASE(alt_screen_47_switches_to_alt_and_back)
{
    // xterm.js: DECSET/DECRST 47 — JUNK goes to the alt buffer, TEST
    // lands back on the main buffer at the carried-back cursor, red.
    Screen& screen = feedTo(U"\x1B[?47h\r\n\x1B[31mJUNK\x1B[?47lTEST");
    QTERMX_CHECK(rowText(screen, 0) == "");
    QTERMX_CHECK(rowText(screen, 1) == "    TEST");
    QTERMX_CHECK(screen.line(1).cells[4].fg == 1); // red — rendition shared
}

TEST_CASE(alt_screen_flag_tracks_the_active_grid)
{
    // The `alt_screen` property — the widget's wheel policy input: the
    // alternate screen has no scrollback, so wheel pages the app.
    auto p = makePipeline();
    QTERMX_CHECK(!p->screen.altScreen());
    p->feed(U"\x1B[?1049h");
    QTERMX_CHECK(p->screen.altScreen());
    p->feed(U"\x1B[?1049l");
    QTERMX_CHECK(!p->screen.altScreen());
}

TEST_CASE(alt_screen_1047_switches_to_alt_and_back)
{
    // xterm.js: DECSET/DECRST 1047 — same as 47.
    Screen& screen = feedTo(U"\x1B[?1047h\r\n\x1B[31mJUNK\x1B[?1047lTEST");
    QTERMX_CHECK(rowText(screen, 0) == "");
    QTERMX_CHECK(rowText(screen, 1) == "    TEST");
    QTERMX_CHECK(screen.line(1).cells[4].fg == 1); // red
}

TEST_CASE(alt_screen_1048_saves_and_restores_cursor_only)
{
    // xterm.js: DECSET/DECRST 1048 — no switch: JUNK and TEST share the
    // main buffer; the restore brings back the default rendition.
    Screen& screen = feedTo(U"\x1B[?1048h\r\n\x1B[31mJUNK\x1B[?1048lTEST");
    QTERMX_CHECK(rowText(screen, 0) == "TEST");
    QTERMX_CHECK(rowText(screen, 1) == "JUNK");
    QTERMX_CHECK(screen.line(0).cells[0].fg == -1); // default — restored
    QTERMX_CHECK(screen.line(1).cells[0].fg == 1);  // red — untouched
}

TEST_CASE(alt_screen_1049_saves_and_restores_cursor_around_switch)
{
    // xterm.js: DECSET/DECRST 1049 — JUNK goes to the alt buffer; the
    // exit restores the saved cursor and rendition, so TEST lands at the
    // saved position in the main buffer, default.
    Screen& screen = feedTo(U"\x1B[?1049h\r\n\x1B[31mJUNK\x1B[?1049lTEST");
    QTERMX_CHECK(rowText(screen, 0) == "TEST");
    QTERMX_CHECK(rowText(screen, 1) == "");
    QTERMX_CHECK(screen.line(0).cells[0].fg == -1); // default — restored
}

TEST_CASE(alt_screen_1049_maintains_saved_cursor_for_alt_buffer)
{
    // xterm.js: the DECSC slot is per-screen — `CSI s` inside the alt
    // screen saves to the alt's own slot, and `CSI u` in a later alt
    // session restores it (position and rendition).
    auto p = makePipeline();
    p->feed(U"\x1B[?1049h\r\n\x1B[31m\x1B[s\x1B[?1049lTEST");
    QTERMX_CHECK(rowText(p->screen, 0) == "TEST");
    QTERMX_CHECK(p->screen.line(0).cells[0].fg == -1); // normal slot restored
    p->feed(U"\x1B[?1049h\x1B[uTEST");
    QTERMX_CHECK(rowText(p->screen, 1) == "TEST"); // the alt's own saved position
    QTERMX_CHECK(p->screen.line(1).cells[0].fg == 1); // the alt's own rendition
}

TEST_CASE(alt_screen_1049_clears_alt_buffer_with_erase_attributes)
{
    // xterm.js: entry fills the alt buffer with the erase fill — the
    // cursor's current background color (42 = green, palette 2).
    Screen& screen = feedTo(U"\x1B[42m\x1B[?1049h");
    QTERMX_CHECK(screen.line(20).cells[10].bg == 2);
}

TEST_CASE(alt_screen_fresh_on_entry_after_exit)
{
    // Clear-on-exit: content written to the alt screen is gone when the
    // screen is entered again.
    Screen& screen = feedTo(U"\x1B[?1047hJUNK\x1B[?1047l\x1B[?1047h");
    QTERMX_CHECK(rowText(screen, 0) == "");
}

TEST_CASE(alt_screen_cursor_position_carries_both_ways)
{
    // The cursor position travels with the switch: main (2,0) → alt
    // prints at (2,0) → exit carries (3,0) back to main.
    Screen& screen = feedTo(U"ab\x1B[?1047hX\x1B[?1047lY");
    QTERMX_CHECK(rowText(screen, 0) == "ab Y");
}

TEST_CASE(alt_screen_resize_reflows_both_grids)
{
    // ADR-0003 + ADR-0004: resize reflows the normal grid and the
    // alternate grid independently — wrapped rows re-join on the alt
    // grid too, and the main grid is preserved underneath. Content sits
    // at the bottom of each grid so it survives the shrink (the grid
    // keeps its newest rows).
    auto p = makePipeline(6, 10);
    p->feed(U"\r\n\r\n\r\nabc\r\ndef\x1B[?1047h\r\n\r\n\r\n\r\nuvwxyz");
    p->screen.resize(4, 5);
    // Still in the alt screen: its grid reflowed at the new width.
    QTERMX_CHECK(rowText(p->screen, 0) == "uvwxy");
    QTERMX_CHECK(!p->screen.line(0).wrapped);
    QTERMX_CHECK(rowText(p->screen, 1) == "z");
    QTERMX_CHECK(p->screen.line(1).wrapped);
    p->feed(U"\x1B[?1047l");
    const auto lines = splitLines(p->screen.render());
    QTERMX_CHECK(strip(lines[0]) == "abc");
    QTERMX_CHECK(strip(lines[1]) == "def");
    QTERMX_CHECK(strip(lines[2]) == "");
    QTERMX_CHECK(strip(lines[3]) == "");
}

TEST_CASE(alt_screen_redundant_enter_preserves_alt_content)
{
    // A DECSET while already in the alt screen is a no-op (xterm.js
    // activateAltBuffer early-returns): the alt content survives a
    // redundant or nested 47/1047/1049.
    Screen& screen = feedTo(U"\x1B[?47hJUNK\x1B[?47h\x1B[?1049h");
    QTERMX_CHECK(rowText(screen, 0) == "JUNK"); // not wiped by re-entry
}

TEST_CASE(alt_screen_leave_when_already_normal_is_a_noop)
{
    // DECRST on the normal screen does nothing (xterm.js
    // activateNormalBuffer early-returns).
    Screen& screen = feedTo(U"abc\x1B[?47l\x1B[?1049l");
    QTERMX_CHECK(rowText(screen, 0) == "abc");
}

TEST_CASE(alt_screen_1048_inside_alt_saves_the_alt_slot)
{
    // `?1048h` inside the alt screen saves to the alt's own DECSC slot,
    // which a later alt session restores (ADR-0004).
    auto p = makePipeline();
    p->feed(U"\x1B[?1049h\r\n\x1B[31m\x1B[?1048h\x1B[?1049l");
    p->feed(U"\x1B[?1049h\x1B[?1048lTEST");
    QTERMX_CHECK(rowText(p->screen, 1) == "TEST"); // the alt slot's position
    QTERMX_CHECK(p->screen.line(1).cells[0].fg == 1); // the alt slot's rendition
}
