#include "emulator.h"

#include <algorithm>
#include <tuple>
#include <unordered_map>

#include "screen.h"

namespace qtermx {

namespace {

// SGR RGB components are 0–255. Values over clamp to 255 — a documented
// deviation (xterm masks them to 8 bits; ADR-0004); a negative value
// (-1, the empty `:` sub-parameter slot) clamps to 0.
int rgbComponent(int value)
{
    return std::max(0, std::min(255, value));
}

// Params values can reach 0xFFFFFFFF (digit overflow, xterm.js
// MAX_VALUE). Screen ops take int; saturate before narrowing so a huge
// value behaves like the Python oracle's arbitrary-precision clamp
// (which clamps to the screen bounds / 255) instead of wrapping
// negative and moving the cursor the wrong way.
int saturate(int64_t v)
{
    if (v > 0x7FFFFFFF) {
        return 0x7FFFFFFF;
    }
    if (v < -0x80000000LL) {
        return -0x80000000;
    }
    return static_cast<int>(v);
}

// SGR 38;2/48;2: the (r, g, b) components at `start` as one RGB cell
// color (ADR-0004).
int rgbParam(const Params& params, int start)
{
    return rgb(rgbComponent(saturate(params.get(start))),
               rgbComponent(saturate(params.get(start + 1))),
               rgbComponent(saturate(params.get(start + 2))));
}

using CsiHandler = void (Emulator::*)(const Params&);
using EscHandler = void (Emulator::*)();

// libc++ (C++17) has no std::hash for tuples — combine the string keys
// (one fold for any tuple arity).
template <typename Tuple>
struct TupleHash {
    size_t operator()(const Tuple& t) const
    {
        size_t h = 0;
        std::apply([&h](const auto&... parts) {
            ((h ^= std::hash<std::string>{}(parts) + 0x9E3779B9 + (h << 6) + (h >> 2)), ...);
        }, t);
        return h;
    }
};

} // namespace

// ============================================================================
// Dispatcher protocol
// ============================================================================

void Emulator::chars(std::u32string text)
{
    m_screen.print(text);
}

void Emulator::execute(int code)
{
    // C0 controls: BS (0x08), HT (0x09), LF (0x0A), VT (0x0B), FF (0x0C),
    // CR (0x0D), SO (0x0E, shift to G1), SI (0x0F, shift to G0). BEL
    // (0x07) is swallowed; anything else is a no-op until its step.
    switch (code) {
    case 0x08:
        m_screen.backspace();
        break;
    case 0x09:
        m_screen.tab();
        break;
    case 0x0A:
    case 0x0B:
    case 0x0C:
        m_screen.lineFeed();
        break;
    case 0x0D:
        m_screen.carriageReturn();
        break;
    case 0x0E:
        m_screen.shiftCharset(1); // SO — G1
        break;
    case 0x0F:
        m_screen.shiftCharset(0); // SI — G0
        break;
    default:
        break; // BEL and the rest: no-op.
    }
}

void Emulator::csiDispatch(std::string intermediates, std::string prefix, Params params,
                           std::string final)
{
    // CSI dispatch table: (prefix, intermediates, final) → handler. A
    // sequence whose intermediates match no entry falls back to the
    // bare final (no intermediates) — the xterm.js "bare final" rule.
    static const std::unordered_map<std::tuple<std::string, std::string, std::string>,
                                    CsiHandler, TupleHash<std::tuple<std::string, std::string, std::string>>>
        kCsiDispatch = {
            {{"", "", "h"}, &Emulator::sm},       // SM — set ANSI modes
            {{"", "", "l"}, &Emulator::rm},       // RM — reset ANSI modes
            {{"?", "", "h"}, &Emulator::decset},  // DECSET
            {{"?", "", "l"}, &Emulator::decrst},  // DECRST
            {{"", "", "m"}, &Emulator::sgr},      // SGR — graphic rendition
            {{"", "", "r"}, &Emulator::decstbm},  // DECSTBM — scroll region
            {{"", "", "A"}, &Emulator::cuu},      // CUU — cursor up
            {{"", "", "B"}, &Emulator::cud},      // CUD — cursor down
            {{"", "", "C"}, &Emulator::cuf},      // CUF — cursor forward
            {{"", "", "D"}, &Emulator::cub},      // CUB — cursor backward
            {{"", "", "E"}, &Emulator::cnl},      // CNL — cursor next line
            {{"", "", "F"}, &Emulator::cpl},      // CPL — cursor preceding line
            {{"", "", "H"}, &Emulator::cup},      // CUP — cursor position
            {{"", "", "f"}, &Emulator::cup},      // HVP — same as CUP
            {{"", "", "G"}, &Emulator::cha},      // CHA — cursor horizontal absolute
            {{"", "", "d"}, &Emulator::vpa},      // VPA — cursor vertical absolute
            {{"", "", "J"}, &Emulator::ed},       // ED — erase in display
            {{"", "", "K"}, &Emulator::el},       // EL — erase in line
            {{"", "", "X"}, &Emulator::ech},      // ECH — erase characters
            {{"", "", "@"}, &Emulator::ich},      // ICH — insert characters
            {{"", "", "L"}, &Emulator::il},       // IL — insert lines
            {{"", "", "M"}, &Emulator::dl},       // DL — delete lines
            {{"", "", "P"}, &Emulator::dch},      // DCH — delete characters
            {{"", "", "S"}, &Emulator::su},       // SU — scroll up
            {{"", "", "T"}, &Emulator::sd},       // SD — scroll down
            {{"", "", "g"}, &Emulator::tbc},      // TBC — tab clear
            {{"", "", "I"}, &Emulator::cht},      // CHT — cursor forward tabulation
            {{"", "", "Z"}, &Emulator::cbt},      // CBT — cursor backward tabulation
            {{"", "", "s"}, &Emulator::save},     // CSI s — save cursor (DECSC alias)
            {{"", "", "u"}, &Emulator::restore},  // CSI u — restore cursor
    };
    auto it = kCsiDispatch.find(std::make_tuple(prefix, intermediates, final));
    if (it == kCsiDispatch.end() && !intermediates.empty()) {
        it = kCsiDispatch.find(std::make_tuple(prefix, "", final));
    }
    if (it != kCsiDispatch.end()) {
        (this->*(it->second))(params);
    }
}

void Emulator::escapeDispatch(std::string intermediates, std::string final)
{
    // Escape dispatch table: (intermediates, final) → handler. Exact
    // match only — intermediate-bearing escapes (e.g. `ESC # 8` DECALN)
    // parse-and-ignore until their step, so no bare-final fallback
    // (xterm.js registers ESC handlers by exact key, unlike CSI's
    // bare-final rule).
    static const std::unordered_map<std::tuple<std::string, std::string>, EscHandler,
                                    TupleHash<std::tuple<std::string, std::string>>>
        kEscDispatch = {
            {{"", "D"}, &Emulator::ind},        // IND — index
            {{"", "E"}, &Emulator::nel},        // NEL — next line (CR + index)
            {{"", "M"}, &Emulator::ri},         // RI — reverse index
            {{"", "n"}, &Emulator::ls2},        // LS2 — shift to G2
            {{"", "o"}, &Emulator::ls3},        // LS3 — shift to G3
            {{"", "~"}, &Emulator::ls1r},       // LS1R — shift to G1
            {{"", "}"}, &Emulator::ls2r},       // LS2R — shift to G2
            {{"", "|"}, &Emulator::ls3r},       // LS3R — shift to G3
            {{"", "H"}, &Emulator::hts},        // HTS — set tab stop
            {{"", "7"}, &Emulator::decsc},      // DECSC — save cursor
            {{"", "8"}, &Emulator::decrc},      // DECRC — restore cursor
            {{"#", "8"}, &Emulator::decaln},    // DECALN — screen alignment test
    };
    auto it = kEscDispatch.find(std::make_tuple(intermediates, final));
    if (it != kEscDispatch.end()) {
        (this->*(it->second))();
    }
}

void Emulator::designateCharset(std::string designator, std::string charset)
{
    m_screen.designateCharset(designator, charset);
}

void Emulator::oscDispatch(std::u32string payload)
{
    // OSC dispatch lands in Phase 5 (title, hyperlinks, clipboard,
    // color queries). Parse-and-ignore for now.
    (void)payload;
}

// ============================================================================
// CSI handlers
// ============================================================================

void Emulator::sm(const Params& params)
{
    for (int i = 0; i < params.count(); ++i) {
        m_screen.setMode(params.get(i));
    }
}

void Emulator::rm(const Params& params)
{
    for (int i = 0; i < params.count(); ++i) {
        m_screen.resetMode(params.get(i));
    }
}

void Emulator::decset(const Params& params)
{
    // DECSET. 47/1047/1049 switch to the alternate screen; 1049 saves
    // the cursor first (xterm.js: saveCursor + fall-through); 1048
    // saves only. Everything else lands in the mode registry.
    for (int i = 0; i < params.count(); ++i) {
        const int mode = params.get(i);
        if (mode == 1049) {
            m_screen.saveState();
            m_screen.enterAltScreen();
        } else if (mode == 47 || mode == 1047) {
            m_screen.enterAltScreen();
        } else if (mode == 1048) {
            m_screen.saveState();
        } else {
            m_screen.setMode(mode, true);
        }
    }
}

void Emulator::decrst(const Params& params)
{
    // DECRST. 47/1047/1049 leave the alternate screen (clearing it);
    // 1049 restores the cursor after (xterm.js); 1048 restores only.
    // Everything else lands in the mode registry.
    for (int i = 0; i < params.count(); ++i) {
        const int mode = params.get(i);
        if (mode == 1049) {
            m_screen.leaveAltScreen();
            m_screen.restoreState();
        } else if (mode == 47 || mode == 1047) {
            m_screen.leaveAltScreen();
        } else if (mode == 1048) {
            m_screen.restoreState();
        } else {
            m_screen.resetMode(mode, true);
        }
    }
}

void Emulator::decstbm(const Params& params)
{
    // DECSTBM: 1-based rows, `CSI r` (no params) resets to full screen;
    // an explicit 0 behaves like 1, and the screen clamps.
    const int top = saturate(params.get(0) ? params.get(0) : 1);
    const int bottom = saturate(params.get(1) ? params.get(1) : m_screen.lines);
    m_screen.setScrollRegion(top - 1, bottom - 1);
}

void Emulator::cuu(const Params& params)
{
    m_screen.cursorUp(params.get(0) ? params.get(0) : 1);
}

void Emulator::cud(const Params& params)
{
    m_screen.cursorDown(params.get(0) ? params.get(0) : 1);
}

void Emulator::cuf(const Params& params)
{
    m_screen.cursorForward(params.get(0) ? params.get(0) : 1);
}

void Emulator::cub(const Params& params)
{
    m_screen.cursorBackward(params.get(0) ? params.get(0) : 1);
}

void Emulator::cnl(const Params& params)
{
    m_screen.cursorNextLine(params.get(0) ? params.get(0) : 1);
}

void Emulator::cpl(const Params& params)
{
    m_screen.cursorPrecedingLine(params.get(0) ? params.get(0) : 1);
}

void Emulator::cup(const Params& params)
{
    // CUP/HVP: 1-based; a single parameter moves to that row in column
    // 0 (xterm.js); missing parameters default to 1.
    const int row = saturate(params.get(0) ? params.get(0) : 1) - 1;
    const int col = params.count() >= 2 ? saturate(params.get(1) ? params.get(1) : 1) - 1 : 0;
    m_screen.setCursor(col, row);
}

void Emulator::cha(const Params& params)
{
    // CHA: cursor to column n (1-based, default 1) of the current row —
    // the row never changes (xterm.js cursorPosition).
    const int col = saturate(params.get(0) ? params.get(0) : 1) - 1;
    m_screen.setCursor(col, m_screen.cursor.y);
}

void Emulator::vpa(const Params& params)
{
    // VPA: cursor to row n (1-based, default 1), column unchanged
    // (xterm.js verticalPosition — origin-relative under DECOM).
    const int row = saturate(params.get(0) ? params.get(0) : 1) - 1;
    m_screen.setCursor(m_screen.cursor.x, row);
}

void Emulator::ed(const Params& params)
{
    // ED: 0/1/2 erase in display; 3 clears the scrollback (ADR-0006 —
    // the only runtime erasure of history).
    const int mode = params.get(0);
    if (mode == 3) {
        m_screen.clearScrollback();
    } else {
        m_screen.eraseInDisplay(mode);
    }
}

void Emulator::el(const Params& params)
{
    m_screen.eraseInLine(params.get(0));
}

void Emulator::ech(const Params& params)
{
    m_screen.eraseChars(params.get(0) ? params.get(0) : 1);
}

void Emulator::ich(const Params& params)
{
    m_screen.insertChars(params.get(0) ? params.get(0) : 1);
}

void Emulator::il(const Params& params)
{
    m_screen.insertLines(params.get(0) ? params.get(0) : 1);
}

void Emulator::dl(const Params& params)
{
    m_screen.deleteLines(params.get(0) ? params.get(0) : 1);
}

void Emulator::dch(const Params& params)
{
    m_screen.deleteChars(params.get(0) ? params.get(0) : 1);
}

void Emulator::su(const Params& params)
{
    m_screen.scrollUp(params.get(0) ? params.get(0) : 1);
}

void Emulator::sd(const Params& params)
{
    m_screen.scrollDown(params.get(0) ? params.get(0) : 1);
}

void Emulator::tbc(const Params& params)
{
    m_screen.clearTabStop(params.get(0));
}

void Emulator::cht(const Params& params)
{
    m_screen.tabForward(params.get(0) ? params.get(0) : 1);
}

void Emulator::cbt(const Params& params)
{
    m_screen.tabBackward(params.get(0) ? params.get(0) : 1);
}

void Emulator::save(const Params& params)
{
    (void)params;
    m_screen.saveState();
}

void Emulator::restore(const Params& params)
{
    (void)params;
    m_screen.restoreState();
}

void Emulator::sgr(const Params& params)
{
    Screen& screen = m_screen;
    int i = 0;
    const int count = params.count();
    while (i < count) {
        const int value = params.get(i);
        if (value == 0) {
            screen.resetRendition();
        } else if (value == 1) {
            screen.setBold();
        } else if (value == 2) {
            screen.setDim();
        } else if (value == 3) {
            screen.setItalic();
        } else if (value == 4) {
            screen.setUnderline();
        } else if (value == 5 || value == 6) {
            screen.setBlink(); // 6 rapid blink collapses to blink
        } else if (value == 7) {
            screen.setReverse();
        } else if (value == 8) {
            screen.setHidden();
        } else if (value == 9) {
            screen.setStrike();
        } else if (value == 21) {
            // Double underline collapses to the boolean underline.
            screen.setUnderline();
        } else if (value == 22) {
            screen.setBold(false);
            screen.setDim(false);
        } else if (value == 23) {
            screen.setItalic(false);
        } else if (value == 24) {
            screen.setUnderline(false);
        } else if (value == 25) {
            screen.setBlink(false);
        } else if (value == 27) {
            screen.setReverse(false);
        } else if (value == 28) {
            screen.setHidden(false);
        } else if (value == 29) {
            screen.setStrike(false);
        } else if (value >= 30 && value <= 37) {
            // SGR codes map onto the 256-color palette's first entries:
            // the cell stores the palette index, not the raw SGR code.
            screen.setFg(value - 30);
        } else if (value == 38) {
            // Extended foreground: `38;5;n` / `38;2;r;g;b` and the colon
            // forms `38:5:n` / `38:2:r:g:b` / `38:2:cs:r:g:b`.
            i += sgrExtended(params, i, &Screen::setFg);
        } else if (value == 39) {
            screen.setFg(-1);
        } else if (value >= 40 && value <= 47) {
            screen.setBg(value - 40);
        } else if (value == 48) {
            // Extended background — same syntaxes as 38.
            i += sgrExtended(params, i, &Screen::setBg);
        } else if (value == 49) {
            screen.setBg(-1);
        } else if (value >= 90 && value <= 97) {
            screen.setFg(value - 90 + 8);
        } else if (value >= 100 && value <= 107) {
            screen.setBg(value - 100 + 8);
        } else if (value == 53) {
            screen.setOverline();
        } else if (value == 55) {
            screen.setOverline(false);
        }
        // Anything else (fonts 10–20, 26, 51/52/54, 56+, 59, 58 extended
        // colors): parse-and-ignore.
        ++i;
    }
}

int Emulator::sgrExtended(const Params& params, int i, void (Screen::*setColor)(int))
{
    // SGR 38/48 — extended colors — for the parameter group at `i`.
    //
    // Handles both syntaxes:
    // - `38;5;n` / `38;2;r;g;b` — semicolon-separated params (the
    //   classic form), consuming 2 or 4 following parameters.
    // - `38:5:n` / `38:2:r:g:b` colon sub-params — xterm's newer form,
    //   self-contained in the group: `(5, n)` a palette index,
    //   `(2, r, g, b)` or `(2, cs, r, g, b)` RGB (cs, the color space,
    //   is accepted and ignored).
    //
    // A truncated or malformed sequence leaves the color untouched —
    // for the semicolon form the leftover components fall through and
    // re-parse as standalone SGR codes (xterm.js-verbatim); for the
    // colon form the group consumes nothing extra. Returns the number
    // of *additional* parameters consumed (0 when nothing matched).
    const std::vector<int64_t> sub = params.subparams(i);
    if (!sub.empty()) {
        // Colon form: everything lives in this group's sub-params.
        if (sub[0] == 2 && (sub.size() == 4 || sub.size() == 5)) {
            const int r = sub.size() == 4 ? sub[1] : sub[2];
            const int g = sub.size() == 4 ? sub[2] : sub[3];
            const int b = sub.size() == 4 ? sub[3] : sub[4];
            (m_screen.*setColor)(rgb(rgbComponent(r), rgbComponent(g), rgbComponent(b)));
        } else if (sub[0] == 5 && sub.size() == 2 && sub[1] >= 0 && sub[1] <= 255) {
            (m_screen.*setColor)(saturate(sub[1]));
        }
        return 0;
    }
    const int count = params.count();
    if (i + 2 < count && params.get(i + 1) == 5) {
        (m_screen.*setColor)(saturate(params.get(i + 2)));
        return 2;
    }
    if (i + 4 < count && params.get(i + 1) == 2) {
        (m_screen.*setColor)(rgbParam(params, i + 2));
        return 4;
    }
    return 0;
}

// ============================================================================
// ESC handlers
// ============================================================================

void Emulator::ind()
{
    m_screen.index(); // IND
}

void Emulator::nel()
{
    // NEL: CR + index (xterm.js nextLine: x = 0, then index) — column
    // 0, then down one line, scrolling the region at its bottom. Unlike
    // LF, an index does not clear the wrapped marker.
    m_screen.carriageReturn();
    m_screen.index();
}

void Emulator::ri()
{
    m_screen.reverseIndex(); // RI
}

void Emulator::ls2() { m_screen.shiftCharset(2); }  // LS2 — G2
void Emulator::ls3() { m_screen.shiftCharset(3); }  // LS3 — G3
void Emulator::ls1r() { m_screen.shiftCharset(1); } // LS1R — G1
void Emulator::ls2r() { m_screen.shiftCharset(2); } // LS2R — G2
void Emulator::ls3r() { m_screen.shiftCharset(3); } // LS3R — G3

void Emulator::hts()
{
    m_screen.setTabStop(); // HTS
}

void Emulator::decsc()
{
    m_screen.saveState(); // DECSC
}

void Emulator::decrc()
{
    m_screen.restoreState(); // DECRC
}

void Emulator::decaln()
{
    m_screen.decaln(); // DECALN — screen alignment test
}

} // namespace qtermx