// T12 — `?5` (DECSCNM, reverse video) via the `effective_rendition`
// seam — the one new seam of Phase 3 (port of
// tests/screen/test_effective_rendition.py).
//
// `effective_rendition(x, y)` returns the (fg, bg) a renderer should
// draw for the cell at (x, y): the SGR `reverse` attribute and the
// DECSCNM mode XOR — both on cancels out, either alone swaps fg/bg.
// `render()` itself stays text-only; the mode lives in the generic DEC
// registry (`?5h`/`?5l` were already wired by the registry in Phase 2).
#include "harness.h"
#include "test_pipeline.h"

using namespace qtermx;
using namespace qtermx::test;

TEST_CASE(effective_rendition_is_cell_colors_without_mode)
{
    // With DECSCNM off, the effective rendition is the cell's own.
    Screen& screen = feedTo(U"\x1B[31;44mHI");
    int fg, bg;
    screen.effectiveRendition(0, 0, fg, bg);
    QTERMX_CHECK(fg == 1 && bg == 4);
    screen.effectiveRendition(1, 0, fg, bg);
    QTERMX_CHECK(fg == 1 && bg == 4);
    screen.effectiveRendition(10, 10, fg, bg);
    QTERMX_CHECK(fg == -1 && bg == -1); // blank
}

TEST_CASE(decsnm_swaps_fg_bg_at_render_time)
{
    // `?5h` swaps the effective colors; the stored cell is unchanged.
    Screen& screen = feedTo(U"\x1B[31;44m\x1B[?5hHI");
    int fg, bg;
    screen.effectiveRendition(0, 0, fg, bg);
    QTERMX_CHECK(fg == 4 && bg == 1);
    const Cell& cell = screen.line(0).cells[0];
    QTERMX_CHECK(cell.data == U"H");
    QTERMX_CHECK(cell.fg == 1 && cell.bg == 4); // stored raw
}

TEST_CASE(decsnm_off_restores_rendition)
{
    // `?5l` turns reverse video back off.
    Screen& screen = feedTo(U"\x1B[31;44m\x1B[?5h\x1B[?5lHI");
    int fg, bg;
    screen.effectiveRendition(0, 0, fg, bg);
    QTERMX_CHECK(fg == 1 && bg == 4);
}

TEST_CASE(sgr_reverse_stacks_with_decsnm_by_xor)
{
    // SGR 7 and DECSCNM both reverse; together they cancel (XOR).
    int fg, bg;
    Screen& a = feedTo(U"\x1B[31;44m\x1B[7mHI"); // SGR reverse alone
    a.effectiveRendition(0, 0, fg, bg);
    QTERMX_CHECK(fg == 4 && bg == 1);
    Screen& b = feedTo(U"\x1B[31;44m\x1B[7m\x1B[?5hHI"); // both on
    b.effectiveRendition(0, 0, fg, bg);
    QTERMX_CHECK(fg == 1 && bg == 4);
    Screen& c = feedTo(U"\x1B[31;44m\x1B[?5h\x1B[27mHI"); // DECSCNM only
    c.effectiveRendition(0, 0, fg, bg);
    QTERMX_CHECK(fg == 4 && bg == 1);
}

TEST_CASE(effective_rendition_reads_the_active_grid)
{
    // In the alternate screen, the alt grid's cells are the ones
    // consulted (ADR-0004); the shared mode still applies.
    auto p = makePipeline(4, 5);
    p->feed(U"\x1B[31mabc\x1B[?1047h\x1B[?5h");
    int fg, bg;
    p->screen.effectiveRendition(0, 0, fg, bg);
    // (0,0) holds main's red "a" — but the alt grid is blank, so the
    // effective rendition reads the alt cell: default colors. Reading
    // the main grid would give the swapped (-1, 1).
    QTERMX_CHECK(fg == -1 && bg == -1);
    QTERMX_CHECK(p->screen.mode(5, true)); // the shared mode is on in the alt
}

TEST_CASE(render_stays_text_only)
{
    // render() output is identical with DECSCNM on or off.
    const std::string off = feedTo(U"\x1B[31;44mHI").render();
    const std::string on = feedTo(U"\x1B[31;44m\x1B[?5hHI").render();
    QTERMX_CHECK(on == off);
}
