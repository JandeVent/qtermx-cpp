// T12 — Scrollback & retention (ADR-0006): history rows live on the
// normal screen above the grid; only full-screen scrolls feed them; the
// cap drops oldest-first; the alt screen has none; ED3 clears; resize
// reflows history + grid as one stream; the viewport read API
// (viewportRow, scroll commands) is model state (port of
// tests/screen/test_scrollback.py).
#include "harness.h"
#include "test_pipeline.h"
#include "utf8_decoder.h"

using namespace qtermx;
using namespace qtermx::test;

namespace qtermx::test {

// Feed through the full pipeline with a custom scrollback cap (port of
// feed_scrollback in test_scrollback.py). The cap is read at push time,
// so setting it after construction is fine.
inline Screen& feedScrollback(std::u32string_view text, int limit, int lines, int columns)
{
    static std::unique_ptr<Pipeline> p;
    p = makePipeline(lines, columns);
    p->screen.scrollbackLimit = limit;
    p->feed(text);
    return p->screen;
}

// The k-th viewport row as stripped text (port of `text()` in
// test_scrollback.py: "".join(c.data for c in row).strip()).
inline std::string viewportText(const Screen& screen, int k)
{
    const Row& row = screen.viewportRow(k);
    std::string s;
    for (const Cell& c : row.cells) {
        s += encodeUtf8(c.data);
    }
    return strip(s);
}

} // namespace qtermx::test

// -- Entry: full-screen scrolling pushes rows into history ---------------

TEST_CASE(scrollback_line_feed_at_bottom_enters_history)
{
    Screen& screen = feedTo(U"1\r\n2\r\n3\r\n4", 3, 4);
    // One row scrolled into history; the viewport is still live (0).
    QTERMX_CHECK(screen.scrollbackLen() == 1);
    QTERMX_CHECK(screen.viewportOffset() == 0);
    QTERMX_CHECK(viewportText(screen, 0) == "2");
    screen.scroll(1);
    QTERMX_CHECK(screen.viewportOffset() == 1);
    // Scrolled up, the history row becomes visible first.
    QTERMX_CHECK(viewportText(screen, 0) == "1");
}

TEST_CASE(scrollback_grows_with_output)
{
    Screen& screen = feedTo(
        U"1\r\n2\r\n3\r\n4\r\n5\r\n6\r\n7\r\n8\r\n9\r\n10\r\n11\r\n12\r\n"
        U"13\r\n14\r\n15\r\n16\r\n17\r\n18\r\n19\r\n20\r\n",
        5, 4);
    QTERMX_CHECK(screen.scrollbackLen() == 16);
    QTERMX_CHECK(screen.viewportOffset() == 0);
}

TEST_CASE(scrollback_viewport_row_maps_history_then_grid)
{
    Screen& screen = feedTo(U"1\r\n2\r\n3\r\n4", 3, 4);
    screen.scroll(1);
    QTERMX_CHECK(viewportText(screen, 0) == "1");
    QTERMX_CHECK(viewportText(screen, 1) == "2");
    QTERMX_CHECK(viewportText(screen, 2) == "3");
}

TEST_CASE(scrollback_viewport_row_at_bottom_shows_grid)
{
    Screen& screen = feedTo(U"1\r\n2\r\n3", 3, 4);
    QTERMX_CHECK(viewportText(screen, 0) == "1");
    QTERMX_CHECK(viewportText(screen, 1) == "2");
    QTERMX_CHECK(viewportText(screen, 2) == "3");
}

TEST_CASE(scrollback_su_full_screen_feeds_history)
{
    Screen& screen = feedTo(U"1\r\n2\r\n3\r\n4\x1B[S", 3, 4);
    // SU over the full screen scrolls "2" off the top — into history
    // on top of the row the line feed already pushed.
    QTERMX_CHECK(screen.scrollbackLen() == 2);
    screen.scroll(2);
    QTERMX_CHECK(screen.viewportRow(0).cells[0].data == U"1");
}

TEST_CASE(scrollback_narrowed_region_scroll_discards)
{
    // The region is narrowed before the scrolling feeds happen.
    Screen& screen = feedTo(U"1\r\n2\r\n3\r\n4\x1B[2;4r5\r\n6", 4, 4);
    QTERMX_CHECK(screen.scrollbackLen() == 0);
}

TEST_CASE(scrollback_ri_at_top_discards_no_history_restore)
{
    Screen& screen = feedTo(U"1\r\n2\r\n3\r\n4", 3, 4);
    QTERMX_CHECK(screen.scrollbackLen() == 1);
    screen.reverseIndex(); // RI at the top of the full screen
    QTERMX_CHECK(screen.scrollbackLen() == 1); // no restore from history (spec)
}

// -- Cap ---------------------------------------------------------------

TEST_CASE(scrollback_cap_drops_oldest)
{
    Screen& screen = feedScrollback(
        U"1\r\n2\r\n3\r\n4\r\n5\r\n6\r\n7\r\n8\r\n", 3, 3, 4);
    QTERMX_CHECK(screen.scrollbackLen() == 3);
    screen.scroll(3);
    QTERMX_CHECK(screen.viewportRow(0).cells[0].data == U"4");
}

TEST_CASE(scrollback_zero_limit_disables_scrollback)
{
    Screen& screen = feedScrollback(U"1\r\n2\r\n3\r\n4", 0, 3, 4);
    QTERMX_CHECK(screen.scrollbackLen() == 0);
}

// -- Viewport commands ---------------------------------------------------

TEST_CASE(scrollback_scroll_clamps_to_history)
{
    Screen& screen = feedTo(U"1\r\n2\r\n3\r\n4", 3, 4);
    screen.scroll(99);
    QTERMX_CHECK(screen.viewportOffset() == 1);
    screen.scroll(-99);
    QTERMX_CHECK(screen.viewportOffset() == 0);
}

TEST_CASE(scrollback_scroll_to_bottom_resets_offset)
{
    Screen& screen = feedTo(U"1\r\n2\r\n3\r\n4", 3, 4);
    screen.scroll(1);
    QTERMX_CHECK(screen.viewportOffset() == 1);
    screen.scrollToBottom();
    QTERMX_CHECK(screen.viewportOffset() == 0);
}

TEST_CASE(scrollback_scroll_commands_never_touch_grid)
{
    Screen& screen = feedTo(U"1\r\n2\r\n3\r\n4", 3, 4);
    const std::string before = screen.render();
    screen.scroll(1);
    screen.scrollToBottom();
    QTERMX_CHECK(screen.render() == before);
}

// -- Alt screen exclusion (ADR-0006) ------------------------------------

TEST_CASE(scrollback_alt_screen_has_no_scrollback)
{
    Screen& screen = feedTo(U"1\r\n2\r\n3\r\n4\x1B[?1049h", 3, 4);
    QTERMX_CHECK(screen.scrollbackLen() == 0);
    QTERMX_CHECK(screen.viewportOffset() == 0);
}

TEST_CASE(scrollback_scrolling_in_alt_screen_leaves_history_untouched)
{
    auto p = makePipeline(3, 4);
    p->feed(U"1\r\n2\r\n3\r\n4");
    const int history = p->screen.scrollbackLen();
    p->feed(U"\x1B[?1049h");
    p->feed(U"a\r\nb\r\nc\r\nd");
    // In the alt screen the scrollback API reads empty (ADR-0006)…
    QTERMX_CHECK(p->screen.scrollbackLen() == 0);
    QTERMX_CHECK(p->screen.viewportOffset() == 0);
    p->feed(U"\x1B[?1049l");
    // …and leaving it: history intact, viewport still live.
    QTERMX_CHECK(p->screen.scrollbackLen() == history);
    QTERMX_CHECK(p->screen.viewportOffset() == 0);
}

// -- Erase interactions ---------------------------------------------------

TEST_CASE(scrollback_ed3_clears_history_and_snaps_viewport)
{
    Screen& screen = feedTo(U"1\r\n2\r\n3\r\n4\x1B[3J", 3, 4);
    QTERMX_CHECK(screen.scrollbackLen() == 0);
    QTERMX_CHECK(screen.viewportOffset() == 0);
    // The grid is untouched by ED3.
    QTERMX_CHECK(strip(splitLines(screen.render())[0]) == "2");
}

TEST_CASE(scrollback_ed1_ed2_decaln_leave_history)
{
    auto p = makePipeline(3, 4);
    p->feed(U"1\r\n2\r\n3\r\n4");
    QTERMX_CHECK(p->screen.scrollbackLen() == 1);
    p->feed(U"\x1B[2J");
    QTERMX_CHECK(p->screen.scrollbackLen() == 1);
    p->feed(U"\x1B[1J");
    QTERMX_CHECK(p->screen.scrollbackLen() == 1);
    p->feed(U"\x1B#8");
    QTERMX_CHECK(p->screen.scrollbackLen() == 1);
}

// -- One-stream reflow (ADR-0006) ----------------------------------------

TEST_CASE(scrollback_resize_reflows_history_and_grid_together)
{
    // "abcdefgh" wraps to abcd/efgh; the feed pushes abcd into history.
    // Widening must re-join the pair across the boundary.
    Screen& screen = feedTo(U"abcdefgh\r\nx", 2, 4);
    QTERMX_CHECK(screen.scrollbackLen() == 1);
    screen.scroll(1);
    QTERMX_CHECK(screen.viewportRow(0).cells[0].data == U"a");
    screen.resize(2, 8);
    QTERMX_CHECK(screen.scrollbackLen() == 0);
    QTERMX_CHECK(viewportText(screen, 0) == "abcdefgh");
}

TEST_CASE(scrollback_resize_narrow_keeps_newest_grid_and_history)
{
    Screen& screen = feedTo(U"1\r\n2\r\n3\r\n4\r\n5\r\n6\r\n", 4, 4);
    // History 1,2,3; grid 4,5,6,blank.
    QTERMX_CHECK(screen.scrollbackLen() == 3);
    screen.resize(2, 4);
    // Stream reflows to 7 rows (6 content + the grid's bottom blank);
    // the grid keeps its newest 2 rows — "6" and the bottom blank — and
    // the rest is history.
    QTERMX_CHECK(screen.scrollbackLen() == 5);
    QTERMX_CHECK(viewportText(screen, 0) == "6");
    QTERMX_CHECK(viewportText(screen, 1) == "");
    QTERMX_CHECK(screen.viewportOffset() == 0); // still live
}

TEST_CASE(scrollback_resize_grows_height_pulls_history_into_grid)
{
    // Growing the height shows more of the stream: history shrinks.
    Screen& screen = feedTo(U"1\r\n2\r\n3\r\n4\r\n5\r\n6\r\n", 4, 4);
    QTERMX_CHECK(screen.scrollbackLen() == 3);
    screen.resize(6, 4);
    QTERMX_CHECK(screen.scrollbackLen() == 0);
    QTERMX_CHECK(strip(splitLines(screen.render())[0]) == "1");
}

TEST_CASE(scrollback_resize_clamps_offset)
{
    Screen& screen = feedTo(U"1\r\n2\r\n3\r\n4\r\n5\r\n6\r\n", 3, 4);
    // History 1,2,3; grid 4,5,6.
    screen.scroll(2);
    QTERMX_CHECK(screen.viewportOffset() == 2);
    screen.resize(4, 4); // history shrinks to 2, offset clamps
    QTERMX_CHECK(screen.viewportOffset() == 2);
    screen.resize(5, 4); // history shrinks to 1, offset clamps again
    QTERMX_CHECK(screen.viewportOffset() == 1);
}

// -- Dirty rows (the transport seam, ADR-0005) ---------------------------

TEST_CASE(scrollback_dirty_rows_track_print_and_clear)
{
    Screen screen(3, 4);
    screen.print(U"abc");
    QTERMX_CHECK(screen.takeDirtyRows() == std::set<int>{0});
    QTERMX_CHECK(screen.takeDirtyRows() == std::set<int>{});
}

TEST_CASE(scrollback_dirty_rows_mark_scrolled_region)
{
    Screen& screen = feedTo(U"1\r\n2\r\n3\r\n4", 3, 4);
    const std::set<int> dirty = screen.takeDirtyRows();
    QTERMX_CHECK(dirty == std::set<int>({0, 1, 2}));
}

TEST_CASE(scrollback_dirty_rows_mark_erase)
{
    Screen screen(3, 4);
    screen.print(U"abc");
    screen.takeDirtyRows();
    screen.eraseInDisplay(2);
    QTERMX_CHECK(screen.takeDirtyRows() == std::set<int>({0, 1, 2}));
}
