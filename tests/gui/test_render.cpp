// T18 — snapshot → pixels: the renderer paints frozen snapshot rows
// into a QImage backing store (port of tests/gui/test_render.py). Pure
// Qt (offscreen): assertions read pixel colors at cell positions — the
// widget blits the same image untouched.
//
// Cell color contract (screen.h): -1 = default, 0–255 palette (0–15
// xterm ANSI, 16–231 cube, 232–255 grayscale), >= 0x1000000 = RGB.
#include <optional>
#include <string>
#include <vector>

#include <QColor>
#include <QFont>
#include <QImage>
#include <QPainter>
#include <QtTest>

#include "renderer.h"
#include "screen.h"
#include "selection.h"
#include "session.h"

using namespace qtermx;
using namespace qtermx::gui;

namespace {

Row makeRow(std::initializer_list<Cell> cells)
{
    Row row;
    row.cells.assign(cells);
    return row;
}

// The Python test's `snapshot()` helper: an incremental snapshot with
// every row dirty.
Snapshot makeSnapshot(const std::vector<Row>& rows, int cursorRow = -1, int cursorCol = 0,
                      int viewportOffset = 0, bool cursorVisible = true,
                      std::optional<std::string> cursorColor = std::nullopt)
{
    Snapshot snap;
    for (size_t i = 0; i < rows.size(); ++i) {
        snap.dirtyRows.push_back(static_cast<int>(i));
    }
    snap.rows = rows;
    snap.cursorRow = cursorRow;
    snap.cursorCol = cursorCol;
    snap.viewportOffset = viewportOffset;
    snap.cursorVisible = cursorVisible;
    snap.cursorColor = std::move(cursorColor);
    return snap;
}

// The pixel at a cell's center (glyphs rarely reach there).
QColor cellPixel(const QImage& image, const TerminalRenderer& r, int col, int row = 0)
{
    return image.pixelColor(static_cast<int>(std::round(r.cellW() * col + r.cellW() / 2)),
                            static_cast<int>(r.cellH() * row + r.cellH() / 2));
}

bool cellHasColor(const QImage& image, const TerminalRenderer& r, int col, const QColor& color,
                  int row = 0)
{
    for (int y = static_cast<int>(r.cellH() * row); y < static_cast<int>(r.cellH() * (row + 1));
         ++y) {
        for (int x = static_cast<int>(std::round(r.cellW() * col));
             x < static_cast<int>(std::round(r.cellW() * (col + 1))); ++x) {
            if (image.pixelColor(x, y) == color) {
                return true;
            }
        }
    }
    return false;
}

// Any pixel within `tol` per channel of `color` — glyphs are
// font-antialiased (no pixel is exactly the pure color on some
// machines), so glyph assertions compare approximately; background
// fills (fillRect) stay exact via cellHasColor/cellPixel.
bool cellHasColorApprox(const QImage& image, const TerminalRenderer& r, int col,
                        const QColor& color, int row = 0, int tol = 150)
{
    for (int y = static_cast<int>(r.cellH() * row); y < static_cast<int>(r.cellH() * (row + 1));
         ++y) {
        for (int x = static_cast<int>(std::round(r.cellW() * col));
             x < static_cast<int>(std::round(r.cellW() * (col + 1))); ++x) {
            const QColor p = image.pixelColor(x, y);
            if (std::abs(p.red() - color.red()) <= tol && std::abs(p.green() - color.green()) <= tol &&
                std::abs(p.blue() - color.blue()) <= tol) {
                return true;
            }
        }
    }
    return false;
}

QImage makeImage(const TerminalRenderer& r, int cols, int rows)
{
    QImage img(static_cast<int>(std::round(r.cellW() * cols)),
               static_cast<int>(r.cellH() * rows), QImage::Format_RGB32);
    img.fill(Qt::black);
    return img;
}

} // namespace

class TestRender : public QObject {
    Q_OBJECT

private slots:
    // -- Basic colors ----------------------------------------------------

    void blankCellIsDefaultBackground()
    {
        TerminalRenderer r(QFont("Menlo", 12));
        QImage img = makeImage(r, 2, 1);
        r.render(img, makeSnapshot({makeRow({Cell::blank(), Cell::blank()})}));
        QCOMPARE(cellPixel(img, r, 0), defaultBg());
    }

    void textCellPaintsDefaultForeground()
    {
        TerminalRenderer r(QFont("Menlo", 12));
        QImage img = makeImage(r, 2, 1);
        r.render(img, makeSnapshot({makeRow({Cell{U"A"}, Cell::blank()})}));
        QVERIFY(cellHasColor(img, r, 0, defaultFg()));
    }

    void paletteForeground()
    {
        TerminalRenderer r(QFont("Menlo", 12));
        QImage img = makeImage(r, 2, 1);
        Cell c{U"A"};
        c.fg = 1;
        r.render(img, makeSnapshot({makeRow({c, Cell::blank()})}));
        QVERIFY(cellHasColor(img, r, 0, QColor(0xCD, 0x00, 0x00)));
    }

    void paletteBackground()
    {
        TerminalRenderer r(QFont("Menlo", 12));
        QImage img = makeImage(r, 2, 1);
        Cell c{U" "};
        c.bg = 2;
        r.render(img, makeSnapshot({makeRow({c, Cell::blank()})}));
        QCOMPARE(cellPixel(img, r, 0), QColor(0x00, 0xCD, 0x00));
    }

    void rgbColor()
    {
        TerminalRenderer r(QFont("Menlo", 12));
        QImage img = makeImage(r, 2, 1);
        Cell c{U"A"};
        c.fg = rgb(1, 2, 3);
        r.render(img, makeSnapshot({makeRow({c, Cell::blank()})}));
        QVERIFY(cellHasColor(img, r, 0, QColor(1, 2, 3)));
    }

    void boldStepsAnsiToBright()
    {
        TerminalRenderer r(QFont("Menlo", 12));
        QImage img = makeImage(r, 2, 1);
        Cell c{U"A"};
        c.fg = 1;
        c.bold = true;
        r.render(img, makeSnapshot({makeRow({c, Cell::blank()})}));
        QVERIFY(cellHasColor(img, r, 0, QColor(0xFF, 0x00, 0x00)));
        QVERIFY(!cellHasColor(img, r, 0, QColor(0xCD, 0x00, 0x00)));
    }

    void cubeAndGrayscale()
    {
        TerminalRenderer r(QFont("Menlo", 12));
        Cell a{U"A"};
        a.fg = 22; // cube (0, 1, 0) → (0, 95, 0)
        Cell b{U"A"};
        b.fg = 232; // grayscale: 8
        QImage img = makeImage(r, 2, 2);
        r.render(img, makeSnapshot({makeRow({a, Cell::blank()}), makeRow({b, Cell::blank()})}));
        QVERIFY(cellHasColor(img, r, 0, QColor(0, 95, 0), 0));
        QVERIFY(cellHasColor(img, r, 0, QColor(8, 8, 8), 1));
    }

    void reverseSwapsColors()
    {
        TerminalRenderer r(QFont("Menlo", 12));
        QImage img = makeImage(r, 2, 1);
        Cell c{U" "};
        c.fg = 1;
        c.reverse = true;
        r.render(img, makeSnapshot({makeRow({c, Cell::blank()})}));
        QCOMPARE(cellPixel(img, r, 0), QColor(0xCD, 0x00, 0x00)); // fg became bg
    }

    void decsnmInvertsTheWholeScreen()
    {
        // DECSCNM (?5) inverts every cell: a plain cell shows the fg
        // color as its background, and a cell with SGR reverse shows a
        // normal bg (the two inversion layers XOR).
        TerminalRenderer r(QFont("Menlo", 12));
        QImage img = makeImage(r, 2, 1);
        Cell rev{U" "};
        rev.fg = 1;
        rev.reverse = true;
        Snapshot snap = makeSnapshot({makeRow({Cell::blank(), rev})});
        snap.reverseVideo = true;
        r.render(img, snap);
        QCOMPARE(cellPixel(img, r, 0), defaultFg()); // plain → inverted
        QCOMPARE(cellPixel(img, r, 1), QColor(0x10, 0x10, 0x10)); // reverse XOR ?5 → normal
    }

    void boldBrightAppliesAfterInversion()
    {
        // Spec §7: inversion layers first, then bold-as-bright — a bold
        // reverse cell shows the original *background* brightened as fg.
        TerminalRenderer r(QFont("Menlo", 12));
        QImage img = makeImage(r, 2, 1);
        Cell c{U"A"};
        c.bg = 1;
        c.bold = true;
        c.reverse = true;
        r.render(img, makeSnapshot({makeRow({c, Cell::blank()})}));
        // Displayed fg = original bg 1, brightened by bold → bright red.
        QVERIFY(cellHasColor(img, r, 0, QColor(0xFF, 0x00, 0x00)));
        QVERIFY(!cellHasColor(img, r, 0, QColor(0xCD, 0x00, 0x00)));
    }

    void wideCharDrawsOnceAcrossTwoCells()
    {
        // A wide char advances two cells; the empty continuation cell
        // gets its background but no glyph (screen.h's "" continuation).
        TerminalRenderer r(QFont("Menlo", 12));
        QImage img = makeImage(r, 2, 1);
        Cell lead{U"界"};
        lead.fg = 1;
        r.render(img, makeSnapshot({makeRow({lead, Cell{U""}})}));
        // The glyph spans into the continuation cell: fg pixels in both
        // (antialiased — compared approximately).
        QVERIFY(cellHasColorApprox(img, r, 0, QColor(0xCD, 0x00, 0x00)));
        QVERIFY(cellHasColorApprox(img, r, 1, QColor(0xCD, 0x00, 0x00)));
    }

    void hiddenCellPaintsNoGlyph()
    {
        TerminalRenderer r(QFont("Menlo", 12));
        QImage img = makeImage(r, 2, 1);
        Cell c{U"A"};
        c.hidden = true;
        r.render(img, makeSnapshot({makeRow({c, Cell::blank()})}));
        QVERIFY(!cellHasColor(img, r, 0, defaultFg()));
    }

    void incrementalSnapshotTouchesOnlyDirtyRows()
    {
        TerminalRenderer r(QFont("Menlo", 12));
        Row before = makeRow({Cell{U"X"}, Cell::blank(), Cell::blank()});
        QImage img = makeImage(r, 3, 2);
        r.render(img, makeSnapshot({before, makeRow({Cell::blank(), Cell::blank(), Cell::blank()})}));
        // Repaint only row 1: row 0 must still show "X".
        Snapshot dirty = makeSnapshot({makeRow({Cell{U"Y"}, Cell::blank(), Cell::blank()})});
        dirty.dirtyRows = {1};
        r.render(img, dirty);
        QVERIFY(cellHasColorApprox(img, r, 0, defaultFg(), 0)); // the "X" survives
        QVERIFY(cellHasColorApprox(img, r, 0, defaultFg(), 1)); // the "Y" landed
    }

    // -- Cursor ----------------------------------------------------------

    void cursorIsReverseBlock()
    {
        TerminalRenderer r(QFont("Menlo", 12));
        QImage img = makeImage(r, 1, 2);
        r.render(img, makeSnapshot({makeRow({Cell::blank()}), makeRow({Cell::blank()})}, 0, 0));
        QCOMPARE(cellPixel(img, r, 0), defaultFg()); // block over the default bg
    }

    void cursorInvertsCharacterUnderIt()
    {
        // A character under the cursor renders inverted — the block is
        // the cell's foreground, the glyph its background — so the
        // cursor never hides the text it sits on (xterm). `rows` is the
        // merged viewport the widget passes (the snapshot alone is
        // incremental).
        TerminalRenderer r(QFont("Menlo", 12));
        QImage img = makeImage(r, 1, 1);
        Cell c{U"X"};
        c.fg = rgb(255, 0, 0);
        c.bg = rgb(0, 0, 255);
        const std::vector<Row> rows = {makeRow({c})};
        r.render(img, makeSnapshot(rows, 0, 0), &rows);
        QVERIFY(cellHasColor(img, r, 0, QColor(255, 0, 0))); // the block
        QVERIFY(cellHasColor(img, r, 0, QColor(0, 0, 255))); // the glyph
    }

    void cursorInvertsDefaultCharacter()
    {
        // A default-rendition character: block = default fg, glyph =
        // default bg — the character stays visible on the block.
        TerminalRenderer r(QFont("Menlo", 12));
        QImage img = makeImage(r, 1, 1);
        const std::vector<Row> rows = {makeRow({Cell{U"M"}})};
        r.render(img, makeSnapshot(rows, 0, 0), &rows);
        QVERIFY(cellHasColor(img, r, 0, defaultFg())); // the block
        QVERIFY(cellHasColor(img, r, 0, defaultBg())); // the glyph
    }

    void cursorBlockUsesOsc12Color()
    {
        // OSC 12 (opencode, vim set the caret color per theme): the
        // block is the app's color, and the glyph contrasts with it by
        // luminance.
        TerminalRenderer r(QFont("Menlo", 12));
        QImage img = makeImage(r, 1, 1);
        const std::vector<Row> rows = {makeRow({Cell{U"M"}})};
        r.render(img, makeSnapshot(rows, 0, 0, 0, true, "#1a1a1a"), &rows);
        QVERIFY(cellHasColor(img, r, 0, QColor("#1a1a1a"))); // the block
        QVERIFY(cellHasColor(img, r, 0, QColor(0xFF, 0xFF, 0xFF))); // the glyph
    }

    void cursorBlockOsc12LightColorDrawsDarkGlyph()
    {
        // A light cursor color (a dark-theme app) flips the glyph
        // contrast.
        TerminalRenderer r(QFont("Menlo", 12));
        QImage img = makeImage(r, 1, 1);
        const std::vector<Row> rows = {makeRow({Cell{U"M"}})};
        r.render(img, makeSnapshot(rows, 0, 0, 0, true, "#ffffff"), &rows);
        QVERIFY(cellHasColor(img, r, 0, QColor(0, 0, 0))); // the glyph
        QVERIFY(cellHasColor(img, r, 0, QColor("#ffffff"))); // the block
    }

    void cursorOsc12ColorOnEmptyCell()
    {
        // The solid block on an empty cell takes the OSC 12 color too.
        TerminalRenderer r(QFont("Menlo", 12));
        QImage img = makeImage(r, 1, 2);
        r.render(img, makeSnapshot({makeRow({Cell::blank()}), makeRow({Cell::blank()})}, 0, 0, 0,
                                   true, "#1a1a1a"));
        QCOMPARE(cellPixel(img, r, 0), QColor("#1a1a1a"));
    }

    void cursorOutlineLeavesCharacterVisible()
    {
        // The unfocused cursor (kCursorOutline): a hollow rectangle
        // around the cell — the character underneath stays visible, no
        // block, no inversion.
        TerminalRenderer r(QFont("Menlo", 12));
        QImage img = makeImage(r, 1, 1);
        Cell c{U"X"};
        c.fg = rgb(255, 0, 0);
        c.bg = rgb(0, 0, 255);
        const std::vector<Row> rows = {makeRow({c})};
        r.render(img, makeSnapshot(rows, 0, 0), &rows, nullptr, nullptr, std::nullopt,
                 kCursorOutline);
        QVERIFY(cellHasColor(img, r, 0, QColor(255, 0, 0))); // the glyph
        QVERIFY(cellHasColor(img, r, 0, QColor(0, 0, 255))); // the background
        QVERIFY(cellHasColor(img, r, 0, defaultFg()));       // the outline
        QVERIFY(cellPixel(img, r, 0) != defaultFg());        // no block at the center
    }

    void cursorOutlineOnBlankCell()
    {
        // An empty cell keeps the character-free rectangle: the outline
        // is drawn, the center stays the background.
        TerminalRenderer r(QFont("Menlo", 12));
        QImage img = makeImage(r, 1, 2);
        r.render(img, makeSnapshot({makeRow({Cell::blank()}), makeRow({Cell::blank()})}, 0, 0),
                 nullptr, nullptr, nullptr, std::nullopt, kCursorOutline);
        QVERIFY(cellHasColor(img, r, 0, defaultFg())); // the outline
        QCOMPARE(cellPixel(img, r, 0), defaultBg());   // center not filled
    }

    void cursorBlockFillsTheCellCorners()
    {
        // The focused block cursor fills the whole cell — the outline's
        // half-pixel inset must not leak into the block path.
        TerminalRenderer r(QFont("Menlo", 12));
        QImage img = makeImage(r, 1, 2);
        r.render(img, makeSnapshot({makeRow({Cell::blank()}), makeRow({Cell::blank()})}, 0, 0));
        const int w = static_cast<int>(std::round(r.cellW()));
        const int h = static_cast<int>(r.cellH());
        QCOMPARE(img.pixelColor(0, 0), defaultFg());
        QCOMPARE(img.pixelColor(w - 1, 0), defaultFg());
        QCOMPARE(img.pixelColor(0, h - 1), defaultFg());
        QCOMPARE(img.pixelColor(w - 1, h - 1), defaultFg());
    }

    void cursorOutlineStaysInsideTheCell()
    {
        // The unfocused outline must not bleed into the row below — a
        // drawRect pen is centered on the rect boundary, so an uninset
        // rect leaves a ~0.5px line in the next row.
        TerminalRenderer r(QFont("Menlo", 12));
        QImage img = makeImage(r, 1, 2);
        r.render(img, makeSnapshot({makeRow({Cell::blank()}), makeRow({Cell::blank()})}, 0, 0),
                 nullptr, nullptr, nullptr, std::nullopt, kCursorOutline);
        for (int y = static_cast<int>(r.cellH()); y < static_cast<int>(2 * r.cellH()); ++y) {
            for (int x = 0; x < static_cast<int>(std::round(r.cellW())); ++x) {
                QCOMPARE(img.pixelColor(x, y), defaultBg());
            }
        }
    }

    void hiddenCursorDrawsNothing()
    {
        // DECTCEM ?25l (cursorVisible=false): the block must not paint.
        TerminalRenderer r(QFont("Menlo", 12));
        QImage img = makeImage(r, 1, 2);
        r.render(img, makeSnapshot({makeRow({Cell::blank()}), makeRow({Cell::blank()})}, 0, 0, 0,
                                   false));
        QCOMPARE(cellPixel(img, r, 0), defaultBg()); // no block over the default bg
    }

    void cursorOverrideHidesVisibleSnapshot()
    {
        // The widget's blink phase (cursorVisible=false) hides the block
        // even though the snapshot's DECTCEM says visible.
        TerminalRenderer r(QFont("Menlo", 12));
        QImage img = makeImage(r, 1, 2);
        r.render(img, makeSnapshot({makeRow({Cell::blank()}), makeRow({Cell::blank()})}, 0, 0),
                 nullptr, nullptr, nullptr, false);
        QCOMPARE(cellPixel(img, r, 0), defaultBg());
    }

    void cursorOverrideShowsVisibleSnapshot()
    {
        // The blink phase True keeps the block over a visible snapshot.
        TerminalRenderer r(QFont("Menlo", 12));
        QImage img = makeImage(r, 1, 2);
        r.render(img, makeSnapshot({makeRow({Cell::blank()}), makeRow({Cell::blank()})}, 0, 0),
                 nullptr, nullptr, nullptr, true);
        QCOMPARE(cellPixel(img, r, 0), defaultFg());
    }

    void cursorOverrideNeverShowsHiddenSnapshot()
    {
        // DECTCEM always wins: the override is ANDed with the snapshot's
        // visibility, so a cursor the app hid (?25l) stays hidden even
        // when the blink phase is True.
        TerminalRenderer r(QFont("Menlo", 12));
        QImage img = makeImage(r, 1, 2);
        r.render(img, makeSnapshot({makeRow({Cell::blank()}), makeRow({Cell::blank()})}, 0, 0, 0,
                                   false),
                 nullptr, nullptr, nullptr, true);
        QCOMPARE(cellPixel(img, r, 0), defaultBg());
    }

    void cursorPaintsAtGridRowPlusOffset()
    {
        // Scrolled 2 up: grid row 1 shows at viewport row 1 + 2 = 3.
        TerminalRenderer r(QFont("Menlo", 12));
        QImage img = makeImage(r, 1, 5);
        r.render(img, makeSnapshot({makeRow({Cell::blank()}), makeRow({Cell::blank()}),
                                    makeRow({Cell::blank()}), makeRow({Cell::blank()}),
                                    makeRow({Cell::blank()})},
                                   1, 0, 2));
        QCOMPARE(cellPixel(img, r, 0, 3), defaultFg()); // the cursor block
        QCOMPARE(cellPixel(img, r, 0, 1), defaultBg()); // grid row 1, unscrolled
    }

    void cursorOffViewportDrawsNothing()
    {
        // Grid row 4 + offset 2 = viewport row 6, beyond the 5-row
        // viewport.
        TerminalRenderer r(QFont("Menlo", 12));
        QImage img = makeImage(r, 1, 5);
        r.render(img, makeSnapshot({makeRow({Cell::blank()}), makeRow({Cell::blank()}),
                                    makeRow({Cell::blank()}), makeRow({Cell::blank()}),
                                    makeRow({Cell::blank()})},
                                   4, 0, 2));
        for (int row = 0; row < 5; ++row) {
            QCOMPARE(cellPixel(img, r, 0, row), defaultBg());
        }
    }

    void cursorGateUsesViewportRow()
    {
        // row_indices are viewport rows: grid row 0 + offset 1 =
        // viewport row 1.
        TerminalRenderer r(QFont("Menlo", 12));
        const QColor marker(Qt::red);
        const std::vector<Row> rows = {makeRow({Cell{U"M"}}), makeRow({Cell::blank()})};
        const Snapshot snap = makeSnapshot(rows, 0, 0, 1);
        QImage img = makeImage(r, 1, 2);
        img.fill(marker);
        {
            QPainter painter(&img);
            const std::vector<int> indices = {1};
            r.paint(painter, snap, 2, nullptr, &indices);
        }
        QCOMPARE(cellPixel(img, r, 0, 0), marker); // row 0 untouched
        QCOMPARE(cellPixel(img, r, 0, 1), defaultFg()); // cursor block at row 1
        // Without row 1 in row_indices the cursor must not paint.
        QImage img2 = makeImage(r, 1, 2);
        img2.fill(marker);
        {
            QPainter painter(&img2);
            const std::vector<int> indices = {0};
            r.paint(painter, snap, 2, nullptr, &indices);
        }
        QVERIFY(cellHasColor(img2, r, 0, defaultFg(), 0)); // row 0 M ink
        QCOMPARE(cellPixel(img2, r, 0, 1), marker);         // row 1 untouched
    }

    // -- font / anti-aliasing ---------------------------------------------

    void defaultFontKeepsSmoothing()
    {
        // Glyphs are font-smoothed; crispness comes from the grid
        // geometry, not jagged masks.
        TerminalRenderer r(QFont("Menlo", 12));
        QVERIFY(!(r.font().styleStrategy() & QFont::NoAntialias));
    }

    void antialiasOptOutDisablesSmoothing()
    {
        TerminalRenderer r(QFont(), false);
        QVERIFY(r.font().styleStrategy() & QFont::NoAntialias);
    }

    void callerFontIsNotMutated()
    {
        QFont font("Menlo", 12);
        const QFont::StyleStrategy original = font.styleStrategy();
        TerminalRenderer r(font); // copies the font, never mutates the caller's
        QCOMPARE(font.styleStrategy(), original);
    }

    void setFontReplacesFontAndMetrics()
    {
        TerminalRenderer r(QFont("Menlo", 12));
        const double oldW = r.cellW();
        const double oldH = r.cellH();
        r.setFont(QFont("Menlo", 24));
        QCOMPARE(r.font().family(), QString("Menlo"));
        QCOMPARE(r.font().pointSize(), 24);
        QVERIFY(r.cellW() >= oldW);
        QVERIFY(r.cellH() >= oldH);
    }

    void setPaletteReplacesDefaults()
    {
        TerminalRenderer r(QFont("Menlo", 12));
        QCOMPARE(r.defaultFgColor(), defaultFg());
        QCOMPARE(r.defaultBgColor(), defaultBg());
        const QColor fg(0x00, 0x00, 0x00);
        const QColor bg(0xff, 0xff, 0xff);
        r.setPalette(fg, bg);
        QCOMPARE(r.defaultFgColor(), fg);
        QCOMPARE(r.defaultBgColor(), bg);
    }

    void setPaletteRepaintsBlankCellWithNewBg()
    {
        TerminalRenderer r(QFont("Menlo", 12));
        QImage img = makeImage(r, 1, 1);
        r.setPalette(QColor(0x00, 0x00, 0x00), QColor(0x12, 0x34, 0x56));
        r.render(img, makeSnapshot({makeRow({Cell::blank()})}));
        QCOMPARE(cellPixel(img, r, 0), QColor(0x12, 0x34, 0x56));
    }

    // -- box-drawing glyphs render through the font ----------------------

    void boxDrawingGlyphsRenderThroughTheFont()
    {
        // Box-drawing lines and corners (U+2500–253C, U+256D–2570) are
        // deliberately NOT vector-drawn — the font's glyphs are designed
        // to join across cells (the vector table only covers block
        // characters and geometric shapes). They must not be classified
        // as vector codepoints, and each glyph must paint through the
        // normal text path.
        const std::vector<char32_t> glyphs = {
            0x2500, 0x2502, // ─ │
            0x250C, 0x2510, 0x2514, 0x2518, // ┌ ┐ └ ┘
            0x251C, 0x2524, 0x252C, 0x2534, 0x253C, // ├ ┤ ┬ ┴ ┼
            0x256D, 0x256E, 0x256F, 0x2570, // ╭ ╮ ╯ ╰
        };
        const auto& table = vectorGlyphs();
        for (const char32_t cp : glyphs) {
            QVERIFY2(table.find(cp) == table.end(), "box-drawing must stay in the font");
            TerminalRenderer r(QFont("Menlo", 12));
            QImage img = makeImage(r, 1, 1);
            Cell c{std::u32string(1, cp)};
            c.fg = 1;
            r.render(img, makeSnapshot({makeRow({c})}));
            QVERIFY2(cellHasColorApprox(img, r, 0, QColor(0xCD, 0x00, 0x00)),
                     "the font must paint the glyph");
        }
    }

    void blockHalfRowsJoinSeamlessly()
    {
        // ▀▀: two cells — the top halves must tile without a gap between
        // cells (the font version leaves seams at the boundaries).
        TerminalRenderer r(QFont("Menlo", 12));
        QImage img = makeImage(r, 2, 1);
        r.render(img, makeSnapshot({makeRow({Cell{U"\u2580"}, Cell{U"\u2580"}})}));
        for (int x = 0; x < static_cast<int>(std::round(2 * r.cellW())); ++x) {
            QCOMPARE(img.pixelColor(x, 0), defaultFg());
        }
        // The seam between the two cells is seamless.
        QCOMPARE(img.pixelColor(static_cast<int>(std::round(r.cellW())) - 1, 0), defaultFg());
        QCOMPARE(img.pixelColor(static_cast<int>(std::round(r.cellW())), 0), defaultFg());
        // The bottom of the cell is untouched.
        QCOMPARE(img.pixelColor(0, static_cast<int>(r.cellH()) - 1), defaultBg());
    }

    void blockQuadrantCharFillsOnlyItsQuadrant()
    {
        // ▘ (U+2598): only the top-left quadrant is lit.
        TerminalRenderer r(QFont("Menlo", 12));
        QImage img = makeImage(r, 1, 1);
        r.render(img, makeSnapshot({makeRow({Cell{U"\u2598"}})}));
        QCOMPARE(img.pixelColor(0, 0), defaultFg());
        QCOMPARE(img.pixelColor(static_cast<int>(std::round(r.cellW())) - 1, 0), defaultBg());
        QCOMPARE(img.pixelColor(0, static_cast<int>(r.cellH()) - 1), defaultBg());
        QCOMPARE(img.pixelColor(static_cast<int>(std::round(r.cellW())) - 1,
                                static_cast<int>(r.cellH()) - 1),
                 defaultBg());
    }

    void shapeGlyphsDrawCenteredVectorFills()
    {
        // The geometric-shape family (⬝ ▪ ■ ● • · ◆ ▲ ▶ ◼ ⬤ ⬛): filled
        // vector shapes — centered in the cell, small enough that the
        // corners stay empty, and visible. A larger font gives the
        // 0.90-size ⬛ enough margin (at Menlo 12 the C++ metrics make
        // it touch the left edge — the Python's metrics differ).
        const std::vector<char32_t> glyphs = {0x2B1D, 0x25AA, 0x25A0, 0x25CF, 0x2022, 0x00B7,
                                              0x25C6, 0x25B2, 0x25B6, 0x25FC, 0x2B24, 0x2B1B};
        for (const char32_t glyph : glyphs) {
            TerminalRenderer r(QFont("Menlo", 16));
            QImage img = makeImage(r, 1, 1);
            Cell c{std::u32string(1, glyph)};
            c.fg = 1;
            r.render(img, makeSnapshot({makeRow({c})}));
            const QColor fg(0xCD, 0x00, 0x00);
            std::vector<std::pair<int, int>> painted;
            for (int y = 0; y < static_cast<int>(r.cellH()); ++y) {
                for (int x = 0; x < static_cast<int>(std::round(r.cellW())); ++x) {
                    if (img.pixelColor(x, y) == fg) {
                        painted.emplace_back(x, y);
                    }
                }
            }
            QVERIFY2(!painted.empty(), "the shape must paint");
            int minX = painted[0].first, maxX = painted[0].first;
            int minY = painted[0].second, maxY = painted[0].second;
            bool midY = false;
            for (const auto& [x, y] : painted) {
                minX = std::min(minX, x);
                maxX = std::max(maxX, x);
                minY = std::min(minY, y);
                maxY = std::max(maxY, y);
                if (y == static_cast<int>(r.cellH()) / 2) {
                    midY = true;
                }
            }
            // Centered and inside the cell: corners stay empty.
            const std::string glyphName = encodeUtf8(std::u32string(1, glyph));
            QVERIFY2(minX > 0 && maxX < static_cast<int>(std::round(r.cellW())) - 1,
                     ("touches an edge: " + glyphName + " cellW=" + std::to_string(r.cellW()) +
                      " maxX=" + std::to_string(maxX) + " font=" + r.font().family().toStdString())
                         .c_str());
            QVERIFY2(minY > 0 && maxY < static_cast<int>(r.cellH()) - 1,
                     ("touches an edge: " + glyphName).c_str());
            // The vertical middle of the cell is painted (centered).
            QVERIFY2(midY, "not vertically centered");
        }
    }

    void ringShapesDrawOutlinesNotFills()
    {
        // Hollow shapes are outlines: the rim paints, the interior shows
        // the cell background through.
        const std::vector<char32_t> glyphs = {0x25A1, 0x25CB, 0x25C7, 0x25B3}; // □ ○ ◇ △
        for (const char32_t glyph : glyphs) {
            TerminalRenderer r(QFont("Menlo", 12));
            QImage img = makeImage(r, 1, 1);
            Cell c{std::u32string(1, glyph)};
            c.fg = 1;
            r.render(img, makeSnapshot({makeRow({c})}));
            const QColor fg(0xCD, 0x00, 0x00);
            bool painted = false;
            for (int y = 0; y < static_cast<int>(r.cellH()); ++y) {
                for (int x = 0; x < static_cast<int>(std::round(r.cellW())); ++x) {
                    if (img.pixelColor(x, y) == fg) {
                        painted = true;
                    }
                }
            }
            QVERIFY2(painted, "the outline must paint");
            // The outline is a rim: the cell center stays unpainted for
            // a ring square and a ring circle.
            if (glyph == 0x25A1 || glyph == 0x25CB) {
                QVERIFY(img.pixelColor(static_cast<int>(r.cellW()) / 2,
                                       static_cast<int>(r.cellH()) / 2) != fg);
            }
        }
    }

    void unlistedBoxVariantsFallBackToTheFont()
    {
        // Heavy/double/dashed box variants (┃ ║ ═ ╧) are not in the
        // vector table — the font draws them like any other text (a
        // real-world crash: opencode renders ┃).
        const std::vector<char32_t> glyphs = {0x2503, 0x2551, 0x2550, 0x2567}; // ┃ ║ ═ ╧
        for (const char32_t glyph : glyphs) {
            TerminalRenderer r(QFont("Menlo", 12));
            QImage img = makeImage(r, 1, 1);
            Cell c{std::u32string(1, glyph)};
            c.fg = 1;
            r.render(img, makeSnapshot({makeRow({c})}));
            QVERIFY2(cellHasColorApprox(img, r, 0, QColor(0xCD, 0x00, 0x00)),
                     "the font fallback must paint");
        }
    }

    void brailleStaysInTheFont()
    {
        // Braille (U+2800–28FF) is intentionally font-rendered: the font
        // glyphs carry the correct dot patterns. It must NOT be
        // classified as a vector glyph, and the glyph must paint through
        // the normal text path.
        TerminalRenderer r(QFont("Menlo", 12));
        QImage img = makeImage(r, 1, 1);
        Cell c{U"\u280b"}; // ⠋
        c.fg = 1;
        r.render(img, makeSnapshot({makeRow({c})}));
        QVERIFY2(cellHasColorApprox(img, r, 0, QColor(0xCD, 0x00, 0x00)),
                 "the font must paint the braille glyph");
    }

    // -- dim / strike / overline / italic ---------------------------------

    void dimMixesForegroundTowardBackground()
    {
        TerminalRenderer r(QFont("Menlo", 12));
        QImage img = makeImage(r, 1, 1);
        Cell c{U"M"};
        c.dim = true;
        r.render(img, makeSnapshot({makeRow({c})}));
        // SGR 2: fg = (DEFAULT_FG + DEFAULT_BG) / 2 per channel.
        const QColor mixed(0x7C, 0x7C, 0x7C);
        QVERIFY(cellHasColor(img, r, 0, mixed));
    }

    void strikeAndOverlineDrawLines()
    {
        TerminalRenderer r(QFont("Menlo", 12));
        QImage img = makeImage(r, 1, 1);
        Cell c{U"M"};
        c.strike = true;
        c.overline = true;
        r.render(img, makeSnapshot({makeRow({c})}));
        QCOMPARE(img.pixelColor(0, static_cast<int>(r.cellH()) / 2), defaultFg()); // strike
        QCOMPARE(img.pixelColor(0, 0), defaultFg()); // overline
    }

    // -- partial rendering: row_indices bounds the repaint -----------------

    void paintRowIndicesLimitTheRepaint()
    {
        // Two rows of M; row_indices=[1] must paint only row 1 — row 0
        // stays untouched (the pre-filled red).
        TerminalRenderer r(QFont("Menlo", 12));
        const QColor marker(Qt::red);
        const std::vector<Row> rows = {makeRow({Cell{U"M"}}), makeRow({Cell{U"M"}})};
        QImage img = makeImage(r, 1, 2);
        img.fill(marker);
        {
            QPainter painter(&img);
            const std::vector<int> indices = {1};
            r.paint(painter, makeSnapshot(rows), 2, nullptr, &indices);
        }
        QCOMPARE(cellPixel(img, r, 0, 0), marker); // row 0 untouched
        QVERIFY(cellHasColor(img, r, 0, defaultFg(), 1)); // row 1 painted
    }

    void paintRowIndicesGateTheCursor()
    {
        // The cursor paints only when its viewport row is in
        // row_indices — a repaint of row 0 must not draw the row-1
        // cursor.
        TerminalRenderer r(QFont("Menlo", 12));
        const QColor marker(Qt::red);
        const std::vector<Row> rows = {makeRow({Cell{U"M"}}), makeRow({Cell{U"M"}})};
        QImage img = makeImage(r, 1, 2);
        img.fill(marker);
        const Snapshot snap = makeSnapshot(rows, 1, 0);
        {
            QPainter painter(&img);
            const std::vector<int> indices = {0};
            r.paint(painter, snap, 2, nullptr, &indices);
        }
        // Row 0: painted — glyph ink present (its row is in row_indices).
        QVERIFY(cellHasColor(img, r, 0, defaultFg()));
        // Row 1: untouched — the cursor row was not in row_indices.
        QCOMPARE(cellPixel(img, r, 0, 1), marker);
    }

    void renderRowIndicesLimitTheWidgetSeam()
    {
        // The widget's seam: `render` must forward `row_indices`, so a
        // one-row snapshot re-rasterizes one row into the backing image,
        // not the whole frame.
        TerminalRenderer r(QFont("Menlo", 12));
        const QColor marker(Qt::red);
        const std::vector<Row> rows = {makeRow({Cell{U"M"}}), makeRow({Cell{U"M"}})};
        QImage img = makeImage(r, 1, 2);
        img.fill(marker);
        const std::vector<int> indices = {1};
        r.render(img, makeSnapshot(rows), &rows, &indices);
        QCOMPARE(cellPixel(img, r, 0, 0), marker); // row 0 untouched
        QVERIFY(cellHasColor(img, r, 0, defaultFg(), 1)); // row 1 painted
    }

    // -- Selection overlay -------------------------------------------------

    void selectionPaintsReversedBackground()
    {
        // Selected cells swap fg/bg (the classic terminal highlight): a
        // blank red-fg cell paints its background red while selected.
        TerminalRenderer r(QFont("Menlo", 12));
        QImage img = makeImage(r, 2, 1);
        Cell c{U" "};
        c.fg = rgb(255, 0, 0);
        c.bg = rgb(0, 0, 255);
        const std::vector<Row> rows = {makeRow({c, c})};
        const Selection sel{0, 0, 0, 0};
        r.render(img, makeSnapshot(rows), nullptr, nullptr, &sel);
        QCOMPARE(cellPixel(img, r, 0), QColor(255, 0, 0));
        QCOMPARE(cellPixel(img, r, 1), QColor(0, 0, 255));
    }

    void selectionSwapsGlyphColor()
    {
        // The glyph of a selected cell is painted in the cell's
        // background color (the swap applies to both passes).
        TerminalRenderer r(QFont("Menlo", 12));
        QImage img = makeImage(r, 2, 1);
        Cell c{U"x"};
        c.fg = rgb(255, 0, 0);
        c.bg = rgb(0, 0, 255);
        const std::vector<Row> rows = {makeRow({c, Cell::blank()})};
        const Selection sel{0, 0, 0, 0};
        r.render(img, makeSnapshot(rows), nullptr, nullptr, &sel);
        // the glyph is painted in the (swapped) background color —
        // antialiased, so compared approximately
        QVERIFY(cellHasColorApprox(img, r, 0, QColor(0, 0, 255)));
        // ...and the selected background is the foreground color
        bool found = false;
        for (int y = 0; y < static_cast<int>(r.cellH()); ++y) {
            for (int x = 0; x < static_cast<int>(std::round(r.cellW())); ++x) {
                if (img.pixelColor(x, y) == QColor(255, 0, 0)) {
                    found = true;
                }
            }
        }
        QVERIFY(found);
    }

    void selectionRangeBoundsPainting()
    {
        // cols 1..2 of row 0 selected: the neighbors keep their own bg.
        TerminalRenderer r(QFont("Menlo", 12));
        QImage img = makeImage(r, 3, 1);
        Cell c{U" "};
        c.fg = rgb(255, 0, 0);
        c.bg = rgb(0, 0, 255);
        const std::vector<Row> rows = {makeRow({c, c, c})};
        const Selection sel{0, 1, 0, 2};
        r.render(img, makeSnapshot(rows), nullptr, nullptr, &sel);
        QCOMPARE(cellPixel(img, r, 0), QColor(0, 0, 255));
        QCOMPARE(cellPixel(img, r, 1), QColor(255, 0, 0));
        QCOMPARE(cellPixel(img, r, 2), QColor(255, 0, 0));
    }

    void selectionMultiRowOpenEnds()
    {
        // Rows 0-1 selected: row 0 from col 1 to the end, row 1 fully.
        TerminalRenderer r(QFont("Menlo", 12));
        QImage img = makeImage(r, 3, 2);
        Cell c{U" "};
        c.fg = rgb(255, 0, 0);
        c.bg = rgb(0, 0, 255);
        const std::vector<Row> rows = {makeRow({c, c, c}), makeRow({c, c, c})};
        const Selection sel{0, 1, 1, 1};
        r.render(img, makeSnapshot(rows), nullptr, nullptr, &sel);
        QCOMPARE(cellPixel(img, r, 0, 0), QColor(0, 0, 255)); // row 0, col 0: outside
        QCOMPARE(cellPixel(img, r, 2, 0), QColor(255, 0, 0)); // row 0, col 2: open end
        QCOMPARE(cellPixel(img, r, 0, 1), QColor(255, 0, 0)); // row 1 fully selected
    }

    void selectionRectangularSlices()
    {
        // Alt-drag rectangle: col 0 across rows 0-1 — col 1 is never
        // selected on either row.
        TerminalRenderer r(QFont("Menlo", 12));
        QImage img = makeImage(r, 2, 2);
        Cell c{U" "};
        c.fg = rgb(255, 0, 0);
        c.bg = rgb(0, 0, 255);
        const std::vector<Row> rows = {makeRow({c, c}), makeRow({c, c})};
        const Selection sel{0, 0, 1, 0, true};
        r.render(img, makeSnapshot(rows), nullptr, nullptr, &sel);
        QCOMPARE(cellPixel(img, r, 0, 0), QColor(255, 0, 0));
        QCOMPARE(cellPixel(img, r, 1, 0), QColor(0, 0, 255));
        QCOMPARE(cellPixel(img, r, 0, 1), QColor(255, 0, 0));
        QCOMPARE(cellPixel(img, r, 1, 1), QColor(0, 0, 255));
    }

    void selectionDoesNotShiftGlyphs()
    {
        // A selection splits a glyph run at its boundary; the unselected
        // cells must re-render pixel-identically to the selectionless
        // frame. Float cell_w == the layout advance, so the split runs
        // land exactly where the continuous run did.
        const int n = 20;
        TerminalRenderer r(QFont("Menlo", 12));
        Row row;
        for (int i = 0; i < n; ++i) {
            row.cells.push_back(Cell{U"M"});
        }
        const std::vector<Row> rows = {row};
        QImage imgPlain = makeImage(r, n, 1);
        QImage imgSel = makeImage(r, n, 1);
        r.render(imgPlain, makeSnapshot(rows));
        const Selection sel{0, 5, 0, 14};
        r.render(imgSel, makeSnapshot(rows), nullptr, nullptr, &sel);
        for (int col = 0; col < n; ++col) {
            if (col >= 5 && col <= 14) {
                continue;
            }
            for (int y = 0; y < static_cast<int>(r.cellH()); ++y) {
                for (int x = static_cast<int>(std::round(r.cellW() * col));
                     x < static_cast<int>(std::round(r.cellW() * (col + 1))); ++x) {
                    QVERIFY2(imgSel.pixelColor(x, y) == imgPlain.pixelColor(x, y),
                             "cell shifted by the selection");
                }
            }
        }
    }

    // -- static text cache -------------------------------------------------

    void staticTextCachesByKey()
    {
        // The QStaticText layout cache: same (text, bold, italic)
        // returns the same prepared layout; any key change is a distinct
        // layout. (QStaticText is implicitly shared — identity is
        // checked via the cache size, the Python's `is` contract.)
        TerminalRenderer r(QFont("Menlo", 12));
        r.staticText("hello", false, false);
        r.staticText("hello", false, false);
        QCOMPARE(r.staticCacheSize(), size_t(1)); // same key hits
        r.staticText("world", false, false);
        QCOMPARE(r.staticCacheSize(), size_t(2)); // different text
        r.staticText("hello", true, false);
        QCOMPARE(r.staticCacheSize(), size_t(3)); // different bold
        r.staticText("hello", false, true);
        QCOMPARE(r.staticCacheSize(), size_t(4)); // different italic
    }

    void staticTextCacheIsBounded()
    {
        // A stream of unique texts cannot grow the cache without bound
        // (the clear-on-overflow contract, like the color cache).
        TerminalRenderer r(QFont("Menlo", 12));
        for (int i = 0; i < kStaticCacheCap + 16; ++i) {
            r.staticText("t" + std::to_string(i), false, false);
        }
        QVERIFY(r.staticCacheSize() <= static_cast<size_t>(kStaticCacheCap));
    }

    void staticTextCacheClearsOnFontChange()
    {
        // Prepared layouts are font-specific — a font change must
        // rebuild them (stale layouts would paint at the old metrics).
        TerminalRenderer r(QFont("Menlo", 12));
        r.staticText("hello", false, false);
        r.setFont(r.font()); // same font — the cache still clears
        QCOMPARE(r.staticCacheSize(), size_t(0));
    }
};

QTEST_MAIN(TestRender)
#include "test_render.moc"