#ifndef QTERMX_SCREEN_H
#define QTERMX_SCREEN_H

// The screen model — the dumb grid the renderer reads (port of pyqtermx
// screen.py). The screen owns the display grid (cells), the cursor, and
// the scroll primitives. It knows nothing about escape sequences; the
// emulator turns parse events into calls on this model.
//
// Qt-free: no Qt includes. The grid is dense: every cell materialized,
// each row its own vector, so scrollback and resize reflow attach without
// rewriting the grid.

#include <array>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace qtermx {

// Default screen size, VT102 canonical.
inline constexpr int kDefaultLines = 24;
inline constexpr int kDefaultColumns = 80;

// Mode numbers, named after their DEC/ANSI numbers. DEC-private modes
// (with `?` prefix) live in their own namespace.
inline constexpr int kIrm = 4;      // insert mode
inline constexpr int kDecom = 6;    // origin mode
inline constexpr int kDecawm = 7;   // autowrap
inline constexpr int kDectcem = 25; // cursor visible
inline constexpr int kNlm = 20;     // newline mode

// RGB colors (SGR 38;2 / 48;2) are ints with the high bit set —
// `(r << 16) | (g << 8) | b | 0x1000000` — so they can never collide
// with the -1 default or the 0–255 palette indices (ADR-0004).
inline constexpr int kRgbMarker = 0x1000000;

// Encode an RGB color as a cell color int (ADR-0004).
inline int rgb(int r, int g, int b)
{
    return (r << 16) | (g << 8) | b | kRgbMarker;
}

// True for RGB cell colors (never true for -1 or a palette index).
inline bool isRgb(int color)
{
    return color >= kRgbMarker;
}

// Decode an RGB cell color into (r, g, b).
inline void rgbParts(int color, int& r, int& g, int& b)
{
    r = (color >> 16) & 0xFF;
    g = (color >> 8) & 0xFF;
    b = color & 0xFF;
}

// One glyph plus its graphic rendition. `fg`/`bg` are ints: -1 the
// default, 0–255 the 256-color palette, and >= 0x1000000 an RGB value.
// Cells are immutable value types (the Python flyweight cache is
// unnecessary in C++ — value semantics make sharing moot).
struct Cell {
    std::u32string data = U" ";
    int fg = -1;
    int bg = -1;
    // VT102 Character Attributes, extended with the full SGR set.
    bool bold = false;
    bool underline = false;
    bool reverse = false;
    bool blink = false;
    bool dim = false;
    bool italic = false;
    bool hidden = false;
    bool strike = false;
    bool overline = false;

    static Cell blank() { return Cell{}; }

    bool operator==(const Cell& other) const
    {
        return data == other.data && fg == other.fg && bg == other.bg &&
               bold == other.bold && underline == other.underline &&
               reverse == other.reverse && blink == other.blink &&
               dim == other.dim && italic == other.italic &&
               hidden == other.hidden && strike == other.strike &&
               overline == other.overline;
    }
};

// One row of the grid: the cells plus the wrapped marker. `wrapped`
// marks a row whose content continues from the row above (xterm.js
// isWrapped): set on the row a wrap lands on, cleared by an explicit
// line feed, by full-row erase, or by DECALN — not by cursor motion or
// row/column shifts. The marker rides with the row through scroll and
// reflow, which consults it to re-join rows when widening.
struct Row {
    std::vector<Cell> cells;
    bool wrapped = false;
};

// The printable position plus what printing stamps on cells.
// `pending_wrap` is the deferred-wrap flag: after a print lands in the
// last column, the cursor sits there until the next printable character
// (or any cursor motion) resolves it.
struct Cursor {
    int x = 0;
    int y = 0;
    bool pending_wrap = false;
    int fg = -1;
    int bg = -1;
    bool bold = false;
    bool underline = false;
    bool reverse = false;
    bool blink = false;
    bool dim = false;
    bool italic = false;
    bool hidden = false;
    bool strike = false;
    bool overline = false;
};

// The DECSC save slot: the cursor (position + full rendition), the four
// charset slots and active level, and the origin/wraparound modes. Tab
// stops and the scroll region are deliberately not saved (xterm.js
// saveCursor).
struct SavedState {
    Cursor cursor;
    std::array<std::string, 4> charsets;
    int charsetLevel = 0;
    bool decom = false;
    bool decawm = false;
};

// Everything that travels with a grid (ADR-0004): the rows, the cursor
// *position* (the rendition is shared — one copy on the screen), the
// scroll region, the tab stops, and the DECSC save slot. One state per
// grid: index 0 the normal screen, 1 the alternate.
//
// The normal state additionally owns the scrollback (ADR-0006): the
// retained history rows above the grid plus the viewport offset. The
// alternate screen has neither.
struct ScreenState {
    std::vector<Row> grid;
    int scrollTop = 0;
    int scrollBottom = 0;
    std::set<int> tabStops;
    std::optional<SavedState> savedState;
    // Retained history (ADR-0006): rows pushed off the top of the
    // normal grid by full-screen scrolling, oldest first. Bounded by
    // scrollbackLimit; only ED3 erases it.
    std::vector<Row> scrollback;
    // The viewport offset — rows up from the bottom (0 = live output).
    int scrollOffset = 0;
    // Cursor position snapshot (write-only bookkeeping, see screen.py).
    int x = 0;
    int y = 0;
    bool pendingWrap = false;
};

class Screen {
public:
    Screen(int lines = kDefaultLines, int columns = kDefaultColumns,
           int scrollbackLimit = 1000);

    // Public data members mirror the Python API surface (screen.py
    // exposes `lines`/`columns`/`scrollback_limit`/`cursor` as plain
    // attributes) — the port keeps the same names deliberately, so the
    // m_ prefix convention is waived here (AGENTS.md: "Document WHY").
    int lines = kDefaultLines;
    int columns = kDefaultColumns;
    // Scrollback cap (ADR-0006): how many history rows the normal
    // screen retains, oldest dropped first. 0 disables scrollback.
    int scrollbackLimit = 1000;

    // The active cursor: per-grid position, shared rendition (ADR-0004).
    Cursor cursor;

    // -- Read API -------------------------------------------------------

    // The row at `y`: cells plus the wrapped marker.
    Row& line(int y) { return activeGrid()[y]; }
    const Row& line(int y) const { return activeGrid()[y]; }

    // The whole grid as text — one row per line, attrs stripped (UTF-8).
    std::string render() const;

    // -- Viewport (scrollback read API, ADR-0006) ------------------------

    int scrollbackLen() const;
    int viewportOffset() const;
    bool altScreen() const;
    // The k-th row of the visible viewport (top to bottom): with the
    // scroll offset applied, history rows above the grid, then grid
    // rows. The renderer's only scrollback read seam.
    const Row& viewportRow(int k) const;
    // Scroll the viewport by `n` rows (positive = up), clamped to the
    // history. Model state — the renderer mirrors it from snapshots and
    // posts commands; it never writes it directly (ADR-0005).
    void scroll(int n);
    // Snap the viewport to the live output (offset 0).
    void scrollToBottom();
    // ED3 (`ESC[3J`): erase the retained history and snap the viewport
    // to the bottom. The grid is untouched (ADR-0006).
    void clearScrollback();

    // -- Change tracking (ADR-0005) ---------------------------------------

    // The rows whose content changed since the last call — the snapshot
    // transport's change list, consumed and cleared.
    std::set<int> takeDirtyRows();

    // -- Printing ---------------------------------------------------------

    // Stamp `text` into the grid at the cursor, advancing it. Wrap is
    // deferred: a pending wrap from a previous print resolves only when
    // the next printable arrives (autowrap off — DECAWM — instead
    // overwrites in place at the last column). Wide characters fill
    // their cell plus a blank continuation cell; combining marks attach
    // to the cell behind the cursor. Under insert mode (IRM), each
    // character shifts the rest of the row right first.
    void print(std::u32string_view text);

    // -- Graphic rendition -------------------------------------------------

    void resetRendition();
    void setFg(int color);
    void setBg(int color);
    void setBold(bool on = true);
    void setDim(bool on = true);
    void setItalic(bool on = true);
    void setUnderline(bool on = true);
    void setBlink(bool on = true);
    void setReverse(bool on = true);
    void setHidden(bool on = true);
    void setStrike(bool on = true);
    void setOverline(bool on = true);

    // -- Modes -------------------------------------------------------------

    bool mode(int number, bool privateMode = false) const;
    void setMode(int number, bool privateMode = false);
    void resetMode(int number, bool privateMode = false);

    // -- Scroll region ------------------------------------------------------

    void setScrollRegion(int top, int bottom);

    // -- Cursor motion ------------------------------------------------------

    void carriageReturn();
    void lineFeed();
    void backspace();
    void tab();
    void setTabStop();
    void clearTabStop(int mode);
    void tabForward(int n = 1);
    void tabBackward(int n = 1);
    void cursorUp(int n = 1);
    void cursorDown(int n = 1);
    void cursorForward(int n = 1);
    void cursorBackward(int n = 1);
    void cursorNextLine(int n = 1);
    void cursorPrecedingLine(int n = 1);
    void setCursor(int x, int y);
    void index();
    void reverseIndex();

    // -- Erase --------------------------------------------------------------

    void eraseInDisplay(int mode = 0);
    void eraseInLine(int mode = 0);
    void eraseChars(int n = 1);

    // -- Charsets -----------------------------------------------------------

    void designateCharset(const std::string& designator, const std::string& charset);
    void shiftCharset(int level);

    // -- Save / restore -----------------------------------------------------

    void saveState();
    void restoreState();

    // -- Alternate screen (ADR-0004) ----------------------------------------

    // The (fg, bg) a renderer should draw for the cell at (x, y): the
    // cell's own colors with reverse video applied (XOR stacking of SGR
    // reverse and DECSCNM `?5`). `render()` itself stays text-only.
    void effectiveRendition(int x, int y, int& fg, int& bg) const;
    void decaln();
    void enterAltScreen();
    void leaveAltScreen();

    // -- Resize --------------------------------------------------------------

    // Resize both grids, re-wrapping every line at the new width
    // (ADR-0003: reflow, not clip). Throws std::invalid_argument for
    // degenerate sizes (lines < 1 or columns < 1).
    void resize(int lines, int columns);

    // -- Scrolling -----------------------------------------------------------

    void scrollUp(int n = 1);
    void scrollDown(int n = 1);

    // -- Row ops -------------------------------------------------------------

    void insertLines(int n = 1);
    void deleteLines(int n = 1);
    void insertChars(int n = 1);
    void deleteChars(int n = 1);

    // The active screen's scroll region bounds (Python exposes
    // scroll_top/scroll_bottom as properties — the tests read them).
    int& scrollTop() { return m_screens[m_active].scrollTop; }
    int& scrollBottom() { return m_screens[m_active].scrollBottom; }

    // The active screen's tab stops (Python exposes `_tab_stops` — the
    // tests read them; the m_ prefix waiver applies as above).
    std::set<int>& tabStops() { return m_screens[m_active].tabStops; }

private:
    // Active modes, one set per namespace (ANSI / DEC-private). DECAWM
    // starts on — autowrap is the default; DECTCEM too — the cursor
    // starts visible. Modes are shared across grids (ADR-0004).
    std::set<int> m_ansiModes;
    std::set<int> m_decModes{kDecawm, kDectcem};
    // Charset slots G0–G3, each named by its designation final ("B"
    // ASCII by default). Shared.
    std::array<std::string, 4> m_charsets{"B", "B", "B", "B"};
    // The active slot: print translates ASCII through this slot's map.
    int m_charsetLevel = 0;
    // The two grids' states: [0] normal, [1] alternate (ADR-0004).
    std::array<ScreenState, 2> m_screens;
    // Which grid is active.
    int m_active = 0;
    // Rows whose content changed since the last take (ADR-0005).
    std::set<int> m_dirtyRows;
    // Cached erase-fill cell: invalidated when cursor.bg changes.
    Cell m_cachedEraseFill;
    int m_cachedEraseBg = -2; // sentinel: never equal to a real bg

    // -- Per-screen delegation (ADR-0004) -----------------------------------

    std::vector<Row>& activeGrid() { return m_screens[m_active].grid; }
    const std::vector<Row>& activeGrid() const { return m_screens[m_active].grid; }
    std::optional<SavedState>& savedState() { return m_screens[m_active].savedState; }

    // -- Construction helpers ------------------------------------------------

    std::vector<Row> blankRows();
    Row blankRow();

    // -- Printing helpers -----------------------------------------------------

    void insertCells(int y, int x, int n);
    const Cell& eraseFill();
    void attachCombining(char32_t ch);
    void markWrapped(int y);
    bool resolveWrap();

    // -- Erase helpers ---------------------------------------------------------

    void replaceCells(Row& row, int start, int end);
    Row eraseRow();

    // -- Tab helpers ------------------------------------------------------------

    int nextStop(int x) const;
    int prevStop(int x) const;

    // -- Resize helper -----------------------------------------------------------

    static std::vector<Row> reflowRows(const std::vector<Row>& rows, int newColumns);

    // -- Scroll helpers ------------------------------------------------------------

    void scrollRegion(int top, int bottom, int n, const Row* fill = nullptr);
    void shiftRegion(int top, int bottom, int n, const Row* fill, bool up,
                     bool scrollback);
    void pushScrollback(const Row& row);
    void scrollRegionDown(int top, int bottom, int n, const Row* fill = nullptr);

    // -- Change tracking ------------------------------------------------------------

    void markDirty(int y) { m_dirtyRows.insert(y); }
    void markAllDirty();
};

} // namespace qtermx

#endif // QTERMX_SCREEN_H