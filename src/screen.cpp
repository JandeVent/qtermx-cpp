#include "screen.h"

#include <algorithm>
#include <stdexcept>

#include "utf8_decoder.h"
#include "wcwidth.h"

namespace qtermx {

namespace {

// The DEC Special Graphics map — the line-drawing charset: ASCII
// 0x60–0x7E → box-drawing and math glyphs, `0x5F` → no-break space.
// What `man` and `ls` boxes are made of.
const std::array<std::pair<char32_t, char32_t>, 32> kDecGraphics = {{
    {0x5F, 0x00A0}, // no-break space
    {0x60, 0x25C6}, // ◆
    {0x61, 0x2592}, // ▒
    {0x62, 0x2409}, // ␉
    {0x63, 0x240C}, // ␌
    {0x64, 0x240D}, // ␍
    {0x65, 0x240A}, // ␊
    {0x66, 0x00B0}, // °
    {0x67, 0x00B1}, // ±
    {0x68, 0x2424}, // ␤
    {0x69, 0x240B}, // ␋
    {0x6A, 0x2518}, // ┘
    {0x6B, 0x2510}, // ┐
    {0x6C, 0x250C}, // ┌
    {0x6D, 0x2514}, // └
    {0x6E, 0x253C}, // ┼
    {0x6F, 0x23BA}, // ⎺
    {0x70, 0x23BB}, // ⎻
    {0x71, 0x2500}, // ─
    {0x72, 0x23BC}, // ⎼
    {0x73, 0x23BD}, // ⎽
    {0x74, 0x251C}, // ├
    {0x75, 0x2524}, // ┤
    {0x76, 0x2534}, // ┴
    {0x77, 0x252C}, // ┬
    {0x78, 0x2502}, // │
    {0x79, 0x2264}, // ≤
    {0x7A, 0x2265}, // ≥
    {0x7B, 0x03C0}, // π
    {0x7C, 0x2260}, // ≠
    {0x7D, 0x00A3}, // £
    {0x7E, 0x00B7}, // ·
}};

// The UK charset (`ESC ( A`): only `#` differs from ASCII.
constexpr std::pair<char32_t, char32_t> kUk[] = {{0x23, 0x00A3}};

char32_t translateChar(const std::string& charset, char32_t cp)
{
    if (charset == "0") {
        for (const auto& [from, to] : kDecGraphics) {
            if (from == cp) {
                return to;
            }
        }
    } else if (charset == "A") {
        for (const auto& [from, to] : kUk) {
            if (from == cp) {
                return to;
            }
        }
    }
    return cp;
}

// Drop trailing blank-separator rows (reflow padding the caller will
// re-pad anyway). Used by resize on the reflowed stream and on the
// history slice.
void trimTrailingBlankRows(std::vector<Row>& rows)
{
    while (!rows.empty() && rows.back().cells.empty()) {
        rows.pop_back();
    }
}

} // namespace

// ============================================================================
// Construction
// ============================================================================

Screen::Screen(int lines_, int columns_, int scrollbackLimit_)
    : lines(lines_)
    , columns(columns_)
    , scrollbackLimit(scrollbackLimit_)
{
    for (auto& state : m_screens) {
        state.grid = blankRows();
        state.scrollBottom = lines - 1;
        for (int x = 0; x < columns; x += 8) {
            state.tabStops.insert(x);
        }
    }
    m_active = 0;
}

std::vector<Row> Screen::blankRows()
{
    std::vector<Row> rows;
    rows.reserve(lines);
    for (int i = 0; i < lines; ++i) {
        rows.push_back(blankRow());
    }
    return rows;
}

Row Screen::blankRow()
{
    // One shared Cell for the whole row: cells are value types and only
    // ever *replaced* (never mutated in place), so sharing is safe and
    // turns an 80-cell construction into one.
    return Row{std::vector<Cell>(columns)};
}

// ============================================================================
// Read API
// ============================================================================

std::string Screen::render() const
{
    std::string out;
    for (const auto& row : activeGrid()) {
        for (const auto& cell : row.cells) {
            out += encodeUtf8(cell.data);
        }
        out += '\n';
    }
    if (!out.empty()) {
        out.pop_back(); // no trailing newline (Python "\n".join)
    }
    return out;
}

// ============================================================================
// Viewport (scrollback read API, ADR-0006)
// ============================================================================

int Screen::scrollbackLen() const
{
    return m_active == 1 ? 0 : static_cast<int>(m_screens[0].scrollback.size());
}

int Screen::viewportOffset() const
{
    return m_active == 1 ? 0 : m_screens[0].scrollOffset;
}

bool Screen::altScreen() const
{
    return m_active == 1;
}

const Row& Screen::viewportRow(int k) const
{
    k = std::max(0, std::min(k, lines - 1));
    if (m_active == 1) {
        return m_screens[1].grid[k];
    }
    const auto& sb = m_screens[0].scrollback;
    const int r = static_cast<int>(sb.size()) - m_screens[0].scrollOffset + k;
    if (r < static_cast<int>(sb.size())) {
        return sb[r];
    }
    return m_screens[0].grid[r - static_cast<int>(sb.size())];
}

void Screen::scroll(int n)
{
    auto& state = m_screens[0];
    state.scrollOffset = std::max(0, std::min(static_cast<int>(state.scrollback.size()),
                                              state.scrollOffset + n));
}

void Screen::scrollToBottom()
{
    m_screens[0].scrollOffset = 0;
}

void Screen::clearScrollback()
{
    auto& state = m_screens[0];
    state.scrollback.clear();
    state.scrollOffset = 0;
    markAllDirty();
}

// ============================================================================
// Change tracking (ADR-0005)
// ============================================================================

std::set<int> Screen::takeDirtyRows()
{
    std::set<int> rows = std::move(m_dirtyRows);
    m_dirtyRows.clear();
    return rows;
}

void Screen::markAllDirty()
{
    for (int y = 0; y < lines; ++y) {
        m_dirtyRows.insert(y);
    }
}

// ============================================================================
// Printing
// ============================================================================

void Screen::print(std::u32string_view text)
{
    Cursor& c = cursor;
    auto& grid = activeGrid();
    const bool decawm = mode(kDecawm, true);
    const bool irm = mode(kIrm);
    // The active charset's ASCII table — fixed for the whole batch (a
    // print is one dispatch; designation/shift events cannot interleave).
    // The default ASCII slot is identity — no table.
    const std::string& charset = m_charsets[m_charsetLevel];
    const bool translate = charset != "B";
    // The graphic rendition is the cursor's state and cannot change
    // mid-batch either (SGR is its own dispatch) — hoist it.
    const int fg = c.fg;
    const int bg = c.bg;
    const bool bold = c.bold;
    const bool underline = c.underline;
    const bool reverse = c.reverse;
    const bool blink = c.blink;
    const bool dim = c.dim;
    const bool italic = c.italic;
    const bool hidden = c.hidden;
    const bool strike = c.strike;
    const bool overline = c.overline;
    int x = c.x;
    int y = c.y;
    int markedY = -1;
    for (const char32_t raw : text) {
        char32_t ch = raw;
        if (translate && ch < 0x7F) {
            ch = translateChar(charset, ch);
        }
        if (markedY != y) {
            markDirty(y);
            markedY = y;
        }
        if (c.pending_wrap) {
            const bool wrapped = resolveWrap();
            x = c.x;
            y = c.y;
            if (wrapped) {
                markWrapped(y);
            }
        }
        // ASCII has no wide or combining glyphs — skip the wcwidth
        // table entirely (the paste workload is ~100% ASCII).
        const int width = ch < 0x80 ? 1 : wcwidth(ch);
        if (width < 0) {
            continue; // control character — not printable
        }
        if (width == 0) {
            // [A][◌̊] = [Å][ ]
            attachCombining(ch);
            continue;
        }
        if (width == 2 && x >= columns - 1) {
            // Only one cell left. Autowrap on: the wide char wraps to
            // the next line first (xterm behavior); off: it does not
            // fit and is dropped (xterm.js).
            if (decawm) {
                const bool wrapped = resolveWrap();
                x = c.x;
                y = c.y;
                if (wrapped) {
                    markWrapped(y);
                }
            } else {
                continue;
            }
        }
        if (irm) {
            // Insert mode: shift the row right by the char's width,
            // dropping trailing cells (xterm.js insertCells).
            insertCells(y, x, width);
        }
        auto& row = grid[y];
        row.cells[x] = Cell{std::u32string(1, ch), fg, bg, bold, underline, reverse,
                            blink, dim, italic, hidden, strike, overline};
        if (markedY != y) {
            markDirty(y);
            markedY = y;
        }
        if (width == 2) {
            // Wide characters occupy two cells; only the lead cell
            // holds the glyph — the follow-up cell is an empty
            // continuation (renderers must skip it). The continuation
            // carries the full rendition like the lead (xterm.js).
            row.cells[x + 1] = Cell{U"", fg, bg, bold, underline, reverse,
                                    blink, dim, italic, hidden, strike, overline};
        }
        x += width;
        if (x >= columns) {
            x = columns - 1;
            if (decawm) {
                c.pending_wrap = true;
            }
        }
        c.x = x;
    }
}

void Screen::insertCells(int y, int x, int n)
{
    // IRM/ICH: shift the row right by `n` cells at `x`, filling the gap
    // with erase-fill cells and dropping cells past the edge (xterm.js
    // BufferLine.insertCells). A wide lead split by the insertion point
    // is blanked, as is a wide lead that lands on the last cell.
    auto& row = activeGrid()[y];
    if (x && !row.cells[x - 1].data.empty() && wcwidth(row.cells[x - 1].data[0]) == 2) {
        // Inserting at the continuation cell of a wide char: the split
        // lead is blanked (xterm.js).
        row.cells[x - 1] = eraseFill();
    }
    if (n < columns - x) {
        for (int i = columns - x - n - 1; i >= 0; --i) {
            row.cells[x + n + i] = row.cells[x + i];
        }
        for (int i = x; i < x + n; ++i) {
            row.cells[i] = eraseFill();
        }
    } else {
        for (int i = x; i < columns; ++i) {
            row.cells[i] = eraseFill();
        }
    }
    if (!row.cells[columns - 1].data.empty() && wcwidth(row.cells[columns - 1].data[0]) == 2) {
        row.cells[columns - 1] = eraseFill();
    }
}

const Cell& Screen::eraseFill()
{
    // The blank cell IRM/ED/EL/ECH stamp: default foreground, the
    // cursor's current background color, no attributes (xterm.js
    // backColorErase default). Cached — bg changes are rare.
    const int bg = cursor.bg;
    if (m_cachedEraseBg != bg) {
        m_cachedEraseFill = Cell{U" ", -1, bg};
        m_cachedEraseBg = bg;
    }
    return m_cachedEraseFill;
}

void Screen::attachCombining(char32_t ch)
{
    // Attach a combining mark to the cell behind the cursor; if that
    // cell is a wide character's blank continuation, the glyph cell
    // behind it instead. Dropped at column 0.
    //  [A][◌̊] = [Å][ ]
    int x = cursor.x;
    if (x == 0) {
        return;
    }
    auto& row = activeGrid()[cursor.y];
    if (row.cells[x - 1].data.empty()) {
        --x;
    }
    if (x > 0) {
        row.cells[x - 1].data.push_back(ch);
    }
}

void Screen::markWrapped(int y)
{
    activeGrid()[y].wrapped = true;
}

bool Screen::resolveWrap()
{
    // The next printable after a full line: move to the next line,
    // scrolling the region if already at its bottom. Returns True when
    // the cursor landed on a new row without scrolling — the caller
    // marks that row wrapped (a wrap that scrolls lands on a fresh line
    // and is not marked, matching xterm.js).
    cursor.pending_wrap = false;
    cursor.x = 0;
    if (cursor.y == scrollBottom()) {
        scrollRegion(scrollTop(), scrollBottom(), 1);
        return false;
    }
    if (cursor.y < lines - 1) {
        cursor.y += 1;
        return true;
    }
    return false;
}

// ============================================================================
// Graphic rendition
// ============================================================================

void Screen::resetRendition()
{
    Cursor& c = cursor;
    c.fg = -1;
    c.bg = -1;
    c.bold = c.dim = c.italic = c.underline = c.blink = false;
    c.reverse = c.hidden = c.strike = c.overline = false;
}

void Screen::setFg(int color) { cursor.fg = color; }
void Screen::setBg(int color) { cursor.bg = color; }
void Screen::setBold(bool on) { cursor.bold = on; }
void Screen::setDim(bool on) { cursor.dim = on; }
void Screen::setItalic(bool on) { cursor.italic = on; }
void Screen::setUnderline(bool on) { cursor.underline = on; }
void Screen::setBlink(bool on) { cursor.blink = on; }
void Screen::setReverse(bool on) { cursor.reverse = on; }
void Screen::setHidden(bool on) { cursor.hidden = on; }
void Screen::setStrike(bool on) { cursor.strike = on; }
void Screen::setOverline(bool on) { cursor.overline = on; }

// ============================================================================
// Modes
// ============================================================================

bool Screen::mode(int number, bool privateMode) const
{
    const auto& modes = privateMode ? m_decModes : m_ansiModes;
    return modes.find(number) != modes.end();
}

void Screen::setMode(int number, bool privateMode)
{
    (privateMode ? m_decModes : m_ansiModes).insert(number);
    if (privateMode && number == kDecom) {
        // Origin mode set: the cursor moves home — the top of the
        // scroll region (xterm.js setMode 6).
        cursor.pending_wrap = false;
        cursor.x = 0;
        cursor.y = scrollTop();
    }
}

void Screen::resetMode(int number, bool privateMode)
{
    (privateMode ? m_decModes : m_ansiModes).erase(number);
    if (privateMode && number == kDecom) {
        // Origin mode reset: the cursor moves home — screen top-left
        // (xterm.js resetMode 6).
        cursor.pending_wrap = false;
        cursor.x = 0;
        cursor.y = 0;
    }
}

// ============================================================================
// Scroll region
// ============================================================================

void Screen::setScrollRegion(int top, int bottom)
{
    // DECSTBM: clamp both bounds, then ignore the region when its
    // bottom is not below its top (xterm.js — nothing changes, no
    // cursor move). On a valid set the cursor moves home: to
    // (0, scroll_top) under origin mode, else (0, 0).
    top = std::max(0, std::min(top, lines - 1));
    bottom = std::max(0, std::min(bottom, lines - 1));
    if (bottom <= top) {
        return;
    }
    scrollTop() = top;
    scrollBottom() = bottom;
    cursor.pending_wrap = false;
    cursor.x = 0;
    cursor.y = mode(kDecom, true) ? scrollTop() : 0;
}

// ============================================================================
// Cursor motion
// ============================================================================

void Screen::carriageReturn()
{
    cursor.pending_wrap = false;
    cursor.x = 0;
}

void Screen::lineFeed()
{
    // LF: move down one line; at the region bottom, scroll the region
    // (a feed below the region moves down until the absolute bottom,
    // where it is a no-op — xterm.js). Under newline mode (NLM), the
    // feed also returns the cursor to column 0.
    //
    // An explicit line feed clears the wrapped marker on the line it
    // lands on (xterm.js: only an explicit feed clears it, not CR).
    cursor.pending_wrap = false;
    if (mode(kNlm)) {
        cursor.x = 0;
    }
    if (cursor.y == scrollBottom()) {
        scrollRegion(scrollTop(), scrollBottom(), 1);
    } else if (cursor.y != lines - 1) {
        cursor.y += 1;
        activeGrid()[cursor.y].wrapped = false;
    }
}

void Screen::backspace()
{
    cursor.pending_wrap = false;
    cursor.x = std::max(0, cursor.x - 1);
}

void Screen::tab()
{
    tabForward(1);
}

void Screen::setTabStop()
{
    // HTS: set a tab stop at the cursor column. Not cursor motion —
    // the pending-wrap flag survives (xterm.js tabSet).
    tabStops().insert(cursor.x);
}

void Screen::clearTabStop(int mode)
{
    // TBC: 0 clears the stop at the cursor column, 3 clears all stops;
    // other modes are unsupported and ignored (xterm.js).
    if (mode == 0) {
        tabStops().erase(cursor.x);
    } else if (mode == 3) {
        tabStops().clear();
    }
}

void Screen::tabForward(int n)
{
    // CHT: move forward `n` tab stops; past the last stop the cursor
    // stays at the wrap position (real xterm xterm_next_tab returns
    // `cols`).
    for (int i = 0; i < n; ++i) {
        const int stop = nextStop(cursor.x);
        if (stop >= columns) {
            cursor.x = columns - 1;
        } else {
            cursor.pending_wrap = false;
            cursor.x = stop;
        }
    }
}

void Screen::tabBackward(int n)
{
    // CBT: move backward `n` tab stops, clamped at 0 (xterm.js
    // cursorBackwardTab).
    cursor.pending_wrap = false;
    for (int i = 0; i < n; ++i) {
        cursor.x = prevStop(cursor.x);
    }
}

int Screen::nextStop(int x) const
{
    // The first stop strictly after `x`, or `columns` — the wrap
    // position — when there is none (real xterm xterm_next_tab).
    const auto& stops = m_screens[m_active].tabStops;
    for (int i = x + 1; i < columns; ++i) {
        if (stops.find(i) != stops.end()) {
            return i;
        }
    }
    return columns;
}

int Screen::prevStop(int x) const
{
    // The first stop strictly before `x`, clamped to 0 (xterm.js
    // Buffer.prevStop).
    const auto& stops = m_screens[m_active].tabStops;
    for (int i = x - 1; i >= 0; --i) {
        if (stops.find(i) != stops.end()) {
            return i;
        }
    }
    return 0;
}

void Screen::cursorUp(int n)
{
    // CUU: up n, clamped at the region top; above the region, free
    // motion clamped at the screen top (xterm.js diffToTop).
    cursor.pending_wrap = false;
    if (cursor.y >= scrollTop()) {
        cursor.y = std::max(scrollTop(), cursor.y - n);
    } else {
        cursor.y = std::max(0, cursor.y - n);
    }
}

void Screen::cursorDown(int n)
{
    // CUD: down n, clamped at the region bottom; below the region, free
    // motion clamped at the screen bottom (xterm.js diffToBottom).
    cursor.pending_wrap = false;
    if (cursor.y <= scrollBottom()) {
        cursor.y = std::min(scrollBottom(), cursor.y + n);
    } else {
        cursor.y = std::min(lines - 1, cursor.y + n);
    }
}

void Screen::cursorForward(int n)
{
    cursor.pending_wrap = false;
    cursor.x = std::min(columns - 1, cursor.x + n);
}

void Screen::cursorBackward(int n)
{
    cursor.pending_wrap = false;
    cursor.x = std::max(0, cursor.x - n);
}

void Screen::cursorNextLine(int n)
{
    // CNL: down n lines (clamped cursor motion — no scroll), then to
    // column 0.
    cursorDown(n);
    cursor.x = 0;
}

void Screen::cursorPrecedingLine(int n)
{
    cursorUp(n);
    cursor.x = 0;
}

void Screen::setCursor(int x, int y)
{
    // CUP/HVP: absolute position, 0-based here (the emulator converts
    // from 1-based). Under origin mode, `y` is relative to the region
    // top and the cursor is clamped to the region; else it is clamped
    // to the screen.
    cursor.pending_wrap = false;
    if (mode(kDecom, true)) {
        y = std::max(scrollTop(), std::min(scrollBottom(), scrollTop() + y));
    } else {
        y = std::max(0, std::min(lines - 1, y));
    }
    cursor.x = std::max(0, std::min(columns - 1, x));
    cursor.y = y;
}

void Screen::index()
{
    // IND: move down one line; at the region bottom, scroll the region
    // (below the region, free motion to the absolute bottom). Unlike
    // LF, an index does not clear the wrapped marker.
    cursor.pending_wrap = false;
    if (cursor.y == scrollBottom()) {
        scrollRegion(scrollTop(), scrollBottom(), 1);
    } else if (cursor.y != lines - 1) {
        cursor.y += 1;
    }
}

void Screen::reverseIndex()
{
    // RI: move up one line; at the region top, scroll the region down —
    // a fresh blank line at the top, the bottom row pushed out
    // (xterm.js reverseIndex).
    cursor.pending_wrap = false;
    if (cursor.y == scrollTop()) {
        scrollRegionDown(scrollTop(), scrollBottom(), 1);
    } else {
        cursor.y = std::max(0, cursor.y - 1);
    }
}

// ============================================================================
// Erase
// ============================================================================

void Screen::eraseInDisplay(int mode)
{
    // ED: erase with erase fill. 0: from the cursor down (rows below
    // are reset to fresh blanks); 1: from the top to the cursor (rows
    // above reset; the current row's wrapped marker is always cleared,
    // and the next row's too when the whole row was erased); 2:
    // everything (every row reset). A row erased from column 0 loses
    // its wrapped marker (xterm.js eraseInDisplay).
    const int y = cursor.y;
    const int x = cursor.x;
    auto& grid = activeGrid();
    if (mode == 0) {
        auto& row = grid[y];
        replaceCells(row, x, columns);
        if (x == 0) {
            row.wrapped = false;
        }
        for (int yy = y; yy < lines; ++yy) {
            markDirty(yy);
        }
        for (int yy = y + 1; yy < lines; ++yy) {
            grid[yy] = eraseRow();
        }
    } else if (mode == 1) {
        auto& row = grid[y];
        replaceCells(row, 0, x + 1);
        row.wrapped = false;
        if (x + 1 >= columns && y + 1 < lines) {
            grid[y + 1].wrapped = false;
        }
        for (int yy = 0; yy <= y; ++yy) {
            markDirty(yy);
        }
        for (int yy = 0; yy < y; ++yy) {
            grid[yy] = eraseRow();
        }
    } else if (mode == 2) {
        markAllDirty();
        for (int yy = 0; yy < lines; ++yy) {
            grid[yy] = eraseRow();
        }
    }
}

void Screen::eraseInLine(int mode)
{
    // EL: erase on the current row with erase fill. 0: from the cursor
    // to the row end; 1: from the row start to the cursor (inclusive);
    // 2: the whole row. A full-row erase (0 from column 0, or 2) clears
    // the wrapped marker; EL 1 never does (xterm.js).
    auto& row = activeGrid()[cursor.y];
    markDirty(cursor.y);
    if (mode == 0) {
        replaceCells(row, cursor.x, columns);
        if (cursor.x == 0) {
            row.wrapped = false;
        }
    } else if (mode == 1) {
        replaceCells(row, 0, cursor.x + 1);
    } else if (mode == 2) {
        replaceCells(row, 0, columns);
        row.wrapped = false;
    }
}

void Screen::eraseChars(int n)
{
    // ECH: erase `n` cells from the cursor rightward, clamped to the
    // row end. The wrapped marker survives (xterm.js eraseChars).
    auto& row = activeGrid()[cursor.y];
    markDirty(cursor.y);
    const int count = std::min(n, columns - cursor.x);
    replaceCells(row, cursor.x, cursor.x + count);
}

void Screen::replaceCells(Row& row, int start, int end)
{
    // Erase cells [start, end) with erase fill, cleaning wide-char
    // edges (xterm.js BufferLine.replaceCells): a lead split by the
    // start is blanked, and a continuation stub whose lead is erased
    // is blanked.
    if (start && !row.cells[start - 1].data.empty() && wcwidth(row.cells[start - 1].data[0]) == 2) {
        row.cells[start - 1] = eraseFill();
    }
    if (end < columns && !row.cells[end - 1].data.empty() && wcwidth(row.cells[end - 1].data[0]) == 2) {
        row.cells[end] = eraseFill();
    }
    for (int i = start; i < end; ++i) {
        row.cells[i] = eraseFill();
    }
}

Row Screen::eraseRow()
{
    // A blank row stamped with the erase fill — the fill xterm.js uses
    // for lines scrolled in by LF/IND/RI/SU/IL/DL and for rows reset by
    // ED (cursor's bg, default fg).
    return Row{std::vector<Cell>(columns, eraseFill())};
}

// ============================================================================
// Charsets
// ============================================================================

void Screen::designateCharset(const std::string& designator, const std::string& charset)
{
    // `ESC ( C` etc: fill the slot named by `designator` (G0–G3) with
    // the charset named by `charset`. Unknown charset names are
    // parse-and-ignore — the slot keeps its previous set (xterm).
    int slot = -1;
    if (designator == "(") {
        slot = 0;
    } else if (designator == ")") {
        slot = 1;
    } else if (designator == "*") {
        slot = 2;
    } else if (designator == "+") {
        slot = 3;
    }
    if (slot >= 0 && (charset == "B" || charset == "A" || charset == "0")) {
        m_charsets[slot] = charset;
    }
}

void Screen::shiftCharset(int level)
{
    // SI/SO, `ESC n`/`o`, `ESC ~`/`}`/`|`: make the slot at `level` the
    // active one, so print translates through its map.
    m_charsetLevel = level;
}

// ============================================================================
// Save / restore
// ============================================================================

void Screen::saveState()
{
    // DECSC / CSI s: record the cursor (position + rendition), the
    // charset slots and active level, and the origin/wraparound modes
    // into the single save slot. The cursor is not touched.
    savedState() = SavedState{
        cursor,
        m_charsets,
        m_charsetLevel,
        mode(kDecom, true),
        mode(kDecawm, true),
    };
}

void Screen::restoreState()
{
    // DECRC / CSI u: restore the saved state — modes first (their
    // set/reset home the cursor, which the position restore then
    // overrides), then the cursor clamped into the region under origin
    // mode, else the screen. A restore before any save is a no-op.
    auto& saved = savedState();
    if (!saved.has_value()) {
        return;
    }
    if (saved->decom) {
        setMode(kDecom, true);
    } else {
        resetMode(kDecom, true);
    }
    if (saved->decawm) {
        setMode(kDecawm, true);
    } else {
        resetMode(kDecawm, true);
    }
    cursor = saved->cursor;
    cursor.pending_wrap = false;
    if (mode(kDecom, true)) {
        cursor.y = std::max(scrollTop(), std::min(scrollBottom(), cursor.y));
    } else {
        cursor.y = std::max(0, std::min(lines - 1, cursor.y));
    }
    cursor.x = std::max(0, std::min(columns - 1, cursor.x));
    m_charsets = saved->charsets;
    m_charsetLevel = saved->charsetLevel;
}

// ============================================================================
// Alternate screen (ADR-0004)
// ============================================================================

void Screen::effectiveRendition(int x, int y, int& fg, int& bg) const
{
    // The (fg, bg) a renderer should draw for the cell at (x, y): the
    // cell's own colors with reverse video applied. Two reverse sources
    // stack by XOR — the SGR `reverse` attribute and the DECSCNM mode
    // (`?5`) — so both on cancels out, either alone swaps fg/bg.
    const Cell& cell = activeGrid()[y].cells[x];
    if (cell.reverse != (m_decModes.find(5) != m_decModes.end())) {
        fg = cell.bg;
        bg = cell.fg;
    } else {
        fg = cell.fg;
        bg = cell.bg;
    }
}

void Screen::decaln()
{
    // DECALN (`ESC # 8`, the screen alignment test): every row of the
    // active grid becomes `E` in the cursor's *full current rendition*
    // — foreground, background and SGR attributes — not the erase fill.
    // The wrapped markers are cleared, and the cursor is homed before
    // and after; tab stops, the scroll region, the DECSC slot, the
    // modes and the scrollback are untouched (ADR-0006).
    cursor.x = 0;
    cursor.y = 0;
    cursor.pending_wrap = false;
    const Cell fill{U"E", cursor.fg, cursor.bg, cursor.bold, cursor.underline,
                    cursor.reverse, cursor.blink, cursor.dim, cursor.italic,
                    cursor.hidden, cursor.strike, cursor.overline};
    auto& grid = activeGrid();
    grid.clear();
    grid.reserve(lines);
    for (int i = 0; i < lines; ++i) {
        grid.push_back(Row{std::vector<Cell>(columns, fill)});
    }
    markAllDirty();
    cursor.x = 0;
    cursor.y = 0;
    cursor.pending_wrap = false;
}

void Screen::enterAltScreen()
{
    // DECSET 47/1047/1049: switch to the alternate grid. The alt grid
    // is filled with the erase fill and the cursor position is carried
    // over (xterm.js activateAltBuffer); the rendition is shared, so it
    // is not saved here — `?1049` wraps this with save_state/restore in
    // the emulator. Re-entering while already on the alternate screen
    // is a no-op (xterm.js), so the alt content survives a redundant
    // DECSET.
    if (m_active == 1) {
        return;
    }
    auto& state = m_screens[1];
    state.grid.clear();
    state.grid.reserve(lines);
    for (int i = 0; i < lines; ++i) {
        state.grid.push_back(eraseRow());
    }
    state.x = cursor.x;
    state.y = cursor.y;
    state.pendingWrap = cursor.pending_wrap;
    m_active = 1;
    markAllDirty();
}

void Screen::leaveAltScreen()
{
    // DECRST 47/1047/1049: switch back to the normal grid. The alt's
    // live cursor position carries back (xterm.js), then the alt grid
    // is cleared. A leave while already on the normal screen is a
    // no-op.
    if (m_active == 0) {
        return;
    }
    auto& state = m_screens[1];
    state.x = cursor.x;
    state.y = cursor.y;
    state.pendingWrap = cursor.pending_wrap;
    // Symmetric with the entry fill: the cleared grid uses the erase
    // fill, like xterm.js fillViewportRows on deactivation.
    state.grid.clear();
    state.grid.reserve(lines);
    for (int i = 0; i < lines; ++i) {
        state.grid.push_back(eraseRow());
    }
    m_active = 0;
    markAllDirty();
}

// ============================================================================
// Resize
// ============================================================================

void Screen::resize(int newLines, int newColumns)
{
    // Resize both grids, re-wrapping every line at the new width
    // (ADR-0003: reflow, not clip) — the normal and the alternate
    // screen reflow independently (ADR-0004); the inactive one resizes
    // invisibly. The normal screen reflows its history and grid as
    // **one stream** (ADR-0006), so a wrapped line spanning the
    // boundary re-joins exactly as if the screen were taller; the
    // newest `lines` rows stay the grid, the rest remain history.
    // Shrinking the height keeps each grid's bottom lines — the newest
    // rows, blank or not. Wrapped rows re-join the row above on widen
    // (the marker rides the reflow).
    if (newLines < 1 || newColumns < 1) {
        throw std::invalid_argument("resize: lines and columns must be >= 1");
    }
    lines = newLines;
    columns = newColumns;
    for (int index = 0; index < 2; ++index) {
        auto& state = m_screens[index];
        std::vector<Row> reflowed;
        if (index == 0) {
            // One-stream reflow (ADR-0006): history + grid re-wrap
            // together; the newest `lines` rows become the grid.
            std::vector<Row> stream = state.scrollback;
            stream.insert(stream.end(), state.grid.begin(), state.grid.end());
            reflowed = reflowRows(stream, columns);
        } else {
            reflowed = reflowRows(state.grid, columns);
        }
        if (newLines >= static_cast<int>(state.grid.size())) {
            // Growing or steady height: the stream's trailing blank
            // separators are padding the grid re-pads anyway — drop
            // them so the newest `lines` rows are the content.
            trimTrailingBlankRows(reflowed);
        }
        std::vector<Row> kept;
        if (static_cast<int>(reflowed.size()) >= newLines) {
            kept.assign(reflowed.end() - newLines, reflowed.end());
        } else {
            kept = reflowed;
        }
        if (index == 0) {
            state.scrollback.clear();
            if (static_cast<int>(reflowed.size()) > newLines) {
                state.scrollback.assign(reflowed.begin(), reflowed.end() - newLines);
            }
            // The history's trailing blank separators are padding (the
            // grid's bottom blanks stayed in `kept`); drop them so the
            // scrollbar range ends at the last content row.
            trimTrailingBlankRows(state.scrollback);
            state.scrollOffset = std::min(state.scrollOffset,
                                          static_cast<int>(state.scrollback.size()));
            // The history rows are trimmed by the reflow (trailing
            // padding dropped) — re-pad them to the new width so a
            // scrolled-up viewport renders every column.
            for (auto& row : state.scrollback) {
                row.cells.resize(columns, Cell::blank());
            }
        }
        state.grid.clear();
        for (auto& row : kept) {
            row.cells.resize(columns, Cell::blank());
            state.grid.push_back(std::move(row));
        }
        while (static_cast<int>(state.grid.size()) < newLines) {
            state.grid.push_back(blankRow());
        }
        // Scroll region resets to full screen; tab stops to every-8
        // (xterm.js setupTabStops); saved positions clamp.
        state.scrollTop = 0;
        state.scrollBottom = newLines - 1;
        state.tabStops.clear();
        for (int x = 0; x < columns; x += 8) {
            state.tabStops.insert(x);
        }
        state.x = std::min(state.x, columns - 1);
        state.y = std::min(state.y, newLines - 1);
        state.pendingWrap = false;
        if (state.savedState.has_value()) {
            state.savedState->cursor.x = std::min(state.savedState->cursor.x, columns - 1);
            state.savedState->cursor.y = std::min(state.savedState->cursor.y, newLines - 1);
        }
    }
    cursor.y = std::min(cursor.y, lines - 1);
    cursor.x = std::min(cursor.x, columns - 1);
    cursor.pending_wrap = false;
    markAllDirty();
}

std::vector<Row> Screen::reflowRows(const std::vector<Row>& rows, int newColumns)
{
    // Re-wrap the text of `rows` at `newColumns`, preserving the graphic
    // rendition of each glyph. A row marked wrapped continues the row
    // above; an unwrapped row starts a new line — so distinct full-width
    // rows no longer merge on widen (ADR-0003). Within a logical line,
    // every re-wrapped segment after the first is marked wrapped, so
    // narrow → widen round-trips re-join the same line.
    //
    // Only each row's *trailing* padding is dropped (it re-pads at the
    // new width): leading and interior blanks are kept, and a fully
    // blank row stays as a blank separator once content has been
    // emitted. Wide characters fill two cells; a glyph that no longer
    // fits at the row's end moves to the next. Trailing blank rows are
    // kept — the caller (`resize`) splits the stream into history and
    // grid by the newest `lines` rows.
    std::vector<Row> out;
    std::vector<Cell> outRow;
    int x = 0;
    bool cont = false;        // the current out_row continues the previous output row
    bool pendingCont = false; // the next out_row to start will continue
    bool emitted = false;
    const Cell blank = Cell::blank();
    for (const auto& row : rows) {
        bool allBlank = true;
        for (const auto& cell : row.cells) {
            if (!(cell == blank)) {
                allBlank = false;
                break;
            }
        }
        if (allBlank) {
            if (emitted) {
                // A blank line between content: flush and keep it as a
                // separator (padded by the caller).
                if (!outRow.empty()) {
                    out.push_back(Row{outRow, cont});
                    outRow.clear();
                    x = 0;
                }
                out.push_back(Row{});
                pendingCont = false;
            }
            continue;
        }
        emitted = true;
        if (!row.wrapped && !outRow.empty()) {
            // A new line, not a continuation: flush what we have.
            out.push_back(Row{outRow, cont});
            outRow.clear();
            x = 0;
            pendingCont = false;
        }
        if (row.wrapped && outRow.empty()) {
            // A continuation row with nothing pending yet: it joins the
            // previous output row when the next cell lands.
            pendingCont = true;
        }
        // Trim only the trailing padding: find the last non-blank cell.
        int last = -1;
        for (int i = static_cast<int>(row.cells.size()) - 1; i >= 0; --i) {
            if (!(row.cells[i] == blank)) {
                last = i;
                break;
            }
        }
        for (int i = 0; i <= last; ++i) {
            const Cell& cell = row.cells[i];
            if (cell.data.empty()) {
                continue; // blank continuation of a wide char
            }
            const int width = wcwidth(cell.data[0]);
            if (width == 0) {
                // Combining mark: attach to the previous glyph.
                if (!outRow.empty()) {
                    outRow.back().data += cell.data;
                }
                continue;
            }
            if (width == 2 && x >= newColumns - 1) {
                // A wide glyph that no longer fits at the row's end
                // moves to the next line, which it continues.
                out.push_back(Row{outRow, cont});
                outRow.clear();
                x = 0;
                pendingCont = true;
            }
            if (outRow.empty()) {
                cont = pendingCont;
                pendingCont = false;
            }
            outRow.push_back(cell);
            x += width;
            if (width == 2) {
                Cell continuation = Cell::blank();
                continuation.data.clear();
                outRow.push_back(continuation);
            }
            if (x >= newColumns) {
                out.push_back(Row{outRow, cont});
                outRow.clear();
                x = 0;
                pendingCont = true;
            }
        }
    }
    if (!outRow.empty()) {
        out.push_back(Row{outRow, cont});
    }
    return out;
}

// ============================================================================
// Scrolling
// ============================================================================

void Screen::scrollUp(int n)
{
    // SU: scroll the region up by `n` lines; the top `n` lines are
    // discarded, `n` erase-fill lines appear at the bottom. The cursor
    // is not touched (xterm.js scrollUp).
    scrollRegion(scrollTop(), scrollBottom(), n);
}

void Screen::scrollDown(int n)
{
    // SD: scroll the region down by `n` lines; the bottom `n` lines are
    // discarded, `n` default-attr blank lines appear at the top. SD
    // alone fills with default attributes, not the erase fill
    // (xterm.js scrollDown). The cursor is not touched.
    const Row fill = blankRow();
    scrollRegionDown(scrollTop(), scrollBottom(), n, &fill);
}

// ============================================================================
// Row ops
// ============================================================================

void Screen::insertLines(int n)
{
    // IL: insert `n` blank lines at the cursor within the scroll
    // region; rows below shift down, the region's bottom `n` rows are
    // pushed out. No effect outside the region (xterm.js insertLines).
    // The cursor returns to column 0; inserted lines are never wrapped.
    if (cursor.y > scrollBottom() || cursor.y < scrollTop()) {
        return;
    }
    cursor.pending_wrap = false;
    cursor.x = 0;
    for (int y = scrollTop(); y <= scrollBottom(); ++y) {
        markDirty(y);
    }
    auto& grid = activeGrid();
    for (int i = 0; i < n; ++i) {
        grid.erase(grid.begin() + scrollBottom());
        grid.insert(grid.begin() + cursor.y, eraseRow());
    }
}

void Screen::deleteLines(int n)
{
    // DL: delete `n` lines at the cursor within the scroll region; rows
    // below shift up, `n` erase-fill lines appear at the region bottom.
    // No effect outside the region (xterm.js deleteLines). The cursor
    // returns to column 0.
    if (cursor.y > scrollBottom() || cursor.y < scrollTop()) {
        return;
    }
    cursor.pending_wrap = false;
    cursor.x = 0;
    for (int y = scrollTop(); y <= scrollBottom(); ++y) {
        markDirty(y);
    }
    auto& grid = activeGrid();
    for (int i = 0; i < n; ++i) {
        grid.erase(grid.begin() + cursor.y);
        grid.insert(grid.begin() + scrollBottom(), eraseRow());
    }
}

void Screen::insertChars(int n)
{
    // ICH: insert `n` erase-fill cells at the cursor, shifting the rest
    // of the row right; cells past the edge are dropped. The cursor
    // does not move (xterm.js insertChars).
    markDirty(cursor.y);
    insertCells(cursor.y, cursor.x, n);
}

void Screen::deleteChars(int n)
{
    // DCH: delete `n` cells at the cursor, shifting the rest of the row
    // left; `n` erase-fill cells appear at the row end (xterm.js
    // BufferLine.deleteCells). A wide lead split by the deletion is
    // blanked, and a continuation cell left without its lead is blanked.
    markDirty(cursor.y);
    auto& row = activeGrid()[cursor.y];
    const int x = cursor.x;
    if (n < columns - x) {
        for (int i = 0; i < columns - x - n; ++i) {
            row.cells[x + i] = row.cells[x + n + i];
        }
        for (int i = columns - n; i < columns; ++i) {
            row.cells[i] = eraseFill();
        }
    } else {
        for (int i = x; i < columns; ++i) {
            row.cells[i] = eraseFill();
        }
    }
    if (x && !row.cells[x - 1].data.empty() && wcwidth(row.cells[x - 1].data[0]) == 2) {
        // The cell before the deletion point was a wide lead split by
        // the shift: blank it (xterm.js).
        row.cells[x - 1] = eraseFill();
    }
    if (row.cells[x].data.empty()) {
        // A continuation cell whose lead was shifted or erased
        // (xterm.js: width 0 without content).
        row.cells[x] = eraseFill();
    }
}

// ============================================================================
// Scroll primitives
// ============================================================================

void Screen::scrollRegion(int top, int bottom, int n, const Row* fill)
{
    // Scroll the region [top, bottom] up by `n` lines; the top `n`
    // lines leave the grid, `n` erase-fill lines appear at the bottom.
    //
    // A full-screen scroll on the normal screen pushes the leaving rows
    // into the scrollback (ADR-0006); a narrowed region discards them,
    // and the alternate screen never writes history.
    const bool fullScreen = top == 0 && bottom == lines - 1;
    shiftRegion(top, bottom, n, fill, /*up=*/true,
                /*scrollback=*/fullScreen && m_active == 0);
}

void Screen::shiftRegion(int top, int bottom, int n, const Row* fill, bool up,
                         bool scrollback)
{
    // Shift the region [top, bottom] by `n` lines (up or down): the
    // leaving lines are discarded — or pushed to scrollback when `up`
    // with `scrollback` — and `n` erase-fill lines appear at the other
    // edge. The whole region marks dirty.
    auto& grid = activeGrid();
    for (int i = 0; i < n; ++i) {
        if (up) {
            if (scrollback) {
                pushScrollback(grid[top]);
            }
            for (int y = top; y < bottom; ++y) {
                grid[y] = grid[y + 1];
            }
            grid[bottom] = fill != nullptr ? *fill : eraseRow();
        } else {
            for (int y = bottom; y > top; --y) {
                grid[y] = grid[y - 1];
            }
            grid[top] = fill != nullptr ? *fill : eraseRow();
        }
    }
    for (int y = top; y <= bottom; ++y) {
        markDirty(y);
    }
}

void Screen::pushScrollback(const Row& row)
{
    // A row leaving the top of the normal grid enters history, bounded
    // by the cap — oldest dropped first (ADR-0006).
    if (scrollbackLimit == 0) {
        return;
    }
    auto& sb = m_screens[0].scrollback;
    sb.push_back(row);
    if (static_cast<int>(sb.size()) > scrollbackLimit) {
        sb.erase(sb.begin(), sb.begin() + (sb.size() - scrollbackLimit));
    }
}

void Screen::scrollRegionDown(int top, int bottom, int n, const Row* fill)
{
    // Scroll the region [top, bottom] down by `n` lines (reverse
    // index); the bottom `n` lines are discarded, `n` erase-fill lines
    // appear at the top. Reverse scrolls never read or write the
    // scrollback (ADR-0006 — the spec's retention contract).
    shiftRegion(top, bottom, n, fill, /*up=*/false, /*scrollback=*/false);
}

} // namespace qtermx