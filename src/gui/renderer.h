#ifndef QTERMX_RENDERER_H
#define QTERMX_RENDERER_H

// Snapshot → pixels (Slice B). The renderer paints viewport rows into a
// QImage backing store the widget blits in paintEvent; it never touches
// the model — it consumes only frozen `Snapshot` rows (ADR-0005) (port
// of pyqtermx render.py).
//
// Cell colors: `-1` is the default, `0–255` a 256-color palette index
// (0–15 xterm, 16–231 the 6×6×6 cube, 232–255 grayscale), and
// `>= 0x1000000` an RGB value. SGR support here: bold (palette colors
// 0–7 step up to their bright entries), reverse (fg/bg swap), dim (fg
// mixed halfway toward bg), underline, strike, overline, hidden (no
// glyph), italic (font flag), and DECSCNM ?5 (whole-screen reverse).
//
// Box-drawing (U+2500–257F), block characters (U+2580–259F), and
// geometric shapes are drawn as vectors — painter drawLine/fillRect/
// drawEllipse/drawPolygon from the `kVectorGlyphs` primitive table —
// not through the font: box and block glyphs join seamlessly across
// cells, and small shapes stay crisp instead of antialiasing to a
// speck. Braille stays in the font. SGR blink is parsed but not yet
// painted; the *cursor* blink is the widget's job — `paint` takes a
// `cursorVisible` override the widget's timer drives.

#include <optional>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include <QColor>
#include <QFont>
#include <QImage>
#include <QPainter>
#include <QStaticText>

#include "palette.h"
#include "screen.h"
#include "selection.h"
#include "session.h"

namespace qtermx::gui {

// libc++ (C++17) has no std::hash for tuples — combine the string key
// with the two bools (one fold).
struct StaticKeyHash {
    size_t operator()(const std::tuple<std::string, bool, bool>& t) const
    {
        size_t h = std::hash<std::string>{}(std::get<0>(t));
        h ^= std::hash<bool>{}(std::get<1>(t)) + 0x9E3779B9 + (h << 6) + (h >> 2);
        h ^= std::hash<bool>{}(std::get<2>(t)) + 0x9E3779B9 + (h << 6) + (h >> 2);
        return h;
    }
};

// Cursor styles: the focused block (inverted character / solid block)
// and the unfocused hollow — a rectangle around the cell that leaves
// the character underneath visible.
inline constexpr const char* kCursorBlock = "block";
inline constexpr const char* kCursorOutline = "outline";

// One drawing instruction in `kVectorGlyphs` — the geometry contract is
// documented on the table itself. Fields are positional per kind
// (cell-relative floats; `u` is the cell's smaller side):
// - kFill:  (fx, fy, fw, fh) in a..d, role 0 = fg, 1 = bg
// - kLine:  (x1, y1, x2, y2) in a..d — a stroke (fg), endpoints inclusive
// - kArc:   (cx, cy, r, a0, a1) in a..e — radius r × u, angles in degrees
// - kSquare/kCircle: (size, cx, cy) in a..c, role 0 = fill, 1 = ring
// - kPoly:  (size, cx, cy) in a..c, role 0 = fill, 1 = ring, then vertex
//   pairs (vx, vy) in d..j — coordinates in the size × u box
struct Primitive {
    enum Kind { kFill, kLine, kArc, kSquare, kCircle, kPoly } kind;
    float a = 0, b = 0, c = 0, d = 0, e = 0, f = 0, g = 0, h = 0, i = 0, j = 0;
    int role = 0;
};

// A centered polygon primitive (diamond/triangle) — vertices in the
// `size × u` box, centered like the shapes.
inline Primitive polyPrim(float size, int style, std::initializer_list<float> verts)
{
    Primitive p;
    p.kind = Primitive::kPoly;
    p.a = size;
    p.role = style;
    float* v = &p.d;
    size_t n = 0;
    for (float x : verts) {
        if (n < 7) {
            v[n++] = x;
        }
    }
    return p;
}

// A fillRect primitive — `role` 0 = fg, 1 = bg.
inline Primitive fillPrim(float fx, float fy, float fw, float fh, int role)
{
    Primitive p;
    p.kind = Primitive::kFill;
    p.a = fx;
    p.b = fy;
    p.c = fw;
    p.d = fh;
    p.role = role;
    return p;
}

// A stroke primitive (fg), endpoints inclusive.
inline Primitive linePrim(float x1, float y1, float x2, float y2)
{
    Primitive p;
    p.kind = Primitive::kLine;
    p.a = x1;
    p.b = y1;
    p.c = x2;
    p.d = y2;
    return p;
}

// An arc primitive (fg), radius `r × u`, angles in degrees.
inline Primitive arcPrim(float cx, float cy, float r, float a0, float a1)
{
    Primitive p;
    p.kind = Primitive::kArc;
    p.a = cx;
    p.b = cy;
    p.c = r;
    p.d = a0;
    p.e = a1;
    return p;
}

// A centered shape primitive — `kind` kSquare or kCircle, side/diameter
// `size × u`, offset by `cx × u` / `cy × u`; `style` 0 = fill, 1 = ring.
inline Primitive shapePrim(Primitive::Kind kind, float size, float cx, float cy, int style)
{
    Primitive p;
    p.kind = kind;
    p.a = size;
    p.b = cx;
    p.c = cy;
    p.role = style;
    return p;
}

// Vector-drawn glyphs — codepoint → cell-relative primitives. The font
// is only used where it is good; these glyphs are painted directly so
// adjacent cells join seamlessly (box/block), and so tiny geometric
// shapes (spinner dots, bullets) survive antialiasing instead of
// washing out to a speck. One table, one draw path — no per-glyph
// exceptions.
inline const std::unordered_map<char32_t, std::vector<Primitive>>& vectorGlyphs()
{
    static const std::unordered_map<char32_t, std::vector<Primitive>> table = {
        // -- Box drawing (U+2500–257F): strokes reach the cell edges so
        //    adjacent cells join without font gaps.
        {0x2500, {linePrim(0.0f, 0.5f, 1.0f, 0.5f)}}, // ─
        {0x2502, {linePrim(0.5f, 0.0f, 0.5f, 1.0f)}}, // │
        {0x250C, {linePrim(0.5f, 0.5f, 1.0f, 0.5f), linePrim(0.5f, 0.5f, 0.5f, 1.0f)}}, // ┌
        {0x2510, {linePrim(0.0f, 0.5f, 0.5f, 0.5f), linePrim(0.5f, 0.5f, 0.5f, 1.0f)}}, // ┐
        {0x2514, {linePrim(0.5f, 0.5f, 1.0f, 0.5f), linePrim(0.5f, 0.5f, 0.5f, 0.0f)}}, // └
        {0x2518, {linePrim(0.0f, 0.5f, 0.5f, 0.5f), linePrim(0.5f, 0.5f, 0.5f, 0.0f)}}, // ┘
        {0x251C, {linePrim(0.5f, 0.0f, 0.5f, 1.0f), linePrim(0.5f, 0.5f, 1.0f, 0.5f)}}, // ├
        {0x2524, {linePrim(0.5f, 0.0f, 0.5f, 1.0f), linePrim(0.0f, 0.5f, 0.5f, 0.5f)}}, // ┤
        {0x252C, {linePrim(0.0f, 0.5f, 1.0f, 0.5f), linePrim(0.5f, 0.5f, 0.5f, 1.0f)}}, // ┬
        {0x2534, {linePrim(0.0f, 0.5f, 1.0f, 0.5f), linePrim(0.5f, 0.5f, 0.5f, 0.0f)}}, // ┴
        {0x253C, {linePrim(0.0f, 0.5f, 1.0f, 0.5f), linePrim(0.5f, 0.0f, 0.5f, 1.0f)}}, // ┼
        // Rounded corners: an arc in the cell center plus two legs.
        {0x256D, {arcPrim(0.5f, 0.5f, 0.25f, 90.0f, 90.0f), linePrim(0.0f, 0.5f, 0.25f, 0.5f),
                  linePrim(0.5f, 0.0f, 0.5f, 0.25f)}}, // ╭
        {0x256E, {arcPrim(0.5f, 0.5f, 0.25f, 90.0f, -90.0f), linePrim(0.75f, 0.5f, 1.0f, 0.5f),
                  linePrim(0.5f, 0.0f, 0.5f, 0.25f)}}, // ╮
        {0x256F, {arcPrim(0.5f, 0.5f, 0.25f, 180.0f, -90.0f), linePrim(0.0f, 0.5f, 0.25f, 0.5f),
                  linePrim(0.5f, 0.75f, 0.5f, 1.0f)}}, // ╯
        {0x2570, {arcPrim(0.5f, 0.5f, 0.25f, 0.0f, -90.0f), linePrim(0.75f, 0.5f, 1.0f, 0.5f),
                  linePrim(0.5f, 0.75f, 0.5f, 1.0f)}}, // ╰
        // -- Block characters (U+2580–259F): the whole cell in the
        //    background, the lit quadrants in the foreground.
        {0x2588, {fillPrim(0.0f, 0.0f, 1.0f, 1.0f, 1), fillPrim(0.0f, 0.0f, 1.0f, 1.0f, 0)}}, // █
        {0x258C, {fillPrim(0.0f, 0.0f, 1.0f, 1.0f, 1), fillPrim(0.0f, 0.0f, 0.5f, 1.0f, 0)}}, // ▌
        {0x2590, {fillPrim(0.0f, 0.0f, 1.0f, 1.0f, 1), fillPrim(0.5f, 0.0f, 0.5f, 1.0f, 0)}}, // ▐
        {0x2580, {fillPrim(0.0f, 0.0f, 1.0f, 1.0f, 1), fillPrim(0.0f, 0.0f, 1.0f, 0.5f, 0)}}, // ▀
        {0x2584, {fillPrim(0.0f, 0.0f, 1.0f, 1.0f, 1), fillPrim(0.0f, 0.5f, 1.0f, 0.5f, 0)}}, // ▄
        {0x259D, {fillPrim(0.0f, 0.0f, 1.0f, 1.0f, 1), fillPrim(0.5f, 0.0f, 0.5f, 0.5f, 0)}}, // ▝
        {0x2598, {fillPrim(0.0f, 0.0f, 1.0f, 1.0f, 1), fillPrim(0.0f, 0.0f, 0.5f, 0.5f, 0)}}, // ▘
        {0x2596, {fillPrim(0.0f, 0.0f, 1.0f, 1.0f, 1), fillPrim(0.0f, 0.5f, 0.5f, 0.5f, 0)}}, // ▖
        {0x2597, {fillPrim(0.0f, 0.0f, 1.0f, 1.0f, 1), fillPrim(0.5f, 0.5f, 0.5f, 0.5f, 0)}}, // ▗
        {0x259B, {fillPrim(0.0f, 0.0f, 1.0f, 1.0f, 1), fillPrim(0.0f, 0.0f, 1.0f, 0.5f, 0),
                  fillPrim(0.0f, 0.5f, 0.5f, 0.5f, 0)}}, // ▛
        {0x259C, {fillPrim(0.0f, 0.0f, 1.0f, 1.0f, 1), fillPrim(0.0f, 0.0f, 1.0f, 0.5f, 0),
                  fillPrim(0.5f, 0.5f, 0.5f, 0.5f, 0)}}, // ▜
        {0x2599, {fillPrim(0.0f, 0.0f, 1.0f, 1.0f, 1), fillPrim(0.0f, 0.0f, 0.5f, 0.5f, 0),
                  fillPrim(0.0f, 0.5f, 1.0f, 0.5f, 0)}}, // ▙
        {0x259F, {fillPrim(0.0f, 0.0f, 1.0f, 1.0f, 1), fillPrim(0.5f, 0.0f, 0.5f, 0.5f, 0),
                  fillPrim(0.0f, 0.5f, 1.0f, 0.5f, 0)}}, // ▟
        {0x259A, {fillPrim(0.0f, 0.0f, 1.0f, 1.0f, 1), fillPrim(0.0f, 0.0f, 0.5f, 0.5f, 0),
                  fillPrim(0.5f, 0.5f, 0.5f, 0.5f, 0)}}, // ▚
        {0x259E, {fillPrim(0.0f, 0.0f, 1.0f, 1.0f, 1), fillPrim(0.5f, 0.0f, 0.5f, 0.5f, 0),
                  fillPrim(0.0f, 0.5f, 0.5f, 0.5f, 0)}}, // ▞
        // -- Geometric shapes: centered, scaled by the cell's smaller
        //    side so they stay square on tall fonts.
        {0x2B1D, {shapePrim(Primitive::kSquare, 0.30f, 0.0f, 0.0f, 0)}}, // ⬝
        {0x25AA, {shapePrim(Primitive::kSquare, 0.35f, 0.0f, 0.0f, 0)}}, // ▪
        {0x25AB, {shapePrim(Primitive::kSquare, 0.35f, 0.0f, 0.0f, 1)}}, // ▫
        {0x25FC, {shapePrim(Primitive::kSquare, 0.50f, 0.0f, 0.0f, 0)}}, // ◼
        {0x25FB, {shapePrim(Primitive::kSquare, 0.50f, 0.0f, 0.0f, 1)}}, // ◻
        {0x25A0, {shapePrim(Primitive::kSquare, 0.80f, 0.0f, 0.0f, 0)}}, // ■
        {0x25A1, {shapePrim(Primitive::kSquare, 0.80f, 0.0f, 0.0f, 1)}}, // □
        {0x2B1B, {shapePrim(Primitive::kSquare, 0.90f, 0.0f, 0.0f, 0)}}, // ⬛
        {0x2B1C, {shapePrim(Primitive::kSquare, 0.90f, 0.0f, 0.0f, 1)}}, // ⬜
        {0x25CF, {shapePrim(Primitive::kCircle, 0.55f, 0.0f, 0.0f, 0)}}, // ●
        {0x25CB, {shapePrim(Primitive::kCircle, 0.55f, 0.0f, 0.0f, 1)}}, // ○
        {0x2B24, {shapePrim(Primitive::kCircle, 0.75f, 0.0f, 0.0f, 0)}}, // ⬤
        {0x2022, {shapePrim(Primitive::kCircle, 0.40f, 0.0f, 0.0f, 0)}}, // • bullet
        {0x00B7, {shapePrim(Primitive::kCircle, 0.30f, 0.0f, 0.0f, 0)}}, // · middle dot
        {0x25C6, {polyPrim(0.60f, 0, {0.5f, 0.0f, 1.0f, 0.5f, 0.5f, 1.0f, 0.0f, 0.5f})}}, // ◆
        {0x25C7, {polyPrim(0.60f, 1, {0.5f, 0.0f, 1.0f, 0.5f, 0.5f, 1.0f, 0.0f, 0.5f})}}, // ◇
        {0x2B25, {polyPrim(0.45f, 0, {0.5f, 0.0f, 1.0f, 0.5f, 0.5f, 1.0f, 0.0f, 0.5f})}}, // ⬥
        {0x2B29, {polyPrim(0.35f, 0, {0.5f, 0.0f, 1.0f, 0.5f, 0.5f, 1.0f, 0.0f, 0.5f})}}, // ⬩
        {0x25B2, {polyPrim(0.70f, 0, {0.5f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f})}},           // ▲
        {0x25BC, {polyPrim(0.70f, 0, {0.5f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f})}},           // ▼
        {0x25C0, {polyPrim(0.70f, 0, {0.0f, 0.5f, 1.0f, 0.0f, 1.0f, 1.0f})}},           // ◀
        {0x25B6, {polyPrim(0.70f, 0, {1.0f, 0.5f, 0.0f, 0.0f, 0.0f, 1.0f})}},           // ▶
        {0x25B3, {polyPrim(0.70f, 1, {0.5f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f})}},           // △
        {0x25BD, {polyPrim(0.70f, 1, {0.5f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f})}},           // ▽
        {0x25C1, {polyPrim(0.70f, 1, {0.0f, 0.5f, 1.0f, 0.0f, 1.0f, 1.0f})}},           // ◁
        {0x25B7, {polyPrim(0.70f, 1, {1.0f, 0.5f, 0.0f, 0.0f, 0.0f, 1.0f})}},           // ▷
    };
    return table;
}

// The terminal's default colors — the `-1` cell colors. Single source
// of truth: palette.h, shared with the emulator's OSC replies.
inline QColor defaultFg()
{
    return QColor(palette::kDefaultFgRgb[0], palette::kDefaultFgRgb[1], palette::kDefaultFgRgb[2]);
}
inline QColor defaultBg()
{
    return QColor(palette::kDefaultBgRgb[0], palette::kDefaultBgRgb[1], palette::kDefaultBgRgb[2]);
}

// Cap for the renderer color flyweight: the 256-palette entries fit
// comfortably, leaving headroom for arbitrary RGB ints — bounded like
// screen's cell intern cap, so a stream of unique RGB colors (a rainbow
// dump) cannot grow the cache without bound.
inline constexpr int kColorCacheCap = 4096;

// Cap for the QStaticText layout cache — bounded like the color cache.
inline constexpr int kStaticCacheCap = 4096;

class TerminalRenderer {
public:
    TerminalRenderer(const QFont& font = QFont(), bool antialias = true);

    // -- Metrics ---------------------------------------------------------

    double cellW() const { return m_cellW; }
    double cellH() const { return m_cellH; }
    QRectF cellRect(int viewportRow, int col) const;

    // -- Theming ---------------------------------------------------------

    // Replace the glyph font and re-derive the cell grid metrics. The
    // widget must rebuild its backing and re-post the resize after
    // calling this (cellW/cellH changed).
    void setFont(const QFont& font);
    // Replace the default foreground/background colors (the terminal's
    // `-1` cell colors). The widget must re-render after calling this —
    // the color cache is cleared so defaults are not baked into cached
    // cells.
    void setPalette(const QColor& fg, const QColor& bg);

    const QFont& font() const { return m_font; }
    const QColor& defaultFgColor() const { return m_defaultFg; }
    const QColor& defaultBgColor() const { return m_defaultBg; }

    // -- Painting --------------------------------------------------------

    // A prepared QStaticText layout for `text` in the (bold, italic)
    // font variant plus the vertical offset drawing it at the top of a
    // cell row — cached (see kStaticCacheCap). Public for the tests
    // (the Python exposes `_static_text` the same way).
    std::pair<QStaticText, double> staticText(const std::string& text, bool bold, bool italic);

    // The static-text cache size (test seam — the bounded-cache
    // contract).
    size_t staticCacheSize() const { return m_staticCache.size(); }

    // Paint the snapshot's rows into `image` (the CPU path — also the
    // test seam: pixel checks read the image). `full` snapshots carry
    // every viewport row at indices 0..lines-1; incremental snapshots
    // carry only the dirty rows at their viewport indices. `rows`
    // overrides the snapshot's rows with a merged viewport (the
    // widget's persistent grid). `rowIndices` limits the repaint to
    // specific viewport rows. `selection` (viewport coordinates) renders
    // the selected cells reversed. `cursorVisible` overrides the
    // snapshot's DECTCEM visibility for the cursor gate (the widget's
    // blink phase) — nullopt keeps the snapshot's value, and the
    // override is ANDed with it. `cursorStyle` picks the cursor's look:
    // kCursorBlock (the focused inverted block) or kCursorOutline (the
    // unfocused hollow rectangle).
    void render(QImage& image, const Snapshot& snapshot,
                const std::vector<Row>* rows = nullptr,
                const std::vector<int>* rowIndices = nullptr,
                const Selection* selection = nullptr,
                std::optional<bool> cursorVisible = std::nullopt,
                const char* cursorStyle = kCursorBlock);

    // Paint the snapshot onto an open painter — the widget's backing
    // renderer (the CPU path renders into a QImage, then blits it).
    void paint(QPainter& painter, const Snapshot& snapshot, int viewportLines,
               const std::vector<Row>* rows = nullptr,
               const std::vector<int>* rowIndices = nullptr,
               const Selection* selection = nullptr,
               std::optional<bool> cursorVisible = std::nullopt,
               const char* cursorStyle = kCursorBlock);

private:
    QColor color(int color, const QColor& def, bool bright = false);
    QFont fontFor(bool bold, bool italic);
    double staticTextY(const QStaticText& st);
    void applyFont(const QFont& font);

    void paintRow(QPainter& painter, int viewportRow, const Row& row, bool reverseVideo,
                  const std::optional<std::pair<int, int>>& selRange);
    void drawVectorGlyph(QPainter& painter, const QRectF& rect, char32_t cp, const QColor& fg,
                         const QColor& bg);
    void paintCursor(QPainter& painter, const Snapshot& snapshot, int viewportLines,
                     const std::vector<Row>* rows,
                     const std::optional<std::pair<int, int>>& selRange,
                     const char* cursorStyle);

    QFont m_font;
    double m_cellW = 0;
    double m_cellH = 0;
    QColor m_defaultFg = defaultFg();
    QColor m_defaultBg = defaultBg();
    // Derived fonts by (bold, italic) — a per-cell QFont would be
    // hundreds of allocations per frame. Key: (bold << 1) | italic.
    std::unordered_map<int, QFont> m_fontCache;
    // Cell color ints → QColor, keyed (color, bright). Key:
    // (color << 1) | bright — color is < 0x2000000, so no collision.
    std::unordered_map<int64_t, QColor> m_colorCache;
    // Glyph-run text → prepared QStaticText, keyed (text, bold, italic).
    std::unordered_map<std::tuple<std::string, bool, bool>, QStaticText, StaticKeyHash>
        m_staticCache;
};

} // namespace qtermx::gui

#endif // QTERMX_RENDERER_H