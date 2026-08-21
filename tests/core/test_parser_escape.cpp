// T3 — ESC sequences + charset designation (port of tests/parser/test_escape.py).
#include "harness.h"
#include "test_recorder.h"

using namespace qtermx;
using namespace qtermx::test;

TEST_CASE(esc_sequence_dispatches_by_final)
{
    expectEvents(feed(U"\x1B" U"7"), {escEvent("", "7")});
}

TEST_CASE(esc_sequence_with_intermediates)
{
    // ESC # 8 (DECALN): '#' is the intermediate byte.
    expectEvents(feed(U"\x1B#8"), {escEvent("#", "8")});
}

TEST_CASE(esc_intermediate_then_final_across_escapes)
{
    expectEvents(feed(U"\x1B!A"), {escEvent("!", "A")});
}

TEST_CASE(esc_multiple_intermediates)
{
    expectEvents(feed(U"\x1B#$x"), {escEvent("#$", "x")});
}

TEST_CASE(esc_charset_designation_g0)
{
    expectEvents(feed(U"\x1B(0"), {charsetEvent("(", "0")});
}

TEST_CASE(esc_charset_designation_g1)
{
    expectEvents(feed(U"\x1B)A"), {charsetEvent(")", "A")});
}

TEST_CASE(esc_charset_designation_g2_and_g3)
{
    expectEvents(feed(U"\x1B*B"), {charsetEvent("*", "B")});
    expectEvents(feed(U"\x1B+4"), {charsetEvent("+", "4")});
}

TEST_CASE(esc_charset_then_text)
{
    expectEvents(feed(U"\x1B(0abc"),
                 {charsetEvent("(", "0"), charsEvent(U"abc")});
}

TEST_CASE(esc_c1_execute_range)
{
    // IND (0x84), NEL (0x85), HTS (0x88), RI (0x8D) as 8-bit code points.
    for (const int code : {0x84, 0x85, 0x88, 0x8D, 0x8E, 0x8F, 0x91, 0x97}) {
        expectEvents(feed(std::u32string(1, static_cast<char32_t>(code))),
                     {execEvent(code)});
    }
}

TEST_CASE(esc_then_executable_stays_in_escape)
{
    // xterm.js: executables execute inside ESCAPE and the escape continues.
    expectEvents(feed(U"\x1B\n7"), {execEvent(10), escEvent("", "7")});
}

TEST_CASE(esc_truncated_escape_waits)
{
    expectEvents(feed(U"\x1B"), {});
    expectEvents(feed(U"\x1B" U"7"), {escEvent("", "7")});
}

TEST_CASE(esc_ignored_until_next_final_after_bad_intermediate)
{
    // ESC followed by an unrecognized intermediate then a printable is
    // implementation-defined; we return to ground without dispatch.
    expectEvents(feed(U"\x1B\x1B" U"7"), {escEvent("", "7")});
}