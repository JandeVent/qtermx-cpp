// T4 — OSC strings: payload collection, ST and BEL terminators (port of
// tests/parser/test_osc.py).
#include "harness.h"
#include "test_recorder.h"

using namespace qtermx;
using namespace qtermx::test;

TEST_CASE(osc_terminated_by_bel)
{
    expectEvents(feed(U"\x1B]0;title\x07"), {oscEvent(U"0;title")});
}

TEST_CASE(osc_terminated_by_esc_st)
{
    // xterm.js quirk: the ESC dispatches the payload, then the trailing
    // backslash of the two-byte ST dispatches as a no-op escape sequence.
    expectEvents(feed(U"\x1B]0;title\x1B\\"),
                 {oscEvent(U"0;title"), escEvent("", "\\")});
}

TEST_CASE(osc_terminated_by_8bit_st)
{
    expectEvents(feed(U"\x1B]0;title\x9C"), {oscEvent(U"0;title")});
}

TEST_CASE(osc_8bit_osc)
{
    expectEvents(feed(U"\x9D" U"0;title\x07"), {oscEvent(U"0;title")});
}

TEST_CASE(osc_can_cancels_osc_without_dispatch)
{
    // xterm.js: CAN terminates the string; the control is not executed.
    expectEvents(feed(U"\x1B]0;title\x18" "abc"), {charsEvent(U"abc")});
}

TEST_CASE(osc_sub_cancels_osc_without_dispatch)
{
    expectEvents(feed(U"\x1B]0;title\x1A" "abc"), {charsEvent(U"abc")});
}

TEST_CASE(osc_del_is_payload_in_osc)
{
    expectEvents(feed(U"\x1B]0;ti\x7Ftle\x07"), {oscEvent(U"0;ti\x7Ftle")});
}

TEST_CASE(osc_non_ascii_payload)
{
    expectEvents(feed(U"\x1B]8;;https://é.example\x07"),
                 {oscEvent(U"8;;https://é.example")});
}

TEST_CASE(osc_executables_inside_osc_are_ignored)
{
    // Build explicitly: a NUL in a C++ string literal would terminate it.
    std::u32string s = U"\x1B]0;ti";
    s.push_back(0);
    s += U"tle\x07";
    expectEvents(feed(s), {oscEvent(U"0;title")});
}

TEST_CASE(osc_restarted_by_new_osc)
{
    // A second 8-bit OSC_START aborts the first payload without dispatching
    // it (ESC cannot do this — ESC always terminates the string).
    expectEvents(feed(U"\x9D" U"0;first\x9D" U"0;second\x07"), {oscEvent(U"0;second")});
}

TEST_CASE(osc_esc_then_other_escape)
{
    // ESC ends the payload; the following escape byte dispatches normally.
    expectEvents(feed(U"\x1B]0;title\x1B" U"7"),
                 {oscEvent(U"0;title"), escEvent("", "7")});
}

TEST_CASE(osc_text_around_osc)
{
    expectEvents(feed(U"hello\x1B]0;x\x07world"),
                 {charsEvent(U"hello"), oscEvent(U"0;x"), charsEvent(U"world")});
}