// T2 — CSI sequences + typed Params (port of tests/parser/test_csi.py).
#include "harness.h"
#include "test_recorder.h"

using namespace qtermx;
using namespace qtermx::test;

TEST_CASE(csi_sgr_with_one_param)
{
    expectEvents(feed(U"\x1B[31m"), {csiEvent("", "", Params{{31}}, "m")});
}

TEST_CASE(csi_multiple_params)
{
    expectEvents(feed(U"\x1B[1;2;3A"),
                 {csiEvent("", "", Params{{1}, {2}, {3}}, "A")});
}

TEST_CASE(csi_multidigit_param)
{
    expectEvents(feed(U"\x1B[123m"), {csiEvent("", "", Params{{123}}, "m")});
}

TEST_CASE(csi_empty_param_is_zero)
{
    // "ESC [ ; 5 m": the first (empty) parameter defaults to 0.
    expectEvents(feed(U"\x1B[;5m"), {csiEvent("", "", Params{{0}, {5}}, "m")});
}

TEST_CASE(csi_no_params_is_zero_default_mode)
{
    // ECMA-48 default is sequence-specific; storage stores 0 (xterm.js ZDM).
    expectEvents(feed(U"\x1B[m"), {csiEvent("", "", Params{{0}}, "m")});
    expectEvents(feed(U"\x1B[A"), {csiEvent("", "", Params{{0}}, "A")});
}

TEST_CASE(csi_subparams)
{
    expectEvents(feed(U"\x1B[4:3m"), {csiEvent("", "", Params{{4, 3}}, "m")});
}

TEST_CASE(csi_empty_subparam_is_minus_one)
{
    // "5::6" — the middle sub-parameter is empty; xterm.js stores -1.
    expectEvents(feed(U"\x1B[5::6m"), {csiEvent("", "", Params{{5, -1, 6}}, "m")});
}

TEST_CASE(csi_private_prefix_question)
{
    expectEvents(feed(U"\x1B[?1h"), {csiEvent("", "?", Params{{1}}, "h")});
}

TEST_CASE(csi_private_prefix_greater_than)
{
    expectEvents(feed(U"\x1B[>0u"), {csiEvent("", ">", Params{{0}}, "u")});
}

TEST_CASE(csi_private_prefix_equals)
{
    expectEvents(feed(U"\x1B[=5c"), {csiEvent("", "=", Params{{5}}, "c")});
}

TEST_CASE(csi_intermediates)
{
    expectEvents(feed(U"\x1B[!p"), {csiEvent("!", "", Params{{0}}, "p")});
}

TEST_CASE(csi_8bit_csi)
{
    expectEvents(feed(U"\x9B" "31m"), {csiEvent("", "", Params{{31}}, "m")});
}

TEST_CASE(csi_second_prefix_byte_ignores_sequence)
{
    // A prefix byte after params started is malformed: ignore until final,
    // dispatch nothing, keep the stream in sync.
    expectEvents(feed(U"\x1B[1?5hX"), {charsEvent(U"X")});
}

TEST_CASE(csi_truncated_csi_waits_for_more_input)
{
    expectEvents(feed(U"\x1B[31"), {});
    expectEvents(feed(U"\x1B[31m"), {csiEvent("", "", Params{{31}}, "m")});
}

TEST_CASE(csi_spans_feeds)
{
    // Chunking mid-sequence must not change the outcome (T6 invariant).
    Recorder recorder;
    Parser parser(&recorder);
    parser.feed(U"\x1B[3");
    parser.feed(U"1m");
    parser.flush();
    expectEvents(recorder.events, {csiEvent("", "", Params{{31}}, "m")});
}

TEST_CASE(csi_can_cancels_csi)
{
    expectEvents(feed(U"\x1B[31\x18" "abc"), {execEvent(0x18), charsEvent(U"abc")});
}

TEST_CASE(csi_digit_overflow_is_capped)
{
    // Oracle (test_csi.py): the cap is 0xFFFFFFFF — the full 32-bit
    // value, not a wrapped signed int.
    expectEvents(feed(U"\x1B[99999999999999999999m"),
                 {csiEvent("", "", Params{{0xFFFFFFFF}}, "m")});
}

TEST_CASE(csi_params_capped_at_32)
{
    std::u32string seq = U"\x1B[";
    for (int i = 0; i < 40; ++i) {
        seq += U"1;";
    }
    seq.pop_back();
    seq += U"m";
    Params expected;
    for (int i = 0; i < 32; ++i) {
        expected.groups.push_back({1});
    }
    expectEvents(feed(seq), {csiEvent("", "", expected, "m")});
}