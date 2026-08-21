// T1/T2/T4/T5/T06 — dumb screen: printable text, C0 controls, deferred
// wrap, width-aware print, resize reflow (ports of tests/screen/
// test_screen.py, test_c0.py, test_wrap.py, test_width.py, test_resize.py).
// Tests drive the full pipeline Parser → Emulator → Screen.
#include <memory>
#include <stdexcept>

#include "emulator.h"
#include "harness.h"
#include "parser.h"
#include "screen.h"

using namespace qtermx;

namespace {

struct Pipeline {
    Screen screen;
    Emulator emulator;
    Parser parser;

    Pipeline(int lines, int columns)
        : screen(lines, columns)
        , emulator(screen)
        , parser(&emulator)
    {
    }

    void feed(std::u32string_view text)
    {
        parser.feed(text);
        parser.flush();
    }
};

std::unique_ptr<Pipeline> makePipeline(int lines = 24, int columns = 80)
{
    return std::make_unique<Pipeline>(lines, columns);
}

// Feed text through the full pipeline and return the screen.
Screen& feedTo(std::u32string_view text, int lines = 24, int columns = 80)
{
    static std::unique_ptr<Pipeline> p;
    p = makePipeline(lines, columns);
    p->feed(text);
    return p->screen;
}

std::vector<std::string> splitLines(const std::string& s)
{
    std::vector<std::string> lines;
    size_t start = 0;
    while (true) {
        const size_t nl = s.find('\n', start);
        if (nl == std::string::npos) {
            lines.push_back(s.substr(start));
            break;
        }
        lines.push_back(s.substr(start, nl - start));
        start = nl + 1;
    }
    return lines;
}

} // namespace

// ---------------------------------------------------------------------------
// T1 — Dumb screen tracer bullet (test_screen.py)
// ---------------------------------------------------------------------------

TEST_CASE(screen_printable_text_lands_in_grid_cells)
{
    Screen& screen = feedTo(U"hi");
    QTERMX_CHECK(screen.line(0).cells[0].data == U"h");
    QTERMX_CHECK(screen.line(0).cells[1].data == U"i");
}

TEST_CASE(screen_render_shows_printed_text)
{
    Screen& screen = feedTo(U"hi");
    QTERMX_CHECK(splitLines(screen.render())[0].rfind("hi", 0) == 0);
}

TEST_CASE(screen_cursor_moves_past_printed_text)
{
    Screen& screen = feedTo(U"hi");
    QTERMX_CHECK(screen.cursor.x == 2);
    QTERMX_CHECK(screen.cursor.y == 0);
}

TEST_CASE(screen_unprinted_cells_are_blank)
{
    Screen& screen = feedTo(U"hi");
    const Cell blank = screen.line(0).cells[3];
    QTERMX_CHECK((blank == Cell{U" ", -1, -1}));
}

TEST_CASE(screen_printed_cells_carry_default_rendition)
{
    Screen& screen = feedTo(U"x");
    const Cell& cell = screen.line(0).cells[0];
    QTERMX_CHECK(cell.fg == -1);
    QTERMX_CHECK(cell.bg == -1);
    QTERMX_CHECK(!cell.bold && !cell.underline && !cell.reverse && !cell.blink);
}

TEST_CASE(screen_cell_blank_classmethod_creates_default)
{
    QTERMX_CHECK(Cell::blank() == Cell{});
}

TEST_CASE(screen_default_size_is_80x24)
{
    Screen screen;
    QTERMX_CHECK(screen.columns == 80);
    QTERMX_CHECK(screen.lines == 24);
}

// ---------------------------------------------------------------------------
// T2 — C0 controls and full-screen scroll (test_c0.py)
// ---------------------------------------------------------------------------

TEST_CASE(c0_cr_returns_to_column_zero)
{
    Screen& screen = feedTo(U"ab\rcd");
    QTERMX_CHECK(splitLines(screen.render())[0].rfind("cd", 0) == 0);
    QTERMX_CHECK(screen.cursor.x == 2);
    QTERMX_CHECK(screen.cursor.y == 0);
}

TEST_CASE(c0_lf_moves_cursor_down)
{
    Screen& screen = feedTo(U"ab\n");
    QTERMX_CHECK(screen.cursor.x == 2);
    QTERMX_CHECK(screen.cursor.y == 1);
}

TEST_CASE(c0_lf_preserves_column)
{
    Screen& screen = feedTo(U"  x\ny");
    QTERMX_CHECK(screen.cursor.x == 4);
    QTERMX_CHECK(screen.cursor.y == 1);
}

TEST_CASE(c0_lf_at_bottom_scrolls_whole_screen)
{
    Screen& screen = feedTo(U"a\r\nb\r\n", 2, 4);
    // a on row 0; CR+LF → row 1; b on row 1; CR+LF at bottom → scroll up.
    QTERMX_CHECK(screen.render() == "b   \n    ");
    QTERMX_CHECK(screen.cursor.x == 0);
    QTERMX_CHECK(screen.cursor.y == 1);
}

TEST_CASE(c0_lf_at_bottom_discards_top_line)
{
    Screen& screen = feedTo(U"1111\r\n2222\r\n3333", 2, 4);
    QTERMX_CHECK(screen.render() == "2222\n3333");
}

TEST_CASE(c0_bs_moves_left_one_column)
{
    Screen& screen = feedTo(U"ab\bX");
    QTERMX_CHECK(splitLines(screen.render())[0].rfind("aX", 0) == 0);
}

TEST_CASE(c0_bs_at_column_zero_is_clamped)
{
    Screen& screen = feedTo(U"\bX");
    QTERMX_CHECK(splitLines(screen.render())[0].rfind("X", 0) == 0);
}

TEST_CASE(c0_tab_advances_to_next_stop)
{
    Screen& screen = feedTo(U"a\tb");
    QTERMX_CHECK(splitLines(screen.render())[0].rfind("a       b", 0) == 0); // col 8
}

TEST_CASE(c0_tab_at_last_stop_clamps)
{
    // 5 columns: TAB from col 1 advances to the next stop (8), clamped to
    // the last column (4) — the stop itself is out of range.
    Screen& screen = feedTo(U"ab\tX", 24, 5);
    QTERMX_CHECK(screen.cursor.x == 4);
    QTERMX_CHECK(screen.cursor.y == 0);
}

TEST_CASE(c0_bel_is_swallowed)
{
    Screen& screen = feedTo(U"a\ab");
    QTERMX_CHECK(splitLines(screen.render())[0].rfind("ab", 0) == 0);
    QTERMX_CHECK(screen.cursor.x == 2);
    QTERMX_CHECK(screen.cursor.y == 0);
}

TEST_CASE(c0_motion_clears_pending_wrap)
{
    Screen& screen = feedTo(U"abcd\rX", 1, 4);
    QTERMX_CHECK(screen.cursor.x == 1);
    QTERMX_CHECK(screen.cursor.y == 0);
    QTERMX_CHECK(!screen.cursor.pending_wrap);
}

// ---------------------------------------------------------------------------
// T4 — Deferred wrap (test_wrap.py)
// ---------------------------------------------------------------------------

TEST_CASE(wrap_print_in_last_column_sets_pending_wrap)
{
    Screen& screen = feedTo(U"abcd", 2, 4);
    QTERMX_CHECK(screen.line(0).cells[3].data == U"d");
    QTERMX_CHECK(screen.cursor.x == 3);
    QTERMX_CHECK(screen.cursor.y == 0);
    QTERMX_CHECK(screen.cursor.pending_wrap);
}

TEST_CASE(wrap_next_printable_resolves_wrap_to_next_line)
{
    Screen& screen = feedTo(U"abcdX", 2, 4);
    QTERMX_CHECK(screen.line(0).cells[3].data == U"d");
    QTERMX_CHECK(screen.line(1).cells[0].data == U"X");
    QTERMX_CHECK(screen.cursor.x == 1);
    QTERMX_CHECK(screen.cursor.y == 1);
    QTERMX_CHECK(!screen.cursor.pending_wrap);
}

TEST_CASE(wrap_at_bottom_line_scrolls)
{
    Screen& screen = feedTo(U"abcdX", 1, 4);
    // d at col 3, wrap → scroll whole screen (the old line is discarded),
    // X lands at the start of the fresh line.
    QTERMX_CHECK(screen.render() == "X   ");
    QTERMX_CHECK(screen.line(0).cells[0].data == U"X");
    QTERMX_CHECK(screen.cursor.x == 1);
    QTERMX_CHECK(screen.cursor.y == 0);
}

TEST_CASE(wrap_cr_cancels_pending_wrap)
{
    Screen& screen = feedTo(U"abcd\rZ", 2, 4);
    QTERMX_CHECK(screen.line(0).cells[3].data == U"d");
    QTERMX_CHECK(screen.line(0).cells[0].data == U"Z"); // Z overwrote a, not row 1
    QTERMX_CHECK(screen.cursor.x == 1);
    QTERMX_CHECK(screen.cursor.y == 0);
}

TEST_CASE(wrap_bs_cancels_pending_wrap)
{
    Screen& screen = feedTo(U"abcd\bZ", 2, 4);
    QTERMX_CHECK(screen.line(0).cells[2].data == U"Z");
    QTERMX_CHECK(screen.cursor.x == 3);
    QTERMX_CHECK(screen.cursor.y == 0);
}

TEST_CASE(wrap_tab_keeps_pending_wrap_at_wrap_position)
{
    Screen& screen = feedTo(U"abcd\tZ", 2, 4);
    // After d at col 3, a tab past the last stop lands at the wrap
    // position with the pending wrap intact (real xterm: cur_col = cols,
    // wrap_pending survives); the next char wraps to the next line.
    QTERMX_CHECK(screen.line(1).cells[0].data == U"Z");
    QTERMX_CHECK(screen.cursor.x == 1);
    QTERMX_CHECK(screen.cursor.y == 1);
    QTERMX_CHECK(!screen.cursor.pending_wrap);
}

TEST_CASE(wrap_long_line_wraps_across_rows)
{
    Screen& screen = feedTo(U"abcdefghij", 3, 4);
    QTERMX_CHECK(screen.line(0).cells[0].data == U"a");
    QTERMX_CHECK(screen.line(1).cells[0].data == U"e");
    QTERMX_CHECK(screen.line(2).cells[0].data == U"i");
    QTERMX_CHECK(screen.cursor.x == 2);
    QTERMX_CHECK(screen.cursor.y == 2);
}

// ---------------------------------------------------------------------------
// T5 — Width-aware print (test_width.py)
// ---------------------------------------------------------------------------

TEST_CASE(width_wide_char_occupies_two_cells)
{
    Screen& screen = feedTo(U"你");
    QTERMX_CHECK(screen.line(0).cells[0].data == U"你");
    QTERMX_CHECK(screen.line(0).cells[1].data.empty()); // blank continuation cell
    QTERMX_CHECK(screen.cursor.x == 2);
    QTERMX_CHECK(screen.cursor.y == 0);
}

TEST_CASE(width_wide_char_shifts_following_text)
{
    Screen& screen = feedTo(U"你a");
    QTERMX_CHECK(screen.line(0).cells[2].data == U"a");
    QTERMX_CHECK(screen.cursor.x == 3);
    QTERMX_CHECK(screen.cursor.y == 0);
}

TEST_CASE(width_combining_mark_attaches_to_previous_cell)
{
    Screen& screen = feedTo(U"e\u0301"); // é as e + combining acute
    QTERMX_CHECK(screen.line(0).cells[0].data == U"e\u0301");
    QTERMX_CHECK(screen.line(0).cells[1].data == U" ");
    QTERMX_CHECK(screen.cursor.x == 1);
    QTERMX_CHECK(screen.cursor.y == 0);
}

TEST_CASE(width_combining_mark_at_column_zero_is_dropped)
{
    Screen& screen = feedTo(U"\u0301a");
    QTERMX_CHECK(screen.line(0).cells[0].data == U"a");
    QTERMX_CHECK(screen.cursor.x == 1);
    QTERMX_CHECK(screen.cursor.y == 0);
}

TEST_CASE(width_wide_char_at_last_column_resolves_wrap)
{
    // 4 columns: 你 at cols 0–1, then 'a' at col 2, 'b' at col 3 → pending.
    Screen& screen = feedTo(U"你abX", 2, 4);
    QTERMX_CHECK(screen.line(1).cells[0].data == U"X");
    QTERMX_CHECK(screen.cursor.x == 1);
    QTERMX_CHECK(screen.cursor.y == 1);
}

TEST_CASE(width_wide_char_does_not_fit_at_edge_wraps_first)
{
    // 3 columns: 'a' at 0, 你 needs cols 1–2 — fits exactly.
    Screen& screen = feedTo(U"a你", 24, 3);
    QTERMX_CHECK(screen.line(0).cells[0].data == U"a");
    QTERMX_CHECK(screen.line(0).cells[1].data == U"你");
    QTERMX_CHECK(screen.cursor.x == 2);
    QTERMX_CHECK(screen.cursor.y == 0);
    QTERMX_CHECK(screen.cursor.pending_wrap);
}

TEST_CASE(width_wide_char_with_only_one_cell_left_wraps)
{
    // 4 columns: 'abc' at 0–2, then 你 needs cols 3–4 — only 1 left, so it
    // wraps to the next line first.
    Screen& screen = feedTo(U"abc你X", 2, 4);
    QTERMX_CHECK(screen.line(1).cells[0].data == U"你");
    QTERMX_CHECK(screen.line(1).cells[2].data == U"X");
    QTERMX_CHECK(screen.cursor.x == 3);
    QTERMX_CHECK(screen.cursor.y == 1);
}

TEST_CASE(width_combining_mark_after_wide_char)
{
    Screen& screen = feedTo(U"你\u0301");
    // Combining attaches to the wide char's *glyph* cell, not the blank
    // continuation.
    QTERMX_CHECK(screen.line(0).cells[0].data == U"你\u0301");
    QTERMX_CHECK(screen.line(0).cells[1].data.empty());
}

// ---------------------------------------------------------------------------
// T06 — Resize reflow (test_resize.py)
// ---------------------------------------------------------------------------

TEST_CASE(resize_widen_merges_wrapped_lines)
{
    Screen& screen = feedTo(U"abcdefgh", 2, 4);
    screen.resize(2, 8);
    QTERMX_CHECK(screen.render() == "abcdefgh\n        ");
}

TEST_CASE(resize_narrow_rewraps_lines)
{
    Screen& screen = feedTo(U"abcdefgh", 2, 8);
    screen.resize(3, 4);
    QTERMX_CHECK(screen.render() == "abcd\nefgh\n    ");
}

TEST_CASE(resize_narrow_twice_keeps_content)
{
    Screen& screen = feedTo(U"abcdefgh", 2, 8);
    screen.resize(4, 2);
    QTERMX_CHECK(screen.render() == "ab\ncd\nef\ngh");
}

TEST_CASE(resize_shrink_height_keeps_bottom_lines)
{
    Screen& screen = feedTo(U"1111\r\n2222\r\n3333", 3, 4);
    screen.resize(2, 4);
    QTERMX_CHECK(screen.render() == "2222\n3333");
}

TEST_CASE(resize_shrink_height_keeps_bottom_blank_lines)
{
    // Shrinking the height keeps the *newest* rows — the grid's bottom
    // blanks — not the reflowed text above them. The trailing-blank trim
    // must not let old content fall into the grid (the resize+scroll
    // "old text tints" bug): text at the top of a tall screen, blanks
    // below, shrink the height → the grid stays blank and the text goes
    // to history.
    Screen& screen = feedTo(U"\x1B[32mROW-1-GRN\r\n", 24, 80);
    screen.resize(4, 8);
    QTERMX_CHECK(screen.render() == "        \n        \n        \n        ");
    // "ROW-1-GRN" reflows to "ROW-1-GR" + "N" in history, rendition kept.
    QTERMX_CHECK(screen.scrollbackLen() == 2);
    screen.scroll(2); // view the history
    QTERMX_CHECK(screen.viewportRow(0).cells[0].fg == 2); // kept its green
    // The history rows are re-padded to the new width — a scrolled-up
    // viewport must render every column (a short row would leave stale
    // pixels in its tail — the "tint fragments" after resize).
    QTERMX_CHECK(screen.viewportRow(0).cells.size() == 8);
    QTERMX_CHECK(screen.viewportRow(1).cells.size() == 8);
}

TEST_CASE(resize_grow_height_pads_blank_lines)
{
    Screen& screen = feedTo(U"ab", 2, 4);
    screen.resize(4, 4);
    QTERMX_CHECK(screen.render() == "ab  \n    \n    \n    ");
}

TEST_CASE(resize_reflow_preserves_rendition)
{
    Screen& screen = feedTo(U"\r\n\x1B[31mred", 2, 6);
    screen.resize(1, 2);
    const Row& line = screen.line(0);
    // "red" reflows to "re"+"d"; only the bottom row survives, so row 0
    // holds "d" (carrying the rendition) plus a blank padding cell.
    QTERMX_CHECK(line.cells[0].data == U"d");
    QTERMX_CHECK(line.cells[0].fg == 1);
    QTERMX_CHECK(line.cells[1] == Cell::blank());
}

TEST_CASE(resize_reflow_preserves_wide_chars)
{
    Screen& screen = feedTo(U"你ab", 2, 6);
    screen.resize(2, 3);
    // 你 occupies cols 0–1 of the new row, 'a' col 2, 'b' wraps.
    QTERMX_CHECK(screen.line(0).cells[0].data == U"你");
    QTERMX_CHECK(screen.line(0).cells[2].data == U"a");
    QTERMX_CHECK(screen.line(1).cells[0].data == U"b");
}

TEST_CASE(resize_cursor_clamped_after_resize)
{
    Screen& screen = feedTo(U"abcdef", 2, 6);
    screen.resize(1, 4);
    QTERMX_CHECK(screen.cursor.x == 3);
    QTERMX_CHECK(screen.cursor.y == 0);
    QTERMX_CHECK(!screen.cursor.pending_wrap);
}

TEST_CASE(resize_reflow_preserves_interior_blanks)
{
    Screen& screen = feedTo(U"a   b", 2, 6);
    screen.resize(2, 8);
    QTERMX_CHECK(screen.render() == "a   b   \n        ");
}

TEST_CASE(resize_reflow_keeps_blank_lines_as_separators)
{
    Screen& screen = feedTo(U"ab\n\ncd", 4, 4);
    screen.resize(4, 8);
    QTERMX_CHECK(screen.render() == "ab      \n        \n  cd    \n        ");
}

TEST_CASE(resize_reflow_after_combining_mark_does_not_raise)
{
    Screen& screen = feedTo(U"e\u0301", 2, 6);
    screen.resize(2, 4);
    QTERMX_CHECK(screen.line(0).cells[0].data == U"e\u0301");
    QTERMX_CHECK(screen.line(0).cells[1].data == U" ");
}

TEST_CASE(resize_reflow_after_wide_combining_mark_does_not_raise)
{
    Screen& screen = feedTo(U"你\u0301", 2, 6);
    screen.resize(2, 3);
    QTERMX_CHECK(screen.line(0).cells[0].data == U"你\u0301");
}

TEST_CASE(resize_rejects_degenerate_sizes)
{
    Screen& screen = feedTo(U"hi");
    for (const auto& bad : {std::make_pair(0, 4), std::make_pair(4, 0),
                            std::make_pair(-1, 4), std::make_pair(0, 0)}) {
        bool threw = false;
        try {
            screen.resize(bad.first, bad.second);
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        QTERMX_CHECK(threw);
    }
}