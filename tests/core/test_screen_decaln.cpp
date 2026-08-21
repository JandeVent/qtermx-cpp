// T11 — DECALN (`ESC # 8`) — screen alignment test (port of
// tests/screen/test_decaln.py). The screen is filled with `E` in the
// cursor's *full current rendition* (not the erase fill), the wrapped
// markers are cleared, and the cursor is homed before and after. Tab
// stops, the scroll region, the DECSC slot, and the modes are
// untouched. In the alternate screen the active grid is the one filled
// (ADR-0004).
#include "harness.h"
#include "test_pipeline.h"

using namespace qtermx;
using namespace qtermx::test;

TEST_CASE(decaln_fills_grid_with_e_in_cursor_rendition)
{
    // Every cell is `E` carrying the cursor's rendition at fill time —
    // foreground, background, and the SGR attributes (not the erase
    // fill: the background comes from the cursor, not from a default).
    Screen& screen = feedTo(U"\x1B[31;1m\x1B[44m\x1B#8");
    QTERMX_CHECK(rowText(screen, 0) == std::string(80, 'E'));
    QTERMX_CHECK(rowText(screen, 23) == std::string(80, 'E'));
    const Cell& e = screen.line(0).cells[0];
    QTERMX_CHECK(e.data == U"E");
    QTERMX_CHECK(e.fg == 1);
    QTERMX_CHECK(e.bg == 4);
    QTERMX_CHECK(e.bold);
}

TEST_CASE(decaln_homes_cursor)
{
    // The cursor ends at the home position.
    Screen& screen = feedTo(U"abc\x1B#8");
    QTERMX_CHECK(screen.cursor.x == 0);
    QTERMX_CHECK(screen.cursor.y == 0);
}

TEST_CASE(decaln_clears_wrapped_flags)
{
    // Wrapped rows from earlier output are marked unwrapped.
    auto p = makePipeline(4, 5);
    p->feed(U"abcdefghij");
    QTERMX_CHECK(p->screen.line(1).wrapped); // "fghij" continues the wrapped row
    p->feed(U"\x1B#8");
    for (int y = 0; y < 4; ++y) {
        QTERMX_CHECK(!p->screen.line(y).wrapped);
    }
}

TEST_CASE(decaln_fills_active_screen_only)
{
    // Inside the alternate screen, DECALN fills the alt grid; the
    // normal grid is preserved.
    auto p = makePipeline(4, 5);
    p->feed(U"abc\r\ndef\x1B[?1047h\x1B#8");
    QTERMX_CHECK(rowText(p->screen, 0) == "EEEEE");
    p->feed(U"\x1B[?1047l");
    QTERMX_CHECK(rowText(p->screen, 0) == "abc");
    QTERMX_CHECK(rowText(p->screen, 1) == "def");
}

TEST_CASE(decaln_leaves_tabs_region_and_saved_cursor)
{
    // DECALN is a display fill only: tab stops, the scroll region and
    // the DECSC slot survive.
    auto p = makePipeline(6, 10);
    // `\0337`/`\0338` (octal) — `\x1B7` would be a greedy hex escape
    // (U+01B7), not ESC + '7'.
    p->feed(U"\x1B[2;4r\x1B[1;5H\x1BH\0337\x1B#8\0338");
    QTERMX_CHECK(p->screen.scrollTop() == 1); // "2;4" → rows 1-3, 0-based
    QTERMX_CHECK(p->screen.scrollBottom() == 3);
    QTERMX_CHECK(p->screen.tabStops().count(4) == 1); // the stop at column 4 set by HTS
    // The DECSC save/restore round-trip left the cursor at its
    // pre-DECALN position — and DECALN did not wipe it.
    QTERMX_CHECK(p->screen.cursor.x == 4);
    QTERMX_CHECK(p->screen.cursor.y == 0);
}
