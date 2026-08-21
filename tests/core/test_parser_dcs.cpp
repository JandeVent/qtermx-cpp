// T5 — DCS/SOS/PM/APC parse-and-ignore: consumed, never dispatched, no
// desync (port of tests/parser/test_dcs.py).
#include "harness.h"
#include "test_recorder.h"

using namespace qtermx;
using namespace qtermx::test;

TEST_CASE(dcs_consumed_without_dispatch)
{
    expectEvents(feed(U"\x1BP1;2|payload\x1B\\abc"),
                 {escEvent("", "\\"), // trailing backslash of the two-byte ST
                  charsEvent(U"abc")});
}

TEST_CASE(dcs_with_intermediates)
{
    expectEvents(feed(U"\x1BP!|data\x1B\\"), {escEvent("", "\\")});
}

TEST_CASE(dcs_8bit_dcs_terminated_by_8bit_st)
{
    expectEvents(feed(U"\x90payload\x9C" "abc"), {charsEvent(U"abc")});
}

TEST_CASE(dcs_containing_escape_does_not_desync)
{
    // The ESC inside the payload ends the string; what follows parses fresh.
    expectEvents(feed(U"\x1BP|esc\x1B\\X"), {escEvent("", "\\"), charsEvent(U"X")});
}

TEST_CASE(dcs_csi_after_dcs_parses_correctly)
{
    expectEvents(feed(U"\x1BPxyz\x1B\\\x1B[31m"),
                 {escEvent("", "\\"), csiEvent("", "", Params{{31}}, "m")});
}

TEST_CASE(dcs_sos_ignored)
{
    expectEvents(feed(U"\x1BXpayload\x1B\\"), {escEvent("", "\\")});
}

TEST_CASE(dcs_pm_ignored)
{
    expectEvents(feed(U"\x1B^payload\x9C"), {});
}

TEST_CASE(dcs_8bit_sos_pm_ignored)
{
    expectEvents(feed(U"\x98payload\x9C"), {});
    expectEvents(feed(U"\x9Epayload\x9C"), {});
}

TEST_CASE(dcs_apc_ignored_until_st)
{
    expectEvents(feed(U"\x1B_payload\x9C" "abc"), {charsEvent(U"abc")});
}

TEST_CASE(dcs_apc_terminated_by_esc)
{
    // Unlike OSC, APC ends directly at ESC (xterm.js APC_END → GROUND).
    expectEvents(feed(U"\x1B_payload\x1B" U"7"), {charsEvent(U"7")});
}

TEST_CASE(dcs_apc_8bit)
{
    expectEvents(feed(U"\x9Fpayload\x9C"), {});
}

TEST_CASE(dcs_bel_does_not_terminate_apc)
{
    // BEL is not an APC terminator; the string continues until ST/ESC/CAN.
    expectEvents(feed(U"\x1B_payload\x07more\x9C" "abc"), {charsEvent(U"abc")});
}

TEST_CASE(dcs_stream_survives_all_string_types)
{
    const std::u32string soup = U"\x1BP|d\x1B\\\x9D" U"0;t\x07\x1B^p\x9C\x9F" U"apc\x9C\x1B]8;;u\x07";
    expectEvents(feed(soup),
                 {escEvent("", "\\"), oscEvent(U"0;t"), oscEvent(U"8;;u")});
}