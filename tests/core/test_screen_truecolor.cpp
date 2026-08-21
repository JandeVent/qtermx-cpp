// T13 — truecolor (SGR 38;2 / 48;2) — ADR-0004 (port of
// tests/screen/test_truecolor.py). Colors are ints: `-1` default,
// `0–255` the 256-color palette, and `>= 0x1000000` an RGB value
// `(r << 16) | (g << 8) | b | 0x1000000`. A truncated `38;2` / `48;2`
// (missing components) is ignored; values over 255 clamp to 255
// (documented deviation).
#include "harness.h"
#include "test_pipeline.h"

using namespace qtermx;
using namespace qtermx::test;

TEST_CASE(rgb_helpers_round_trip)
{
    QTERMX_CHECK(rgb(0, 0, 0) == 0x1000000);
    QTERMX_CHECK(rgb(255, 0, 0) == 0x1FF0000);
    QTERMX_CHECK(rgb(12, 34, 56) == 0x10C2238);
    QTERMX_CHECK(isRgb(rgb(1, 2, 3)));
    QTERMX_CHECK(isRgb(0x1000000));
    QTERMX_CHECK(!isRgb(-1));      // default
    QTERMX_CHECK(!isRgb(255));     // palette index
    QTERMX_CHECK(!isRgb(0xFFFFF)); // below the RGB marker
    int r, g, b;
    rgbParts(rgb(12, 34, 56), r, g, b);
    QTERMX_CHECK(r == 12 && g == 34 && b == 56);
}

TEST_CASE(sgr_38_2_sets_rgb_foreground)
{
    Screen& screen = feedTo(U"\x1B[38;2;255;0;0mX");
    QTERMX_CHECK(screen.line(0).cells[0].fg == rgb(255, 0, 0));
    QTERMX_CHECK(screen.line(0).cells[0].bg == -1);
}

TEST_CASE(sgr_48_2_sets_rgb_background)
{
    Screen& screen = feedTo(U"\x1B[48;2;10;20;30mX");
    QTERMX_CHECK(screen.line(0).cells[0].bg == rgb(10, 20, 30));
    QTERMX_CHECK(screen.line(0).cells[0].fg == -1);
}

TEST_CASE(rgb_carries_other_attributes)
{
    Screen& screen = feedTo(U"\x1B[1;3;38;2;1;2;3mX");
    const Cell& cell = screen.line(0).cells[0];
    QTERMX_CHECK(cell.fg == rgb(1, 2, 3));
    QTERMX_CHECK(cell.bold);
    QTERMX_CHECK(cell.italic);
}

TEST_CASE(sgr_39_49_reset_rgb)
{
    Screen& screen = feedTo(U"\x1B[38;2;1;2;3;48;2;4;5;6m\x1B[39;49mX");
    const Cell& cell = screen.line(0).cells[0];
    QTERMX_CHECK(cell.fg == -1);
    QTERMX_CHECK(cell.bg == -1);
}

TEST_CASE(colon_form_sgr_38_2)
{
    // `38:2:r:g:b` — xterm's sub-parameter syntax — sets RGB fg.
    Screen& screen = feedTo(U"\x1B[38:2:255:0:0mX");
    QTERMX_CHECK(screen.line(0).cells[0].fg == rgb(255, 0, 0));
}

TEST_CASE(colon_form_sgr_48_2_with_color_space)
{
    // `48:2:cs:r:g:b` — the color-space slot (0) is accepted and
    // ignored; the remaining three components are the RGB value.
    Screen& screen = feedTo(U"\x1B[48:2:0:10:20:30mX");
    QTERMX_CHECK(screen.line(0).cells[0].bg == rgb(10, 20, 30));
}

TEST_CASE(colon_form_sgr_38_2_empty_color_space)
{
    // `38:2::r:g:b` — the empty `:` slot decodes to -1, which the
    // color-space position absorbs; the RGB components follow it.
    Screen& screen = feedTo(U"\x1B[38:2::1:2:3mX");
    QTERMX_CHECK(screen.line(0).cells[0].fg == rgb(1, 2, 3));
}

TEST_CASE(colon_form_sgr_38_5_palette_index)
{
    Screen& screen = feedTo(U"\x1B[38:5:196mX");
    QTERMX_CHECK(screen.line(0).cells[0].fg == 196);
}

TEST_CASE(colon_form_truncated_is_ignored)
{
    // A truncated colon form leaves the color untouched (no leftover
    // re-parse — the components are sub-params, not standalone SGR).
    Screen& screen = feedTo(U"\x1B[31m\x1B[38:2:1:2mX");
    const Cell& cell = screen.line(0).cells[0];
    QTERMX_CHECK(cell.fg == 1); // the SGR 31 from before, untouched
    QTERMX_CHECK(!cell.bold && !cell.dim);
}

TEST_CASE(colon_form_negative_components_clamp_to_zero)
{
    // `38:2::5:5` has an empty color-space slot and a missing blue: as
    // (2, r, g, b) that is r=-1, which clamps to 0 — the rest read as
    // written.
    Screen& screen = feedTo(U"\x1B[38:2::5:5mX");
    QTERMX_CHECK(screen.line(0).cells[0].fg == rgb(0, 5, 5));
}

TEST_CASE(colon_and_semicolon_forms_interleave)
{
    Screen& screen = feedTo(U"\x1B[38:2:1:2:3m\x1B[38;2;4;5;6mX");
    QTERMX_CHECK(screen.line(0).cells[0].fg == rgb(4, 5, 6)); // semicolon wins last
}

TEST_CASE(rgb_and_palette_interleave)
{
    Screen& screen = feedTo(U"\x1B[31m\x1B[38;2;9;9;9m\x1B[38;5;2mX");
    QTERMX_CHECK(screen.line(0).cells[0].fg == 2); // palette wins last
}

TEST_CASE(truncated_rgb_is_ignored)
{
    // A truncated 38;2/48;2 leaves the color untouched; the leftover
    // components fall through and re-parse as standalone SGR codes
    // (xterm.js-verbatim: `38;2;1;2` sets bold + dim).
    Screen& screen = feedTo(U"\x1B[31;44m\x1B[38;2;1;2m\x1B[48;2;1mX");
    const Cell& cell = screen.line(0).cells[0];
    QTERMX_CHECK(cell.fg == 1); // untouched by the malformed 38;2
    QTERMX_CHECK(cell.bg == 4); // untouched by the malformed 48;2
    QTERMX_CHECK(cell.bold && cell.dim); // the leftover 1;2 re-parsed as SGR
}

TEST_CASE(rgb_values_clamp_at_255)
{
    Screen& screen = feedTo(U"\x1B[38;2;300;0;128mX");
    QTERMX_CHECK(screen.line(0).cells[0].fg == rgb(255, 0, 128));
}

TEST_CASE(effective_rendition_passes_rgb_through)
{
    // The seam returns RGB ints as-is; DECSCNM swaps them.
    Screen& screen = feedTo(U"\x1B[38;2;1;2;3;48;2;4;5;6m\x1B[?5hX");
    int fg, bg;
    screen.effectiveRendition(0, 0, fg, bg);
    QTERMX_CHECK(fg == rgb(4, 5, 6) && bg == rgb(1, 2, 3));
    const Cell& cell = screen.line(0).cells[0];
    QTERMX_CHECK(cell.data == U"X");
    QTERMX_CHECK(cell.fg == rgb(1, 2, 3) && cell.bg == rgb(4, 5, 6));
}
