// T8 — Charset designation and level shifts (port of
// tests/screen/test_charsets.py). The screen owns four slots G0–G3 plus
// the active level; `ESC ( 0` fills G0 with the line-drawing charset,
// `ESC ( B` restores ASCII, `ESC ( A` the UK charset. Print translates
// ASCII code points (below 0x7F) through the active slot's map; Unicode
// is never translated. SI/SO pick G0/G1, `ESC n`/`o` G2/G3, `ESC ~`/`}`/
// `|` G1R/G2R/G3R.
#include "harness.h"
#include "test_pipeline.h"

using namespace qtermx;
using namespace qtermx::test;

TEST_CASE(charsets_default_is_ascii)
{
    Screen& screen = feedTo(U"q");
    QTERMX_CHECK(screen.line(0).cells[0].data == U"q");
}

TEST_CASE(charsets_esc_paren_zero_designates_line_drawing)
{
    Screen& screen = feedTo(U"\x1B(0q");
    QTERMX_CHECK(screen.line(0).cells[0].data == U"\u2500"); // ─
}

TEST_CASE(charsets_esc_paren_b_restores_ascii)
{
    Screen& screen = feedTo(U"\x1B(0q\x1B(Bq");
    QTERMX_CHECK(screen.line(0).cells[0].data == U"\u2500");
    QTERMX_CHECK(screen.line(0).cells[1].data == U"q");
}

TEST_CASE(charsets_line_drawing_box_corners)
{
    Screen& screen = feedTo(U"\x1B(0lmkq");
    QTERMX_CHECK(screen.line(0).cells[0].data == U"\u250C"); // ┌
    QTERMX_CHECK(screen.line(0).cells[1].data == U"\u2514"); // └
    QTERMX_CHECK(screen.line(0).cells[2].data == U"\u2510"); // ┐
    QTERMX_CHECK(screen.line(0).cells[3].data == U"\u2500"); // ─
}

TEST_CASE(charsets_line_drawing_vertical_and_junctions)
{
    Screen& screen = feedTo(U"\x1B(0xtuwv");
    QTERMX_CHECK(screen.line(0).cells[0].data == U"\u2502"); // │
    QTERMX_CHECK(screen.line(0).cells[1].data == U"\u251C"); // ├
    QTERMX_CHECK(screen.line(0).cells[2].data == U"\u2524"); // ┤
    QTERMX_CHECK(screen.line(0).cells[3].data == U"\u252C"); // ┬ (w)
    QTERMX_CHECK(screen.line(0).cells[4].data == U"\u2534"); // ┴ (v)
}

TEST_CASE(charsets_uk_charset_maps_pound)
{
    Screen& screen = feedTo(U"\x1B(A#a");
    QTERMX_CHECK(screen.line(0).cells[0].data == U"\u00A3"); // £
    QTERMX_CHECK(screen.line(0).cells[1].data == U"a");      // the rest is ASCII
}

TEST_CASE(charsets_unknown_charset_name_ignored)
{
    Screen& screen = feedTo(U"\x1B(1q");
    QTERMX_CHECK(screen.line(0).cells[0].data == U"q");
}

TEST_CASE(charsets_designation_into_other_slots)
{
    // G1 designated, then made active via SO
    Screen& screen = feedTo(U"\x1B)0q\x0Eq");
    QTERMX_CHECK(screen.line(0).cells[0].data == U"q");      // G0 still ASCII
    QTERMX_CHECK(screen.line(0).cells[1].data == U"\u2500"); // SO switched to G1
}

TEST_CASE(charsets_si_so_toggle)
{
    Screen& screen = feedTo(U"\x1B)0\x0Eq\x0Fq");
    QTERMX_CHECK(screen.line(0).cells[0].data == U"\u2500"); // SO → G1
    QTERMX_CHECK(screen.line(0).cells[1].data == U"q");      // SI → G0
}

TEST_CASE(charsets_ls2_ls3_and_right_shifts)
{
    Screen& screen = feedTo(U"\x1B*0\x1Bnq"); // G2 designated, ESC n → G2
    QTERMX_CHECK(screen.line(0).cells[0].data == U"\u2500");
    Screen& screen2 = feedTo(U"\x1B+0\x1Boq"); // G3 designated, ESC o → G3
    QTERMX_CHECK(screen2.line(0).cells[0].data == U"\u2500");
    Screen& screen3 = feedTo(U"\x1B*0\x1B}q"); // G2 via LS2R
    QTERMX_CHECK(screen3.line(0).cells[0].data == U"\u2500");
    Screen& screen4 = feedTo(U"\x1B+0\x1B|q"); // G3 via LS3R
    QTERMX_CHECK(screen4.line(0).cells[0].data == U"\u2500");
    Screen& screen5 = feedTo(U"\x1B)0\x1B~q"); // G1 via LS1R
    QTERMX_CHECK(screen5.line(0).cells[0].data == U"\u2500");
}

TEST_CASE(charsets_unicode_never_translated)
{
    Screen& screen = feedTo(U"\x1B(0中q");
    QTERMX_CHECK(screen.line(0).cells[0].data == U"中");
    QTERMX_CHECK(screen.line(0).cells[2].data == U"\u2500");
}

TEST_CASE(charsets_high_codepoints_pass_through)
{
    Screen& screen = feedTo(U"\x1B(0\u0100");
    QTERMX_CHECK(screen.line(0).cells[0].data == U"\u0100");
}

TEST_CASE(charsets_line_drawing_is_single_width)
{
    Screen& screen = feedTo(U"\x1B(0q", 24, 4);
    QTERMX_CHECK(screen.cursor.x == 1); // ─ is one cell, not two
}