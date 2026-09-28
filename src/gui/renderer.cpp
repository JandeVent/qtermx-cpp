#include "renderer.h"

#include <algorithm>
#include <cmath>

#include <QFontDatabase>
#include <QFontMetrics>
#include <QFontMetricsF>
#include <QMarginsF>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <QTransform>

#include "palette.h"
#include "utf8_decoder.h"

namespace qtermx::gui {

namespace {

// drawText alignment — a module constant: per-cell `|` on the enums was
// ~0.65 s of the htop profile (enum.__or__ per cell).
constexpr Qt::Alignment kTextFlags = Qt::AlignVCenter | Qt::AlignLeft;

} // namespace

TerminalRenderer::TerminalRenderer(const QFont& font, bool antialias)
{
    // Copy the font so a caller-provided QFont is never mutated.
    if (font.family().isEmpty()) {
        // The platform's native monospace font, falling back to Menlo.
        QFont system = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        if (!system.family().isEmpty()) {
            system.setPixelSize(12);
            m_font = system;
        } else {
            m_font = QFont("Menlo", 12);
        }
    } else {
        m_font = QFont(font);
    }
    if (!antialias) {
        // Opt out (experiments/GL comparisons): 1-bit glyph masks.
        m_font.setStyleStrategy(QFont::NoAntialias);
    }
    applyFont(m_font);
}

void TerminalRenderer::applyFont(const QFont& font)
{
    m_font = QFont(font);
    // Float cell width: QFontMetrics::horizontalAdvance returns an int
    // (rounded), but drawText/QTextLayout position glyphs at the font's
    // true fractional advance. An int cell_w made the grid drift from
    // the glyphs (±0.5px/cell, accumulating across the row). cell_w ==
    // the advance the layout uses, so glyphs land exactly in their
    // cells. cell_h stays int — vertical centering is consistent across
    // runs; the drift is purely horizontal.
    m_cellW = QFontMetricsF(m_font).horizontalAdvance("M");
    m_cellH = QFontMetrics(m_font).height();
    m_fontCache.clear();
    m_colorCache.clear();
    m_staticCache.clear();
}

void TerminalRenderer::setFont(const QFont& font)
{
    applyFont(font);
}

void TerminalRenderer::setPalette(const QColor& fg, const QColor& bg)
{
    m_defaultFg = fg;
    m_defaultBg = bg;
    m_colorCache.clear();
}

QRectF TerminalRenderer::cellRect(int viewportRow, int col) const
{
    return QRectF(col * m_cellW, viewportRow * m_cellH, m_cellW, m_cellH);
}

QColor TerminalRenderer::color(int color, const QColor& def, bool bright)
{
    if (color == -1) {
        return def;
    }
    const int64_t key = (static_cast<int64_t>(color) << 1) | (bright ? 1 : 0);
    const auto it = m_colorCache.find(key);
    if (it != m_colorCache.end()) {
        return it->second;
    }
    QColor qcolor;
    if (isRgb(color)) {
        int r, g, b;
        rgbParts(color, r, g, b);
        qcolor = QColor(r, g, b);
    } else if (color < 8 && bright) {
        qcolor = QColor::fromRgb(palette::kPalette16[color + 8]);
    } else if (color < 16) {
        qcolor = QColor::fromRgb(palette::kPalette16[color]);
    } else if (color < 232) {
        const int value = color - 16;
        qcolor = QColor(palette::kCubeLevels[value / 36], palette::kCubeLevels[(value / 6) % 6],
                        palette::kCubeLevels[value % 6]);
    } else {
        const int gray = 8 + 10 * (color - 232);
        qcolor = QColor(gray, gray, gray);
    }
    if (static_cast<int>(m_colorCache.size()) >= kColorCacheCap) {
        m_colorCache.clear();
    }
    m_colorCache[key] = qcolor;
    return qcolor;
}

QFont TerminalRenderer::fontFor(bool bold, bool italic)
{
    const int key = (bold ? 2 : 0) | (italic ? 1 : 0);
    const auto it = m_fontCache.find(key);
    if (it != m_fontCache.end()) {
        return it->second;
    }
    QFont font(m_font);
    font.setBold(bold);
    font.setItalic(italic);
    m_fontCache[key] = font;
    return font;
}

double TerminalRenderer::staticTextY(const QStaticText& st)
{
    // The vertical offset drawing `st` at the top of a cell row:
    // drawText (AlignVCenter) centers the font's line box, which is the
    // cell height, so a layout as tall as the cell draws top-aligned —
    // and a layout *taller* than the cell (braille: 16 vs 15 in the CI
    // font) must clamp to top-aligned too, not center the taller box
    // (that would shift the dots).
    return std::max(0.0, (m_cellH - st.size().height()) / 2.0);
}

std::pair<QStaticText, double> TerminalRenderer::staticText(const std::string& text, bool bold,
                                                            bool italic)
{
    const auto key = std::make_tuple(text, bold, italic);
    const auto it = m_staticCache.find(key);
    if (it != m_staticCache.end()) {
        return {it->second, staticTextY(it->second)};
    }
    QStaticText st(QString::fromUtf8(text.c_str()));
    st.setTextFormat(Qt::PlainText);
    st.prepare(QTransform(), fontFor(bold, italic));
    if (static_cast<int>(m_staticCache.size()) >= kStaticCacheCap) {
        m_staticCache.clear();
    }
    m_staticCache[key] = st;
    return {st, staticTextY(st)};
}

// -- Painting ------------------------------------------------------------

void TerminalRenderer::render(QImage& image, const Snapshot& snapshot,
                              const std::vector<Row>* rows, const std::vector<int>* rowIndices,
                              const Selection* selection, std::optional<bool> cursorVisible,
                              const char* cursorStyle)
{
    QPainter painter(&image);
    const double dpr = image.devicePixelRatio();
    paint(painter, snapshot, static_cast<int>(std::round(image.height() / (m_cellH * dpr))),
          rows, rowIndices, selection, cursorVisible, cursorStyle);
    painter.end();
}

void TerminalRenderer::paint(QPainter& painter, const Snapshot& snapshot, int viewportLines,
                             const std::vector<Row>* rows, const std::vector<int>* rowIndices,
                             const Selection* selection, std::optional<bool> cursorVisible,
                             const char* cursorStyle)
{
    // Build the (viewport_row, row) pairs: the merged viewport override,
    // else the snapshot's own rows (full: every row; incremental: the
    // dirty rows at their viewport indices).
    std::vector<std::pair<int, const Row*>> pairs;
    if (rows != nullptr) {
        pairs.reserve(rows->size());
        for (size_t i = 0; i < rows->size(); ++i) {
            pairs.emplace_back(static_cast<int>(i), &(*rows)[i]);
        }
    } else if (snapshot.full) {
        pairs.reserve(snapshot.rows.size());
        for (size_t i = 0; i < snapshot.rows.size(); ++i) {
            pairs.emplace_back(static_cast<int>(i), &snapshot.rows[i]);
        }
    } else {
        pairs.reserve(snapshot.dirtyRows.size());
        for (size_t i = 0; i < snapshot.dirtyRows.size() && i < snapshot.rows.size(); ++i) {
            pairs.emplace_back(snapshot.dirtyRows[i], &snapshot.rows[i]);
        }
    }
    if (rowIndices != nullptr) {
        std::vector<std::pair<int, const Row*>> wanted;
        for (const auto& [i, r] : pairs) {
            if (std::find(rowIndices->begin(), rowIndices->end(), i) != rowIndices->end()) {
                wanted.emplace_back(i, r);
            }
        }
        pairs = std::move(wanted);
    }
    for (const auto& [viewportRow, row] : pairs) {
        const auto sel = selection != nullptr ? columnRange(*selection, viewportRow) : std::nullopt;
        paintRow(painter, viewportRow, *row, snapshot.reverseVideo, sel);
    }
    const bool visible = cursorVisible.has_value() ? (snapshot.cursorVisible && *cursorVisible)
                                                   : snapshot.cursorVisible;
    if (visible && snapshot.cursorRow >= 0) {
        const int cursorRow = snapshot.cursorRow + snapshot.viewportOffset;
        if (rowIndices == nullptr ||
            std::find(rowIndices->begin(), rowIndices->end(), cursorRow) != rowIndices->end()) {
            const auto sel = selection != nullptr ? columnRange(*selection, cursorRow) : std::nullopt;
            paintCursor(painter, snapshot, viewportLines, rows, sel, cursorStyle);
        }
    }
}

void TerminalRenderer::paintRow(QPainter& painter, int viewportRow, const Row& row,
                                bool reverseVideo,
                                const std::optional<std::pair<int, int>>& selRange)
{
    // Two passes: all backgrounds, then all glyphs — a wide char's glyph
    // spans its own cell and the empty continuation cell, so the
    // continuation's background must be filled before the glyph (which
    // would otherwise be overwritten by it). The painter stays open —
    // callers own its lifecycle.
    //
    // Hot path: colors and fonts are cached, and adjacent cells sharing
    // a rendition are batched into runs — one fillRect per background
    // run, one drawStaticText per glyph run (the layout cached) — so a
    // full row of uniform text is a handful of Qt calls, not one per
    // cell. Block and wide characters break runs and draw individually.
    const double cw = m_cellW;
    const double ch = m_cellH;
    const double y0 = viewportRow * ch;
    const std::vector<Cell>& cells = row.cells;
    const int n = static_cast<int>(cells.size());

    // Pass 1: backgrounds — one fillRect per run of identical bg.
    int runStart = 0;
    QColor runBg;
    bool haveRunBg = false;
    for (int col = 0; col < n; ++col) {
        const Cell& cell = cells[col];
        QColor bg = color(cell.bg, m_defaultBg);
        if (cell.reverse != reverseVideo) { // SGR 7 XOR DECSCNM ?5
            bg = color(cell.fg, m_defaultFg);
        }
        if (selRange.has_value() && selRange->first <= col && col <= selRange->second) {
            // Selected: the background is the cell's foreground (the
            // glyph pass swaps the other way — they must agree).
            bg = (cell.reverse == reverseVideo)
                     ? color(cell.fg, m_defaultFg, cell.bold)
                     : color(cell.bg, m_defaultBg, cell.bold);
        }
        if (!haveRunBg || bg != runBg) {
            if (haveRunBg) {
                painter.fillRect(QRectF(runStart * cw, y0, (col - runStart) * cw, ch), runBg);
            }
            runBg = bg;
            runStart = col;
            haveRunBg = true;
        }
    }
    if (haveRunBg) {
        painter.fillRect(QRectF(runStart * cw, y0, (n - runStart) * cw, ch), runBg);
    }

    // Pass 2: glyphs — one drawStaticText per run of identical rendition.
    int glyphRunStart = 0;
    std::string runText;
    QColor runFg;
    bool haveRunFg = false;
    int runFontKey = -1;
    bool runUnderline = false;
    bool runStrike = false;
    bool runOverline = false;

    const auto flush = [&](int endCol) {
        if (runText.empty()) {
            return;
        }
        const QRectF rect(glyphRunStart * cw, y0, (endCol - glyphRunStart) * cw, ch);
        painter.setFont(fontFor((runFontKey & 2) != 0, (runFontKey & 1) != 0));
        painter.setPen(runFg);
        const auto [st, off] = staticText(runText, (runFontKey & 2) != 0, (runFontKey & 1) != 0);
        painter.drawStaticText(QPointF(rect.left(), rect.top() + off), st);
        if (runUnderline) {
            painter.fillRect(QRectF(rect.left(), rect.bottom() - 1, rect.width(), 1), runFg);
        }
        if (runStrike) {
            painter.fillRect(QRectF(rect.left(), rect.top() + static_cast<int>(rect.height()) / 2, rect.width(), 1),
                             runFg);
        }
        if (runOverline) {
            painter.fillRect(QRectF(rect.left(), rect.top(), rect.width(), 1), runFg);
        }
        runText.clear();
        haveRunFg = false;
        runFontKey = -1;
        runUnderline = false;
        runStrike = false;
        runOverline = false;
    };

    for (int col = 0; col < n; ++col) {
        const Cell& cell = cells[col];
        if (cell.hidden || cell.data.empty()) {
            flush(col);
            continue; // continuation cells draw no glyph
        }
        QColor fg = color(cell.fg, m_defaultFg, cell.bold);
        QColor bg = color(cell.bg, m_defaultBg);
        if (cell.reverse != reverseVideo) {
            // SGR 7 XOR DECSCNM: fg/bg swap — bold-is-bright applies
            // after the swap (xterm behavior).
            fg = color(cell.bg, m_defaultBg, cell.bold);
            bg = color(cell.fg, m_defaultFg);
        }
        if (selRange.has_value() && selRange->first <= col && col <= selRange->second) {
            std::swap(fg, bg); // selection renders reversed
        }
        if (cell.dim) {
            // SGR 2: fg mixed halfway toward bg (xterm faint).
            fg = QColor((fg.red() + bg.red()) / 2, (fg.green() + bg.green()) / 2,
                        (fg.blue() + bg.blue()) / 2);
        }
        const char32_t cp = cell.data.size() == 1 ? cell.data[0] : 0;
        const bool wide = col + 1 < n && cells[col + 1].data.empty();
        const auto& glyphs = vectorGlyphs();
        const bool isVector = glyphs.find(cp) != glyphs.end();
        if (isVector || wide) {
            // Vector glyphs and wide chars break the run and draw
            // individually (wide chars are text; vector glyphs are
            // primitives).
            flush(col);
            QRectF rect(col * cw, y0, cw, ch);
            if (wide) {
                rect.setWidth(2 * cw); // a wide char: one glyph across two cells
            }
            if (isVector) {
                drawVectorGlyph(painter, rect, cp, fg, bg);
            } else {
                painter.setFont(fontFor(cell.bold, cell.italic));
                painter.setPen(fg);
                painter.drawText(rect, kTextFlags, QString::fromStdU32String(cell.data));
            }
            if (cell.underline) {
                painter.fillRect(QRectF(rect.left(), rect.bottom() - 1, rect.width(), 1), fg);
            }
            if (cell.strike) {
                painter.fillRect(QRectF(rect.left(), rect.top() + static_cast<int>(rect.height()) / 2, rect.width(),
                                        1),
                                 fg);
            }
            if (cell.overline) {
                painter.fillRect(QRectF(rect.left(), rect.top(), rect.width(), 1), fg);
            }
            continue;
        }
        const int key = (cell.bold ? 2 : 0) | (cell.italic ? 1 : 0);
        if (haveRunFg && fg == runFg && key == runFontKey && cell.underline == runUnderline &&
            cell.strike == runStrike && cell.overline == runOverline) {
            runText += encodeUtf8(cell.data);
        } else {
            flush(col);
            glyphRunStart = col;
            runFg = fg;
            runFontKey = key;
            runUnderline = cell.underline;
            runStrike = cell.strike;
            runOverline = cell.overline;
            haveRunFg = true;
            runText += encodeUtf8(cell.data);
        }
    }
    flush(n);
}

void TerminalRenderer::drawVectorGlyph(QPainter& painter, const QRectF& rect, char32_t cp,
                                       const QColor& fg, const QColor& bg)
{
    // Paint a vector glyph's primitives — the single draw path for every
    // non-font glyph: block quadrant fills and the centered geometric
    // shapes. `fg`/`bg` are the cell's rendered colors
    // (reverse/selection/dim already applied). `rect` is a float cell
    // rect — strokes land at fractional cell boundaries, so adjacent
    // cells join exactly.
    const double x = rect.left();
    const double y = rect.top();
    const double w = rect.width();
    const double h = rect.height();
    const double u = std::min(w, h);
    const auto& glyphs = vectorGlyphs();
    const auto it = glyphs.find(cp);
    if (it == glyphs.end()) {
        // Unreachable: `paintRow` only routes table members here. The
        // font draws everything else (box-drawing included).
        painter.setPen(fg);
        painter.drawText(rect, Qt::AlignCenter, QString::fromUcs4(&cp, 1));
        return;
    }
    for (const Primitive& prim : it->second) {
        switch (prim.kind) {
        case Primitive::kFill:
            painter.fillRect(QRectF(x + prim.a * w, y + prim.b * h, prim.c * w, prim.d * h),
                             prim.role == 0 ? fg : bg);
            break;
        case Primitive::kLine:
            painter.setPen(fg);
            painter.drawLine(QPointF(x + prim.a * w, y + prim.b * h),
                             QPointF(x + prim.c * w, y + prim.d * h));
            break;
        case Primitive::kSquare:
        case Primitive::kCircle: {
            const double side = prim.a * u;
            const QRectF qrect(x + w / 2 + prim.b * u - side / 2,
                               y + h / 2 + prim.c * u - side / 2, side, side);
            if (prim.role == 0) { // fill
                if (prim.kind == Primitive::kSquare) {
                    painter.fillRect(qrect, fg);
                } else {
                    painter.setPen(Qt::NoPen);
                    painter.setBrush(fg);
                    painter.drawEllipse(qrect);
                }
            } else { // ring: an outline, the cell background shows through
                painter.setPen(fg);
                painter.setBrush(Qt::NoBrush);
                if (prim.kind == Primitive::kSquare) {
                    painter.drawRect(qrect);
                } else {
                    painter.drawEllipse(qrect);
                }
            }
            break;
        }
        case Primitive::kPoly: {
            const double side = prim.a * u;
            const double cx = x + w / 2 + prim.b * u;
            const double cy = y + h / 2 + prim.c * u;
            QPolygonF pts;
            const float* v = &prim.d;
            for (int i = 0; i + 1 < 7; i += 2) {
                pts.append(QPointF(cx + (v[i] - 0.5f) * side, cy + (v[i + 1] - 0.5f) * side));
            }
            if (prim.role == 0) { // fill
                painter.setPen(Qt::NoPen);
                painter.setBrush(fg);
            } else { // ring
                painter.setPen(fg);
                painter.setBrush(Qt::NoBrush);
            }
            painter.drawPolygon(pts);
            break;
        }
        }
    }
}

void TerminalRenderer::paintCursor(QPainter& painter, const Snapshot& snapshot,
                                   int viewportLines, const std::vector<Row>* rows,
                                   const std::optional<std::pair<int, int>>& selRange,
                                   const char* cursorStyle)
{
    // The cursor at its viewport position (grid row plus the scroll
    // offset — the viewport shows history rows above the grid);
    // off-viewport cursors draw nothing. kCursorBlock (the focused
    // cursor): a character under it renders inverted — the block is the
    // cell's foreground, the glyph its background — so the cursor never
    // hides the text it sits on (xterm); an empty cell keeps the solid
    // block. When the app set an OSC 12 cursor color, the block is that
    // color instead and the glyph contrasts with it by luminance.
    // kCursorOutline (the unfocused cursor): a hollow rectangle around
    // the cell — the character underneath stays visible.
    const int y = snapshot.cursorRow;
    const int x = snapshot.cursorCol;
    const int viewportRow = y + snapshot.viewportOffset;
    if (viewportRow < 0 || viewportRow >= viewportLines) {
        return;
    }
    QRectF rect = cellRect(viewportRow, x);
    const Cell* cell = nullptr;
    if (rows != nullptr && viewportRow < static_cast<int>(rows->size()) &&
        x < static_cast<int>((*rows)[viewportRow].cells.size())) {
        cell = &(*rows)[viewportRow].cells[x];
    } else if (rows == nullptr && snapshot.full &&
               viewportRow < static_cast<int>(snapshot.rows.size()) &&
               x < static_cast<int>(snapshot.rows[viewportRow].cells.size())) {
        cell = &snapshot.rows[viewportRow].cells[x];
    }
    const bool wideContinuation =
        cell != nullptr && x + 1 < static_cast<int>(rows != nullptr
                                                        ? (*rows)[viewportRow].cells.size()
                                                        : snapshot.rows[viewportRow].cells.size()) &&
        (rows != nullptr ? (*rows)[viewportRow].cells[x + 1].data.empty()
                         : snapshot.rows[viewportRow].cells[x + 1].data.empty());
    if (std::string(cursorStyle) == kCursorOutline) {
        // The unfocused cursor: a hollow rectangle around the cell — the
        // character (or empty background) underneath stays visible. A
        // wide char's continuation widens the rect. The rect is inset by
        // half a line width: drawRect centers a 1px pen on the rect
        // boundary, so an uninset rect bleeds ~0.5px into the adjacent
        // rows — which the row-only repaint never clears.
        if (wideContinuation) {
            rect.setWidth(2 * m_cellW);
        }
        rect = rect.marginsRemoved(QMarginsF(0.5, 0.5, 0.5, 0.5));
        painter.setPen(m_defaultFg);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(rect);
        return;
    }
    if (cell == nullptr || cell->hidden || cell->data.empty()) {
        // Empty (or unavailable) cell: the solid block — the OSC 12
        // cursor color when set, else the default foreground.
        painter.fillRect(rect, snapshot.cursorColor.has_value()
                                   ? QColor(QString::fromStdString(*snapshot.cursorColor))
                                   : m_defaultFg);
        return;
    }
    QColor fg, bg;
    if (snapshot.cursorColor.has_value()) {
        // OSC 12 — the app's cursor color: the block is that color, the
        // glyph contrasts with it by luminance.
        fg = QColor(QString::fromStdString(*snapshot.cursorColor));
        bg = fg.lightnessF() < 0.5 ? QColor(0xFF, 0xFF, 0xFF) : QColor(0, 0, 0);
    } else {
        // The cell's rendered colors — derived exactly as paintRow does
        // (reverse-video XOR, selection, dim) — the block takes the
        // foreground, the glyph the background.
        fg = color(cell->fg, m_defaultFg, cell->bold);
        bg = color(cell->bg, m_defaultBg);
        if (cell->reverse != snapshot.reverseVideo) {
            fg = color(cell->bg, m_defaultBg, cell->bold);
            bg = color(cell->fg, m_defaultFg);
        }
        if (selRange.has_value() && selRange->first <= x && x <= selRange->second) {
            std::swap(fg, bg);
        }
        if (cell->dim) {
            fg = QColor((fg.red() + bg.red()) / 2, (fg.green() + bg.green()) / 2,
                        (fg.blue() + bg.blue()) / 2);
        }
    }
    if (wideContinuation) {
        rect.setWidth(2 * m_cellW); // a wide char spans two cells
    }
    painter.fillRect(rect, fg);
    painter.setFont(fontFor(cell->bold, cell->italic));
    painter.setPen(bg);
    const auto [st, off] = staticText(encodeUtf8(cell->data), cell->bold, cell->italic);
    painter.drawStaticText(QPointF(rect.left(), rect.top() + off), st);
    if (cell->underline) {
        painter.fillRect(QRectF(rect.left(), rect.bottom() - 1, rect.width(), 1), bg);
    }
    if (cell->strike) {
        painter.fillRect(QRectF(rect.left(), rect.top() + static_cast<int>(rect.height()) / 2, rect.width(), 1), bg);
    }
    if (cell->overline) {
        painter.fillRect(QRectF(rect.left(), rect.top(), rect.width(), 1), bg);
    }
}

} // namespace qtermx::gui