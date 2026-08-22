#ifndef QTERMX_EMULATOR_H
#define QTERMX_EMULATOR_H

// The emulator — turns parse events into screen operations (port of
// pyqtermx emulator.py). Implements the dispatcher protocol, the seam
// the parser already defines. The screen is the dumb model; the
// emulator decides what each event means.

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "dispatcher.h"
#include "params.h"

namespace qtermx {

class Screen;

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

class Emulator : public Dispatcher {
public:
    using CsiHandler = void (Emulator::*)(const Params&);
    using EscHandler = void (Emulator::*)();
    // The reply callback receives OSC query replies (BEL-terminated,
    // xterm style) — the session wires it to the pty so the child sees
    // its own terminal's colors.
    using ReplyCallback = std::function<void(const std::string&)>;

    explicit Emulator(Screen& screen, ReplyCallback reply = nullptr);

    // CSI dispatch table: (prefix, intermediates, final) → handler. A
    // sequence whose intermediates match no entry falls back to the
    // bare final (no intermediates) — the xterm.js "bare final" rule.
    // Public for the dispatch-completeness tests (port of the Python
    // _CSI_DISPATCH class attribute).
    static const std::unordered_map<std::tuple<std::string, std::string, std::string>,
                                    CsiHandler,
                                    TupleHash<std::tuple<std::string, std::string, std::string>>>
        kCsiDispatch;

    // Escape dispatch table: (intermediates, final) → handler. Exact
    // match only — intermediate-bearing escapes (e.g. `ESC # 8` DECALN)
    // parse-and-ignore until their step, so no bare-final fallback
    // (xterm.js registers ESC handlers by exact key, unlike CSI's
    // bare-final rule).
    static const std::unordered_map<std::tuple<std::string, std::string>, EscHandler,
                                    TupleHash<std::tuple<std::string, std::string>>>
        kEscDispatch;

    // Table lookups, exposed for the dispatch tests (port of the Python
    // _lookup_csi / _lookup_esc).
    static CsiHandler lookupCsi(const std::string& intermediates, const std::string& prefix,
                                const std::string& final);
    static EscHandler lookupEsc(const std::string& intermediates, const std::string& final);

    void chars(std::u32string text) override;
    void execute(int code) override;
    void csiDispatch(std::string intermediates, std::string prefix, Params params,
                     std::string final) override;
    void escapeDispatch(std::string intermediates, std::string final) override;
    void designateCharset(std::string designator, std::string charset) override;
    void oscDispatch(std::u32string payload) override;

    void sm(const Params& params);
    void rm(const Params& params);
    void decset(const Params& params);
    void decrst(const Params& params);
    void sgr(const Params& params);
    void decstbm(const Params& params);
    void cuu(const Params& params);
    void cud(const Params& params);
    void cuf(const Params& params);
    void cub(const Params& params);
    void cnl(const Params& params);
    void cpl(const Params& params);
    void cup(const Params& params);
    void cha(const Params& params);
    void vpa(const Params& params);
    void ed(const Params& params);
    void el(const Params& params);
    void ech(const Params& params);
    void ich(const Params& params);
    void il(const Params& params);
    void dl(const Params& params);
    void dch(const Params& params);
    void su(const Params& params);
    void sd(const Params& params);
    void tbc(const Params& params);
    void cht(const Params& params);
    void cbt(const Params& params);
    void save(const Params& params);
    void restore(const Params& params);

    // ESC handlers (no params).
    void ind();
    void nel();
    void ri();
    void ls2();
    void ls3();
    void ls1r();
    void ls2r();
    void ls3r();
    void hts();
    void decsc();
    void decrc();
    void decaln();

    // Replace the default foreground/background reported to OSC 10/11
    // color queries (hex `#rrggbb` — the `QColor.name(HexRgb)` form the
    // widget forwards).
    void setPalette(const std::string& fg, const std::string& bg);

    // OSC 12 — the cursor color (`#rrggbb`), nullopt for the default
    // inverted block (visible state: snapshots carry it).
    const std::optional<std::string>& cursorColor() const { return m_cursorColor; }

private:
    // SGR extended colors: returns the number of *additional* params
    // consumed (0 when nothing matched).
    int sgrExtended(const Params& params, int i, void (Screen::*setColor)(int));

    // OSC color dispatch (Phase 5, color queries).
    void oscColorQuery(const std::vector<std::string>& fields);
    void oscCursorColor(const std::vector<std::string>& fields);
    void oscReply(const std::string& payload);

    Screen& m_screen;
    ReplyCallback m_reply;
    // The colors OSC 10/11 queries report — the themed defaults
    // (palette.h), replaceable via set_palette.
    std::string m_defaultFg = "#e8e8e8";
    std::string m_defaultBg = "#101010";
    // The 256 palette colors as `#rrggbb` (OSC 4 queries).
    std::vector<std::string> m_palette;
    std::optional<std::string> m_cursorColor;
};

} // namespace qtermx

#endif // QTERMX_EMULATOR_H