// T04 — Scroll region (DECSTBM) with xterm.js semantics (port of
// tests/screen/test_region.py). `CSI n;m r` clamps both bounds, ignores
// a region whose bottom is not below its top, moves the cursor home on
// a valid set, and does not touch origin mode itself. Resize resets the
// region to full screen. Scrolling happens within the region, leaving
// rows outside it untouched.
#include "harness.h"
#include "test_pipeline.h"

using namespace qtermx;
using namespace qtermx::test;

TEST_CASE(region_lf_at_region_bottom_scrolls_only_the_region)
{
    Screen& screen = feedTo(U"\x1B[3;5r" "a\r\nb\r\nc\r\nd\r\ne\r\n", 6, 4);
    const auto rows = splitLines(screen.render());
    QTERMX_CHECK(rows[0][0] == 'a'); // outside the region: untouched
    QTERMX_CHECK(rows[1][0] == 'b');
    QTERMX_CHECK(rows[2][0] == 'd'); // region scrolled up by one
    QTERMX_CHECK(rows[3][0] == 'e');
    QTERMX_CHECK(rows[4].find_first_not_of(' ') == std::string::npos); // fresh row
    QTERMX_CHECK(rows[5].find_first_not_of(' ') == std::string::npos); // untouched
}

TEST_CASE(region_wrap_at_region_bottom_scrolls_region)
{
    Screen& screen = feedTo(U"\x1B[3;5r" "a\r\nb\r\nc\r\nd\r\neeeeX", 6, 4);
    const auto rows = splitLines(screen.render());
    QTERMX_CHECK(rows[0][0] == 'a');
    QTERMX_CHECK(rows[1][0] == 'b');
    QTERMX_CHECK(rows[2][0] == 'd');
    QTERMX_CHECK(rows[3].substr(0, 4) == "eeee");
    QTERMX_CHECK(rows[4][0] == 'X'); // wrapped onto the fresh row
}

TEST_CASE(region_inverted_region_is_ignored)
{
    // bottom <= top: nothing changes — LF at the bottom still scrolls
    // the full screen.
    Screen& screen = feedTo(U"\x1B[5;3r" "a\r\nb\r\nc\r\nd\r\ne\r\nf\r\n", 6, 4);
    const auto rows = splitLines(screen.render());
    QTERMX_CHECK(rows[0][0] == 'b'); // full-screen scroll happened
    QTERMX_CHECK(rows[5].find_first_not_of(' ') == std::string::npos);
}

TEST_CASE(region_declaring_region_moves_cursor_home)
{
    Screen& screen = feedTo(U"\n\n\n\x1B[3;5r", 6, 4);
    QTERMX_CHECK(screen.cursor.x == 0);
    QTERMX_CHECK(screen.cursor.y == 0);
}

TEST_CASE(region_home_is_region_top_under_origin_mode)
{
    Screen& screen = feedTo(U"\n\n\n\x1B[?6h\x1B[3;5r", 6, 4);
    QTERMX_CHECK(screen.cursor.x == 0);
    QTERMX_CHECK(screen.cursor.y == 2);
}

TEST_CASE(region_decstbm_does_not_reset_origin_mode)
{
    Screen& screen = feedTo(U"\x1B[?6h\x1B[2;4r");
    QTERMX_CHECK(screen.mode(kDecom, true));
}

TEST_CASE(region_resize_resets_region_to_full_screen)
{
    Screen& screen = feedTo(U"\x1B[3;5r" "a\r\nb\r\nc\r\nd\r\ne", 6, 4);
    screen.resize(6, 4);
    screen.lineFeed(); // y=4 → 5 (region no longer [2,4])
    screen.lineFeed(); // at the full-screen bottom → scroll everything
    QTERMX_CHECK(splitLines(screen.render())[0][0] == 'b');
}

TEST_CASE(region_empty_params_reset_region_to_full_screen)
{
    Screen& screen = feedTo(U"\x1B[3;5r\x1B[r" "a\r\nb\r\nc\r\nd\r\ne", 6, 4);
    screen.lineFeed(); // y=4 → 5
    screen.lineFeed(); // at the full-screen bottom → scroll everything
    QTERMX_CHECK(splitLines(screen.render())[0][0] == 'b');
}