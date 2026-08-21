// T01 — Rows carry the wrapped marker (xterm.js isWrapped) (port of
// tests/screen/test_wrapped.py). The marker rides on the row a wrap
// lands on, is cleared by an explicit line feed, survives scroll, and
// is consulted by reflow on widen (the ADR-0003 fix: distinct
// full-width rows no longer merge).
#include "harness.h"
#include "test_pipeline.h"

using namespace qtermx;
using namespace qtermx::test;

TEST_CASE(wrapped_wrap_marks_the_landing_row)
{
    Screen& screen = feedTo(U"xxxxxx", 2, 5);
    // 5 columns: "xxxxx" fills row 0, the 6th "x" wraps onto row 1.
    QTERMX_CHECK(screen.line(1).wrapped);
}

TEST_CASE(wrapped_no_wrap_leaves_rows_unwrapped)
{
    Screen& screen = feedTo(U"abc");
    QTERMX_CHECK(!screen.line(0).wrapped);
}

TEST_CASE(wrapped_explicit_lf_clears_the_marker_on_the_landed_row)
{
    Screen& screen = feedTo(U"xxxxxx\n", 3, 5);
    // Wrap lands on row 1 (wrapped=True), then LF moves to row 2 and
    // clears the marker there — row 1 keeps its marker.
    QTERMX_CHECK(screen.line(1).wrapped);
    QTERMX_CHECK(!screen.line(2).wrapped);
}

TEST_CASE(wrapped_cr_does_not_clear_the_marker)
{
    Screen& screen = feedTo(U"xxxxxx\rZ", 2, 5);
    // CR is not an explicit line feed: the wrap marker on row 1 stays.
    QTERMX_CHECK(screen.line(1).wrapped);
}

TEST_CASE(wrapped_wrap_at_bottom_scrolls_and_does_not_mark)
{
    Screen& screen = feedTo(U"aaaabbbbcccc", 2, 4);
    // "aaaa" fills row 0; "bbbb" wraps onto row 1 (marked); "cccc" wraps
    // at the bottom, so the screen scrolls and the fresh row 1 is never
    // marked — it is a new line, not a continuation.
    QTERMX_CHECK(!screen.line(1).wrapped);
    QTERMX_CHECK(splitLines(screen.render())[1] == "cccc");
}

TEST_CASE(wrapped_marker_survives_scroll)
{
    Screen& screen = feedTo(U"xxxxyyyyy", 2, 4);
    // "xxxx" fills row 0; the 5th "y" wraps onto row 1 (marked) and the
    // rest fills it; the 6th "y" wraps at the bottom, scrolling the
    // marked row 1 up to row 0 — the marker rides along.
    QTERMX_CHECK(screen.line(0).wrapped);
    QTERMX_CHECK(splitLines(screen.render())[0] == "yyyy");
}

TEST_CASE(wrapped_widen_joins_wrapped_rows)
{
    Screen& screen = feedTo(U"aaaabbbb", 2, 4);
    QTERMX_CHECK(screen.line(1).wrapped);
    screen.resize(1, 8);
    QTERMX_CHECK(splitLines(screen.render())[0] == "aaaabbbb");
}

TEST_CASE(wrapped_widen_does_not_merge_unwrapped_rows)
{
    // ADR-0003 fix: `abcd\r\nefgh` at 4 columns stays two rows at 8.
    Screen& screen = feedTo(U"abcd\r\nefgh", 2, 4);
    screen.resize(2, 8);
    const auto rows = splitLines(screen.render());
    QTERMX_CHECK(strip(rows[0]) == "abcd");
    QTERMX_CHECK(strip(rows[1]) == "efgh");
}

TEST_CASE(wrapped_narrow_preserves_wrapped_flag)
{
    Screen& screen = feedTo(U"aaaabbbb", 2, 4);
    screen.resize(4, 2);
    QTERMX_CHECK(screen.line(1).wrapped); // still a continuation
    screen.resize(1, 8);
    QTERMX_CHECK(splitLines(screen.render())[0] == "aaaabbbb");
}