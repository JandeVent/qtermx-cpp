// T15 — Mouse selection — the pure model the widget drives (click →
// word → line, drag extend, rectangular, and the copy text extraction)
// (port of tests/selection/test_selection.py). Qt-free: rows are the
// frozen snapshot Row/Cell objects, selection coordinates are viewport
// rows/columns — the widget clears a selection when the viewport
// scrolls, because the GUI only ever holds viewport rows (ADR-0005) and
// cannot re-identify text that scrolled away.
#include "harness.h"
#include "selection.h"

using namespace qtermx;
using namespace qtermx::test;

namespace {

// Python `Row([Cell(c) for c in text])` — one row per string.
std::vector<Row> makeRows(std::initializer_list<const char*> texts)
{
    std::vector<Row> rows;
    for (const char* t : texts) {
        Row row;
        for (const char* p = t; *p; ++p) {
            row.cells.push_back(Cell{std::u32string(1, static_cast<char32_t>(*p))});
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

// A row with a wide char (你) followed by its hidden continuation cell.
std::vector<Row> wideRow()
{
    std::vector<Row> rows;
    Row row;
    row.cells.push_back(Cell{U"你"});
    Cell cont{U" "};
    cont.hidden = true;
    row.cells.push_back(cont);
    row.cells.push_back(Cell{U"x"});
    rows.push_back(std::move(row));
    return rows;
}

} // namespace

// -- point / word / line -------------------------------------------------

TEST_CASE(selection_point_covers_one_cell)
{
    const Selection sel = point(2, 3);
    QTERMX_CHECK((sel == Selection{2, 3, 2, 3}));
    QTERMX_CHECK(contains(sel, 2, 3));
    QTERMX_CHECK(!contains(sel, 2, 4));
    QTERMX_CHECK(!contains(sel, 1, 3));
}

TEST_CASE(selection_word_spans_the_non_space_run)
{
    const auto rows = makeRows({"aa bb cc"});
    QTERMX_CHECK((word(0, 3, rows) == Selection{0, 3, 0, 4}));
    QTERMX_CHECK((word(0, 6, rows) == Selection{0, 6, 0, 7}));
    QTERMX_CHECK((word(0, 0, rows) == Selection{0, 0, 0, 1}));
}

TEST_CASE(selection_word_on_a_space_is_single_cell)
{
    const auto rows = makeRows({"aa bb"});
    QTERMX_CHECK((word(0, 2, rows) == Selection{0, 2, 0, 2}));
}

TEST_CASE(selection_word_includes_wide_char_continuations)
{
    // 你 occupies two cells; the continuation is a hidden empty cell —
    // it belongs to the word (CJK text must not split mid-glyph).
    const auto rows = wideRow();
    QTERMX_CHECK((word(0, 0, rows) == Selection{0, 0, 0, 2}));
    QTERMX_CHECK((word(0, 1, rows) == Selection{0, 0, 0, 2})); // clicked on the continuation
    QTERMX_CHECK((word(0, 2, rows) == Selection{0, 0, 0, 2}));
    std::vector<Row> rows2;
    {
        Row row;
        row.cells.push_back(Cell{U"你"});
        Cell cont{U" "};
        cont.hidden = true;
        row.cells.push_back(cont);
        row.cells.push_back(Cell{U"x"});
        row.cells.push_back(Cell{U" "});
        row.cells.push_back(Cell{U"y"});
        rows2.push_back(std::move(row));
    }
    QTERMX_CHECK((word(0, 0, rows2) == Selection{0, 0, 0, 2}));
    QTERMX_CHECK((word(0, 4, rows2) == Selection{0, 4, 0, 4}));
}

TEST_CASE(selection_word_out_of_bounds_is_a_point)
{
    const auto rows = makeRows({"ab"});
    QTERMX_CHECK((word(5, 3, rows) == Selection{5, 3, 5, 3}));
    QTERMX_CHECK((word(0, 9, rows) == Selection{0, 9, 0, 9}));
}

TEST_CASE(selection_line_selects_the_whole_row)
{
    QTERMX_CHECK((line(1, 10) == Selection{1, 0, 1, 9}));
}

// -- extend --------------------------------------------------------------

TEST_CASE(selection_extend_keeps_the_anchor_cell)
{
    QTERMX_CHECK((extend(1, 1, 3, 4) == Selection{1, 1, 3, 4}));
    QTERMX_CHECK((extend(1, 1, 4, 0) == Selection{1, 1, 4, 0}));
}

TEST_CASE(selection_extend_normalizes_backwards_drags)
{
    QTERMX_CHECK((extend(2, 3, 1, 5) == Selection{1, 5, 2, 3}));
    QTERMX_CHECK((extend(2, 3, 2, 1) == Selection{2, 1, 2, 3}));
}

TEST_CASE(selection_extend_anchor_survives_direction_reversal)
{
    // The anchor-drift regression: dragging past the anchor and back
    // must extend from the original press cell, never the last mouse
    // cell. Press at col 10, drag left to 5, drag right to 8 — the
    // selection must be 8–10, not 5–8.
    QTERMX_CHECK((extend(0, 10, 0, 5) == Selection{0, 5, 0, 10}));
    QTERMX_CHECK((extend(0, 10, 0, 8) == Selection{0, 8, 0, 10}));
    QTERMX_CHECK((extend(0, 10, 0, 3) == Selection{0, 3, 0, 10}));
}

TEST_CASE(selection_extend_preserves_rectangular_mode)
{
    QTERMX_CHECK(extend(0, 0, 2, 3, true).rectangular);
}

// -- column_range (the renderer's per-row test) ---------------------------

TEST_CASE(selection_column_range_single_row)
{
    const Selection sel{2, 3, 2, 5};
    QTERMX_CHECK(columnRange(sel, 2) == std::make_pair(3, 5));
    QTERMX_CHECK(!columnRange(sel, 1).has_value());
    QTERMX_CHECK(!columnRange(sel, 3).has_value());
}

TEST_CASE(selection_column_range_multi_row_has_open_ends)
{
    const Selection sel{1, 3, 3, 5};
    QTERMX_CHECK(columnRange(sel, 1) == std::make_pair(3, kSelectionInf)); // first: to end
    QTERMX_CHECK(columnRange(sel, 2) == std::make_pair(0, kSelectionInf)); // middle: full width
    QTERMX_CHECK(columnRange(sel, 3) == std::make_pair(0, 5));             // last: from start
    QTERMX_CHECK(!columnRange(sel, 0).has_value());
    QTERMX_CHECK(!columnRange(sel, 4).has_value());
}

TEST_CASE(selection_column_range_rectangular_is_same_every_row)
{
    const Selection sel{1, 2, 3, 4, true};
    QTERMX_CHECK(columnRange(sel, 1) == std::make_pair(2, 4));
    QTERMX_CHECK(columnRange(sel, 2) == std::make_pair(2, 4));
    QTERMX_CHECK(columnRange(sel, 3) == std::make_pair(2, 4));
    QTERMX_CHECK(!columnRange(sel, 0).has_value());
}

// -- selected_text (the copy contract) ------------------------------------

TEST_CASE(selection_selected_text_trims_trailing_blanks)
{
    const auto rows = makeRows({"ab   "});
    QTERMX_CHECK((selectedText(rows, Selection{0, 0, 0, 4}) == "ab"));
}

TEST_CASE(selection_selected_text_joins_rows_with_newlines)
{
    const auto rows = makeRows({"hello", "world"});
    QTERMX_CHECK((selectedText(rows, Selection{0, 0, 1, 4}) == "hello\nworld"));
}

TEST_CASE(selection_selected_text_middle_rows_are_fully_included)
{
    const auto rows = makeRows({"abcdef", "ghijkl", "mnopqr"});
    QTERMX_CHECK((selectedText(rows, Selection{0, 2, 2, 3}) == "cdef\nghijkl\nmnop"));
}

TEST_CASE(selection_selected_text_skips_hidden_continuations)
{
    const auto rows = wideRow();
    QTERMX_CHECK((selectedText(rows, Selection{0, 0, 0, 1}) == "你"));
}

TEST_CASE(selection_selected_text_rectangular_slices_columns)
{
    const auto rows = makeRows({"ab", "cd"});
    QTERMX_CHECK((selectedText(rows, Selection{0, 0, 1, 0, true}) == "a\nc"));
}

TEST_CASE(selection_selected_text_clamps_out_of_bounds_rows)
{
    const auto rows = makeRows({"ab"});
    QTERMX_CHECK((selectedText(rows, Selection{0, 0, 5, 9}) == "ab"));
    QTERMX_CHECK((selectedText(rows, Selection{5, 0, 7, 9}) == ""));
}
