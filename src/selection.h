#ifndef QTERMX_SELECTION_H
#define QTERMX_SELECTION_H

// Mouse selection — the pure model behind click/drag selection (port of
// pyqtermx selection.py). Qt-free: rows are the frozen Row/Cell objects
// snapshots carry, column_range feeds the renderer's per-cell paint
// test, and selected_text produces the copy payload.
//
// Selection coordinates are *viewport* rows and columns (what the GUI
// can see, ADR-0005): the widget holds only snapshot rows, so a
// selection is cleared whenever the viewport scrolls — it selects
// visible text, not buffer text (the GUI never reads the model).
//
// Header-only: pure functions over the frozen Row/Cell value types, so
// a header keeps the Qt-free core build simple (no .cpp to wire into
// qtermx_core) — the same deliberate deviation as palette.h.

#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "screen.h"
#include "utf8_decoder.h"

namespace qtermx {

// Open-ended column bound for mid-selection rows (the renderer's
// `col <= end` test): far beyond any real grid width.
inline constexpr int kSelectionInf = 1 << 30;

// A normalized selection: `row1 <= row2` and, within one row,
// `col1 <= col2`. `rectangular` is the Alt-drag mode — a column slice
// across rows instead of contiguous text.
struct Selection {
    int row1 = 0;
    int col1 = 0;
    int row2 = 0;
    int col2 = 0;
    bool rectangular = false;

    bool operator==(const Selection& other) const
    {
        return row1 == other.row1 && col1 == other.col1 && row2 == other.row2 &&
               col2 == other.col2 && rectangular == other.rectangular;
    }
};

// A single-cell selection — the click anchor.
inline Selection point(int row, int col)
{
    return Selection{row, col, row, col};
}

namespace detail {

// True when the cell's glyph is blank (empty or whitespace) — the C++
// analogue of Python's `cell.data.strip() == ""`.
inline bool isBlankish(const Cell& c)
{
    for (char32_t ch : c.data) {
        if (ch != U' ' && ch != U'\t' && ch != U'\n' && ch != U'\r' && ch != U'\f' &&
            ch != U'\v') {
            return false;
        }
    }
    return true;
}

} // namespace detail

// The word (maximal non-space run) containing the cell. A space cell is
// its own single-cell word; the hidden continuation cell of a wide char
// belongs to the word, so CJK text never splits mid-glyph.
inline Selection word(int row, int col, const std::vector<Row>& rows)
{
    const std::vector<Cell>* cells = (row >= 0 && row < static_cast<int>(rows.size()))
                                         ? &rows[row].cells
                                         : nullptr;
    if (cells == nullptr || col >= static_cast<int>(cells->size())) {
        return point(row, col);
    }
    const Cell& cell = (*cells)[col];
    if (detail::isBlankish(cell) && !cell.hidden) {
        return point(row, col);
    }
    int left = col;
    while (left > 0 && (!detail::isBlankish((*cells)[left - 1]) || (*cells)[left - 1].hidden)) {
        --left;
    }
    int right = col;
    while (right + 1 < static_cast<int>(cells->size()) &&
           (!detail::isBlankish((*cells)[right + 1]) || (*cells)[right + 1].hidden)) {
        ++right;
    }
    return Selection{row, left, row, right};
}

// The whole row (the copy contract trims trailing blanks).
inline Selection line(int row, int columns)
{
    return Selection{row, 0, row, columns - 1};
}

// Drag: grow the selection from the fixed `(anchor_row, anchor_col)` cell
// toward `(row, col)`, normalized so `row1`/`col1` is always the start.
// The anchor is explicit because the normalized Selection stores no
// anchor — deriving it from `row1`/`col1` drifts: a drag past the anchor
// pushes it into `row2`/`col2`, and the next extend would then anchor at
// the *last mouse cell*, not the press cell. The widget remembers the
// press cell for the whole drag.
inline Selection extend(int anchorRow, int anchorCol, int row, int col, bool rectangular = false)
{
    if (std::make_pair(row, col) < std::make_pair(anchorRow, anchorCol)) {
        return Selection{row, col, anchorRow, anchorCol, rectangular};
    }
    return Selection{anchorRow, anchorCol, row, col, rectangular};
}

// The selected column range on a viewport row, or nullopt when the row
// is untouched — the renderer's per-cell test. Mid-selection rows are
// open-ended (kSelectionInf): they span the full width.
inline std::optional<std::pair<int, int>> columnRange(const Selection& sel, int row)
{
    if (row < sel.row1 || row > sel.row2) {
        return std::nullopt;
    }
    if (sel.rectangular || (row == sel.row1 && row == sel.row2)) {
        return std::make_pair(std::min(sel.col1, sel.col2), std::max(sel.col1, sel.col2));
    }
    if (row == sel.row1) {
        return std::make_pair(sel.col1, kSelectionInf);
    }
    if (row == sel.row2) {
        return std::make_pair(0, sel.col2);
    }
    return std::make_pair(0, kSelectionInf);
}

// Whether the cell is selected — the hit-test that keeps a click inside
// the current selection from discarding it (xterm behavior).
inline bool contains(const Selection& sel, int row, int col)
{
    const auto r = columnRange(sel, row);
    return r.has_value() && r->first <= col && col <= r->second;
}

// The selected text — the copy contract: per row, the selected cells'
// data (hidden continuations of wide chars skipped), trailing blanks
// trimmed, rows joined with newlines. Rows and columns are clamped to
// what the viewport actually holds.
inline std::string selectedText(const std::vector<Row>& rows, const Selection& sel)
{
    if (rows.empty()) {
        return "";
    }
    const int firstRow = std::max(0, sel.row1);
    const int lastRow = std::min(static_cast<int>(rows.size()) - 1, sel.row2);
    std::string out;
    for (int r = firstRow; r <= lastRow; ++r) {
        const Row& row = rows[r];
        const bool first = r == firstRow;
        const bool last = r == lastRow;
        int c1, c2;
        if (sel.rectangular || (first && last)) {
            c1 = std::min(sel.col1, sel.col2);
            c2 = std::max(sel.col1, sel.col2);
        } else if (first) {
            c1 = sel.col1;
            c2 = static_cast<int>(row.cells.size()) - 1;
        } else if (last) {
            c1 = 0;
            c2 = sel.col2;
        } else {
            c1 = 0;
            c2 = static_cast<int>(row.cells.size()) - 1;
        }
        const int maxCol = static_cast<int>(row.cells.size()) - 1;
        c1 = std::max(0, std::min(c1, maxCol));
        c2 = std::max(0, std::min(c2, maxCol));
        std::string text;
        for (int c = c1; c <= c2; ++c) {
            const Cell& cell = row.cells[c];
            if (cell.hidden) {
                continue;
            }
            text += encodeUtf8(cell.data);
        }
        // rstrip whitespace (Python str.rstrip()).
        const size_t lastNonWs = text.find_last_not_of(" \t\n\r\f\v");
        if (lastNonWs != std::string::npos) {
            text.erase(lastNonWs + 1);
        } else {
            text.clear();
        }
        if (!out.empty()) {
            out += '\n';
        }
        out += text;
    }
    return out;
}

} // namespace qtermx

#endif // QTERMX_SELECTION_H
