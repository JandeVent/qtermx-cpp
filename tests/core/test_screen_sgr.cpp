// T3 — SGR graphic rendition in the emulator (port of
// tests/screen/test_sgr.py). `csi_dispatch` with final `m` sets the
// cursor's graphic rendition; printed cells are stamped with it.
#include "harness.h"
#include "test_pipeline.h"

using namespace qtermx;
using namespace qtermx::test;

TEST_CASE(sgr_reset_restores_defaults)
{
    Screen& screen = feedTo(U"\x1B[31mred\x1B[0mplain");
    QTERMX_CHECK(splitLines(screen.render())[0].rfind("redplain", 0) == 0);
    const Cell& cell = screen.line(0).cells[3]; // 'p'
    QTERMX_CHECK(cell.fg == -1);
    QTERMX_CHECK(cell.bg == -1);
}

TEST_CASE(sgr_fg_colors_following_text)
{
    Screen& screen = feedTo(U"\x1B[31mred");
    for (int i = 0; i < 3; ++i) {
        const Cell& cell = screen.line(0).cells[i];
        QTERMX_CHECK(cell.data == std::u32string(1, U"red"[i]));
        QTERMX_CHECK(cell.fg == 1);
    }
}

TEST_CASE(sgr_bg_colors_following_text)
{
    Screen& screen = feedTo(U"\x1B[44mX");
    QTERMX_CHECK(screen.line(0).cells[0].bg == 4);
}

TEST_CASE(sgr_38_5_sets_palette_index)
{
    Screen& screen = feedTo(U"\x1B[38;5;196mX");
    QTERMX_CHECK(screen.line(0).cells[0].fg == 196);
}

TEST_CASE(sgr_missing_params_default_to_reset)
{
    Screen& screen = feedTo(U"\x1B[31mred\x1B[mplain");
    QTERMX_CHECK(screen.line(0).cells[3].fg == -1);
}

TEST_CASE(sgr_truncated_38_5_is_ignored)
{
    // A 38;5 without the index is malformed — xterm ignores it rather
    // than defaulting to black.
    Screen& screen = feedTo(U"\x1B[38;5mX");
    QTERMX_CHECK(screen.line(0).cells[0].fg == -1);
}

TEST_CASE(sgr_applies_to_run_of_chars)
{
    Screen& screen = feedTo(U"\x1B[32mhello");
    for (int i = 0; i < 5; ++i) {
        QTERMX_CHECK(screen.line(0).cells[i].fg == 2);
    }
}

TEST_CASE(sgr_unknown_params_are_ignored)
{
    // Font selection (10) is not in scope; must not break the stream.
    Screen& screen = feedTo(U"\x1B[10mplain\x1B[0m");
    QTERMX_CHECK(splitLines(screen.render())[0].rfind("plain", 0) == 0);
    QTERMX_CHECK(!screen.line(0).cells[0].bold);
}

TEST_CASE(sgr_multiple_in_one_sequence)
{
    Screen& screen = feedTo(U"\x1B[31;44mX");
    const Cell& cell = screen.line(0).cells[0];
    QTERMX_CHECK(cell.fg == 1);
    QTERMX_CHECK(cell.bg == 4);
}

TEST_CASE(sgr_unknown_csi_finals_are_ignored)
{
    Screen& screen = feedTo(U"\x1B[2Jab");
    QTERMX_CHECK(splitLines(screen.render())[0].rfind("ab", 0) == 0);
}

// -- Full attribute set (T7) --------------------------------------------

TEST_CASE(sgr_sets_each_attribute_flag)
{
    struct Case {
        std::u32string seq;
        bool Cell::*flag;
    };
    const Case cases[] = {
        {U"\x1B[1m", &Cell::bold},   {U"\x1B[2m", &Cell::dim},
        {U"\x1B[3m", &Cell::italic}, {U"\x1B[4m", &Cell::underline},
        {U"\x1B[5m", &Cell::blink},  {U"\x1B[6m", &Cell::blink}, // rapid → blink
        {U"\x1B[7m", &Cell::reverse}, {U"\x1B[8m", &Cell::hidden},
        {U"\x1B[9m", &Cell::strike}, {U"\x1B[53m", &Cell::overline},
    };
    for (const auto& c : cases) {
        Screen& screen = feedTo(c.seq + U"X");
        QTERMX_CHECK(screen.line(0).cells[0].*c.flag);
    }
}

TEST_CASE(sgr_attribute_composition)
{
    Screen& screen = feedTo(U"\x1B[1;3;7mX");
    const Cell& cell = screen.line(0).cells[0];
    QTERMX_CHECK(cell.bold && cell.italic && cell.reverse);
}

TEST_CASE(sgr_21_double_underline_sets_underline)
{
    Screen& screen = feedTo(U"\x1B[21mX");
    QTERMX_CHECK(screen.line(0).cells[0].underline);
}

TEST_CASE(sgr_22_clears_bold_and_dim)
{
    Screen& screen = feedTo(U"\x1B[1;2mX\x1B[22mY");
    QTERMX_CHECK(!screen.line(0).cells[1].bold);
    QTERMX_CHECK(!screen.line(0).cells[1].dim);
}

TEST_CASE(sgr_resets_clear_their_flags)
{
    Screen& screen = feedTo(U"\x1B[3;4;5;7;8;9;53mX\x1B[23;24;25;27;28;29;55mY");
    const Cell& cell = screen.line(0).cells[1];
    QTERMX_CHECK(!cell.italic && !cell.underline && !cell.blink && !cell.reverse &&
                 !cell.hidden && !cell.strike && !cell.overline);
}

TEST_CASE(sgr_reset_clears_new_flags_too)
{
    Screen& screen = feedTo(U"\x1B[1;2;3;4;5;6;7;8;9;53mX\x1B[0mY");
    const Cell& cell = screen.line(0).cells[1];
    QTERMX_CHECK(!cell.bold && !cell.dim && !cell.italic && !cell.underline &&
                 !cell.blink && !cell.reverse && !cell.hidden && !cell.strike &&
                 !cell.overline);
    QTERMX_CHECK(cell.fg == -1);
    QTERMX_CHECK(cell.bg == -1);
}

TEST_CASE(sgr_39_49_restore_default_colors)
{
    Screen& screen = feedTo(U"\x1B[31;44mX\x1B[39;49mY");
    const Cell& cell = screen.line(0).cells[1];
    QTERMX_CHECK(cell.fg == -1);
    QTERMX_CHECK(cell.bg == -1);
}

TEST_CASE(sgr_bright_fg_maps_to_palette_8_15)
{
    Screen& screen = feedTo(U"\x1B[90mX\x1B[97mY");
    QTERMX_CHECK(screen.line(0).cells[0].fg == 8);
    QTERMX_CHECK(screen.line(0).cells[1].fg == 15);
}

TEST_CASE(sgr_bright_bg_maps_to_palette_8_15)
{
    Screen& screen = feedTo(U"\x1B[100mX\x1B[107mY");
    QTERMX_CHECK(screen.line(0).cells[0].bg == 8);
    QTERMX_CHECK(screen.line(0).cells[1].bg == 15);
}

TEST_CASE(sgr_48_5_sets_bg_palette_index)
{
    Screen& screen = feedTo(U"\x1B[48;5;196mX");
    QTERMX_CHECK(screen.line(0).cells[0].bg == 196);
}