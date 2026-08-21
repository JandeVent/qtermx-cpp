// P0 — parser state machine, ported from xterm.js
// EscapeSequenceParser.test.ts (port of tests/parser/test_parser_states.py).
// The suite drives the parser one code point at a time from a chosen
// state and asserts the resulting state, collected buffers, and
// dispatcher calls. Divergences from xterm.js are pyqtermx's deliberate
// choices, preserved verbatim (see the Python file's docstring).
#include <set>

#include "harness.h"
#include "test_recorder.h"

using namespace qtermx;
using namespace qtermx::test;

namespace {

// xterm.js EXECUTABLES: 0x00–0x17, 0x19, 0x1C–0x1F (CAN/SUB/ESC excluded).
const std::vector<int> kExecutables = [] {
    std::vector<int> v;
    for (int i = 0x00; i < 0x18; ++i) {
        v.push_back(i);
    }
    v.push_back(0x19);
    for (int i = 0x1C; i < 0x20; ++i) {
        v.push_back(i);
    }
    return v;
}();

// C1 controls that execute everywhere (xterm.js global-anywhere set).
const std::vector<int> kC1Execute = [] {
    std::vector<int> v;
    for (int i = 0x80; i < 0x90; ++i) {
        v.push_back(i);
    }
    for (int i = 0x91; i < 0x98; ++i) {
        v.push_back(i);
    }
    v.push_back(0x99);
    v.push_back(0x9A);
    return v;
}();

std::u32string cp(int code)
{
    return std::u32string(1, static_cast<char32_t>(code));
}

std::string ascii(int code)
{
    return std::string(1, static_cast<char>(code));
}

} // namespace

// ---------------------------------------------------------------------------
// Parser init and methods
// ---------------------------------------------------------------------------

TEST_CASE(states_initial_state)
{
    Probe probe;
    QTERMX_CHECK(probe.state() == ParserState::GROUND);
    QTERMX_CHECK((probe.paramsGroups() == std::vector<std::vector<int64_t>>{{0}}));
    QTERMX_CHECK(probe.intermediates().empty());
    QTERMX_CHECK(probe.prefix().empty());
    QTERMX_CHECK(probe.osc().empty());
    QTERMX_CHECK(probe.events().empty());
}

TEST_CASE(states_reset_returns_to_initial_state)
{
    Probe probe;
    probe.setState(ParserState::CSI_PARAM);
    probe.setOsc(U"#");
    probe.dirty();
    probe.reset();
    QTERMX_CHECK(probe.state() == ParserState::GROUND);
    QTERMX_CHECK((probe.paramsGroups() == std::vector<std::vector<int64_t>>{{0}}));
    QTERMX_CHECK(probe.intermediates().empty());
    QTERMX_CHECK(probe.prefix().empty());
    QTERMX_CHECK(probe.osc().empty());
    QTERMX_CHECK(probe.events().empty());
}

// ---------------------------------------------------------------------------
// GROUND
// ---------------------------------------------------------------------------

TEST_CASE(states_ground_c0_executables_execute_and_stay)
{
    for (const int code : kExecutables) {
        Probe probe = feedFrom(ParserState::GROUND, cp(code));
        QTERMX_CHECK(probe.state() == ParserState::GROUND);
        expectEvents(probe.events(), {execEvent(code)});
    }
}

TEST_CASE(states_ground_ascii_printables_print)
{
    for (int code = 0x20; code < 0x7F; ++code) { // DEL (0x7F) excluded
        Probe probe = feedFrom(ParserState::GROUND, cp(code));
        QTERMX_CHECK(probe.state() == ParserState::GROUND);
        expectEvents(probe.events(), {charsEvent(cp(code))});
    }
}

TEST_CASE(states_ground_unicode_printables_print)
{
    Probe probe = feedFrom(ParserState::GROUND, U"中😀");
    expectEvents(probe.events(), {charsEvent(U"中😀")});
}

TEST_CASE(states_ground_del_is_ignored)
{
    Probe probe = feedFrom(ParserState::GROUND, U"\x7F");
    QTERMX_CHECK(probe.state() == ParserState::GROUND);
    QTERMX_CHECK(probe.events().empty());
}

// ---------------------------------------------------------------------------
// Global-anywhere rules
// ---------------------------------------------------------------------------

TEST_CASE(states_global_can_sub_and_c1_executes_return_to_ground)
{
    // Exceptions: OSC_STRING aborts (no execute), APC_ENTRY ends with
    // plain IGNORE (no execute) — everywhere else CAN/SUB/C1 execute.
    const std::vector<ParserState> exceptions = {ParserState::OSC_STRING,
                                                 ParserState::APC_ENTRY};
    for (int state = 0; state <= static_cast<int>(ParserState::APC_ENTRY); ++state) {
        const ParserState st = static_cast<ParserState>(state);
        for (const int code : {0x18, 0x1A}) {
            Probe probe = feedFrom(st, cp(code));
            QTERMX_CHECK(probe.state() == ParserState::GROUND);
            const bool isException =
                std::find(exceptions.begin(), exceptions.end(), st) != exceptions.end();
            if (isException) {
                QTERMX_CHECK(probe.events().empty());
            } else {
                expectEvents(probe.events(), {execEvent(code)});
            }
        }
        for (const int code : kC1Execute) {
            Probe probe = feedFrom(st, cp(code));
            QTERMX_CHECK(probe.state() == ParserState::GROUND);
            expectEvents(probe.events(), {execEvent(code)});
        }
    }
}

TEST_CASE(states_global_8bit_st_is_swallowed_everywhere)
{
    for (int state = 0; state <= static_cast<int>(ParserState::APC_ENTRY); ++state) {
        const ParserState st = static_cast<ParserState>(state);
        Probe probe = feedFrom(st, U"\x9C");
        QTERMX_CHECK(probe.state() == ParserState::GROUND);
        if (st != ParserState::OSC_STRING) {
            // OSC_STRING ends at ST with an (empty) osc_dispatch.
            QTERMX_CHECK(probe.events().empty());
        }
    }
}

TEST_CASE(states_global_esc_goes_to_escape_and_clears)
{
    for (int state = 0; state <= static_cast<int>(ParserState::APC_ENTRY); ++state) {
        const ParserState st = static_cast<ParserState>(state);
        Probe probe;
        probe.setState(st);
        probe.dirty();
        probe.feed(U"\x1B");
        probe.flush();
        if (st == ParserState::APC_ENTRY) {
            // APC ends at ESC with plain IGNORE: no escape is restarted.
            QTERMX_CHECK(probe.state() == ParserState::GROUND);
        } else {
            QTERMX_CHECK(probe.state() == ParserState::ESCAPE);
        }
        if (st != ParserState::OSC_STRING && st != ParserState::APC_ENTRY) {
            // The CLEAR action drops collected state (OSC_END does not).
            QTERMX_CHECK((probe.paramsGroups() == std::vector<std::vector<int64_t>>{{0}}));
            QTERMX_CHECK(probe.intermediates().empty());
            QTERMX_CHECK(probe.prefix().empty());
        }
    }
}

TEST_CASE(states_global_8bit_introducers_enter_their_state_everywhere)
{
    struct Case {
        int code;
        ParserState expected;
    };
    const Case cases[] = {
        {0x9B, ParserState::CSI_ENTRY}, {0x90, ParserState::DCS_ENTRY},
        {0x9F, ParserState::APC_ENTRY}, {0x9D, ParserState::OSC_STRING},
        {0x98, ParserState::SOS_PM_STRING}, {0x9E, ParserState::SOS_PM_STRING},
    };
    for (const auto& c : cases) {
        for (int state = 0; state <= static_cast<int>(ParserState::APC_ENTRY); ++state) {
            const ParserState st = static_cast<ParserState>(state);
            Probe probe;
            probe.setState(st);
            probe.dirty();
            probe.feed(cp(c.code));
            probe.flush();
            QTERMX_CHECK(probe.state() == c.expected);
            if (c.code == 0x9B || c.code == 0x90 || c.code == 0x9F) {
                // CLEAR action drops collected state.
                QTERMX_CHECK((probe.paramsGroups() == std::vector<std::vector<int64_t>>{{0}}));
                QTERMX_CHECK(probe.intermediates().empty());
                QTERMX_CHECK(probe.prefix().empty());
            }
        }
    }
}

// ---------------------------------------------------------------------------
// ESCAPE
// ---------------------------------------------------------------------------

TEST_CASE(states_escape_c0_executables_execute_and_stay)
{
    for (const int code : kExecutables) {
        Probe probe = feedFrom(ParserState::ESCAPE, cp(code));
        QTERMX_CHECK(probe.state() == ParserState::ESCAPE);
        expectEvents(probe.events(), {execEvent(code)});
    }
}

TEST_CASE(states_escape_del_is_ignored)
{
    Probe probe = feedFrom(ParserState::ESCAPE, U"\x7F");
    QTERMX_CHECK(probe.state() == ParserState::ESCAPE);
    QTERMX_CHECK(probe.events().empty());
}

TEST_CASE(states_escape_finals_dispatch_to_ground)
{
    // 0x30–0x7E minus the routes claimed by other states: the charset
    // designators ( ) * + → CHARSET, P → DCS, X → SOS, [ → CSI,
    // ] → OSC, ^ → PM, _ → APC.
    const std::set<int> claimed = {0x28, 0x29, 0x2A, 0x2B, 0x50, 0x58,
                                   0x5B, 0x5D, 0x5E, 0x5F};
    for (int code = 0x30; code < 0x7F; ++code) {
        if (claimed.find(code) != claimed.end()) {
            continue;
        }
        Probe probe = feedFrom(ParserState::ESCAPE, cp(code));
        QTERMX_CHECK(probe.state() == ParserState::GROUND);
        expectEvents(probe.events(), {escEvent("", ascii(code))});
    }
}

TEST_CASE(states_escape_esc_backslash_dispatches)
{
    // Divergence: xterm.js registers a swallowing handler for ESC \;
    // pyqtermx dispatches it as an ordinary escape sequence.
    expectEvents(feed(U"\x1B\\"), {escEvent("", "\\")});
}

TEST_CASE(states_escape_intermediates_collect_to_escape_intermediate)
{
    for (int code = 0x20; code < 0x28; ++code) {
        Probe probe = feedFrom(ParserState::ESCAPE, cp(code));
        QTERMX_CHECK(probe.state() == ParserState::ESCAPE_INTERMEDIATE);
        QTERMX_CHECK(probe.intermediates() == ascii(code));
    }
    for (int code = 0x2C; code < 0x30; ++code) {
        Probe probe = feedFrom(ParserState::ESCAPE, cp(code));
        QTERMX_CHECK(probe.state() == ParserState::ESCAPE_INTERMEDIATE);
        QTERMX_CHECK(probe.intermediates() == ascii(code));
    }
}

TEST_CASE(states_escape_charset_designators_route_to_charset)
{
    for (const int code : {0x28, 0x29, 0x2A, 0x2B}) {
        Probe probe = feedFrom(ParserState::ESCAPE, cp(code));
        QTERMX_CHECK(probe.state() == ParserState::CHARSET);
        QTERMX_CHECK(probe.intermediates() == ascii(code));
    }
}

TEST_CASE(states_escape_7bit_string_introducers_route_to_their_states)
{
    QTERMX_CHECK(feedFrom(ParserState::ESCAPE, U"[").state() == ParserState::CSI_ENTRY);
    QTERMX_CHECK(feedFrom(ParserState::ESCAPE, U"]").state() == ParserState::OSC_STRING);
    QTERMX_CHECK(feedFrom(ParserState::ESCAPE, U"P").state() == ParserState::DCS_ENTRY);
    QTERMX_CHECK(feedFrom(ParserState::ESCAPE, U"X").state() == ParserState::SOS_PM_STRING);
    QTERMX_CHECK(feedFrom(ParserState::ESCAPE, U"^").state() == ParserState::SOS_PM_STRING);
    QTERMX_CHECK(feedFrom(ParserState::ESCAPE, U"_").state() == ParserState::APC_ENTRY);
}

// ---------------------------------------------------------------------------
// ESCAPE_INTERMEDIATE
// ---------------------------------------------------------------------------

TEST_CASE(states_escape_intermediate_c0_executables_execute_and_stay)
{
    for (const int code : kExecutables) {
        Probe probe = feedFrom(ParserState::ESCAPE_INTERMEDIATE, cp(code));
        QTERMX_CHECK(probe.state() == ParserState::ESCAPE_INTERMEDIATE);
        expectEvents(probe.events(), {execEvent(code)});
    }
}

TEST_CASE(states_escape_intermediate_del_is_ignored)
{
    Probe probe = feedFrom(ParserState::ESCAPE_INTERMEDIATE, U"\x7F");
    QTERMX_CHECK(probe.state() == ParserState::ESCAPE_INTERMEDIATE);
    QTERMX_CHECK(probe.events().empty());
}

TEST_CASE(states_escape_intermediate_intermediates_collect)
{
    for (int code = 0x20; code < 0x30; ++code) {
        Probe probe = feedFrom(ParserState::ESCAPE_INTERMEDIATE, cp(code));
        QTERMX_CHECK(probe.state() == ParserState::ESCAPE_INTERMEDIATE);
        QTERMX_CHECK(probe.intermediates() == ascii(code));
    }
}

TEST_CASE(states_escape_intermediate_finals_dispatch_to_ground)
{
    for (int code = 0x30; code < 0x7F; ++code) {
        Probe probe = feedFrom(ParserState::ESCAPE_INTERMEDIATE, cp(code));
        QTERMX_CHECK(probe.state() == ParserState::GROUND);
        expectEvents(probe.events(), {escEvent("", ascii(code))});
    }
}

TEST_CASE(states_escape_intermediate_full_sequence_with_intermediates)
{
    expectEvents(feed(U"\x1B#8"), {escEvent("#", "8")});
}

// ---------------------------------------------------------------------------
// CHARSET (pyqtermx-specific state)
// ---------------------------------------------------------------------------

TEST_CASE(states_charset_finals_designate_to_ground)
{
    for (int code = 0x30; code < 0x7F; ++code) {
        Probe probe = feedFrom(ParserState::CHARSET, cp(code));
        QTERMX_CHECK(probe.state() == ParserState::GROUND);
        expectEvents(probe.events(), {charsetEvent("", ascii(code))});
    }
}

TEST_CASE(states_charset_intermediates_collect)
{
    for (int code = 0x20; code < 0x30; ++code) {
        Probe probe = feedFrom(ParserState::CHARSET, cp(code));
        QTERMX_CHECK(probe.state() == ParserState::CHARSET);
        QTERMX_CHECK(probe.intermediates() == ascii(code));
    }
}

TEST_CASE(states_charset_full_designation_sequence)
{
    expectEvents(feed(U"\x1B(0"), {charsetEvent("(", "0")});
}

// ---------------------------------------------------------------------------
// CSI_ENTRY
// ---------------------------------------------------------------------------

TEST_CASE(states_csi_entry_c0_executables_execute_and_stay)
{
    for (const int code : kExecutables) {
        Probe probe = feedFrom(ParserState::CSI_ENTRY, cp(code));
        QTERMX_CHECK(probe.state() == ParserState::CSI_ENTRY);
        expectEvents(probe.events(), {execEvent(code)});
    }
}

TEST_CASE(states_csi_entry_del_is_ignored)
{
    Probe probe = feedFrom(ParserState::CSI_ENTRY, U"\x7F");
    QTERMX_CHECK(probe.state() == ParserState::CSI_ENTRY);
    QTERMX_CHECK(probe.events().empty());
}

TEST_CASE(states_csi_entry_finals_dispatch_to_ground)
{
    for (int code = 0x40; code < 0x7F; ++code) {
        Probe probe = feedFrom(ParserState::CSI_ENTRY, cp(code));
        QTERMX_CHECK(probe.state() == ParserState::GROUND);
        expectEvents(probe.events(), {csiEvent("", "", Params{{0}}, ascii(code))});
    }
}

TEST_CASE(states_csi_entry_digits_collect_as_param)
{
    for (int digit = 0; digit < 10; ++digit) {
        Probe probe = feedFrom(ParserState::CSI_ENTRY, cp(0x30 + digit));
        QTERMX_CHECK(probe.state() == ParserState::CSI_PARAM);
        QTERMX_CHECK((probe.paramsGroups() == std::vector<std::vector<int64_t>>{{digit}}));
    }
}

TEST_CASE(states_csi_entry_semicolon_starts_second_param)
{
    Probe probe = feedFrom(ParserState::CSI_ENTRY, U";");
    QTERMX_CHECK(probe.state() == ParserState::CSI_PARAM);
    QTERMX_CHECK((probe.paramsGroups() == std::vector<std::vector<int64_t>>{{0}, {0}}));
}

TEST_CASE(states_csi_entry_colon_starts_subparam)
{
    Probe probe = feedFrom(ParserState::CSI_ENTRY, U":");
    QTERMX_CHECK(probe.state() == ParserState::CSI_PARAM);
    QTERMX_CHECK((probe.paramsGroups() == std::vector<std::vector<int64_t>>{{0, -1}}));
}

TEST_CASE(states_csi_entry_private_prefix_collects)
{
    for (int code = 0x3C; code < 0x40; ++code) {
        Probe probe = feedFrom(ParserState::CSI_ENTRY, cp(code));
        QTERMX_CHECK(probe.state() == ParserState::CSI_PARAM);
        QTERMX_CHECK(probe.prefix() == ascii(code));
    }
}

TEST_CASE(states_csi_entry_intermediates_collect_to_csi_intermediate)
{
    for (int code = 0x20; code < 0x30; ++code) {
        Probe probe = feedFrom(ParserState::CSI_ENTRY, cp(code));
        QTERMX_CHECK(probe.state() == ParserState::CSI_INTERMEDIATE);
        QTERMX_CHECK(probe.intermediates() == ascii(code));
    }
}

// ---------------------------------------------------------------------------
// CSI_PARAM
// ---------------------------------------------------------------------------

TEST_CASE(states_csi_param_c0_executables_execute_and_stay)
{
    for (const int code : kExecutables) {
        Probe probe = feedFrom(ParserState::CSI_PARAM, cp(code));
        QTERMX_CHECK(probe.state() == ParserState::CSI_PARAM);
        expectEvents(probe.events(), {execEvent(code)});
    }
}

TEST_CASE(states_csi_param_del_is_ignored)
{
    Probe probe = feedFrom(ParserState::CSI_PARAM, U"\x7F");
    QTERMX_CHECK(probe.state() == ParserState::CSI_PARAM);
    QTERMX_CHECK(probe.events().empty());
}

TEST_CASE(states_csi_param_digits_accumulate)
{
    Probe probe = feedFrom(ParserState::CSI_PARAM, U"12");
    QTERMX_CHECK(probe.state() == ParserState::CSI_PARAM);
    QTERMX_CHECK((probe.paramsGroups() == std::vector<std::vector<int64_t>>{{12}}));
}

TEST_CASE(states_csi_param_params_separated_by_semicolons)
{
    Probe probe = feedFrom(ParserState::CSI_PARAM, U"1;2");
    QTERMX_CHECK((probe.paramsGroups() == std::vector<std::vector<int64_t>>{{1}, {2}}));
}

TEST_CASE(states_csi_param_finals_dispatch_with_params)
{
    Probe probe = feedFrom(ParserState::CSI_PARAM, U"10;20H");
    QTERMX_CHECK(probe.state() == ParserState::GROUND);
    expectEvents(probe.events(), {csiEvent("", "", Params{{10}, {20}}, "H")});
}

TEST_CASE(states_csi_param_second_private_prefix_is_malformed)
{
    Probe probe = feedFrom(ParserState::CSI_PARAM, U"?");
    QTERMX_CHECK(probe.state() == ParserState::CSI_IGNORE);
}

TEST_CASE(states_csi_param_malformed_sequence_swallowed_to_final)
{
    Probe probe = feedFrom(ParserState::CSI_PARAM, U"?>abcX");
    QTERMX_CHECK(probe.state() == ParserState::GROUND);
    expectEvents(probe.events(), {charsEvent(U"bcX")});
}

TEST_CASE(states_csi_param_intermediates_after_params_collect)
{
    Probe probe = feedFrom(ParserState::CSI_PARAM, U"5 q");
    QTERMX_CHECK(probe.state() == ParserState::GROUND);
    expectEvents(probe.events(), {csiEvent(" ", "", Params{{5}}, "q")});
}

// ---------------------------------------------------------------------------
// CSI_INTERMEDIATE
// ---------------------------------------------------------------------------

TEST_CASE(states_csi_intermediate_intermediates_collect)
{
    for (int code = 0x20; code < 0x30; ++code) {
        Probe probe = feedFrom(ParserState::CSI_INTERMEDIATE, cp(code));
        QTERMX_CHECK(probe.state() == ParserState::CSI_INTERMEDIATE);
        QTERMX_CHECK(probe.intermediates() == ascii(code));
    }
}

TEST_CASE(states_csi_intermediate_finals_dispatch_with_intermediates)
{
    Probe probe = feedFrom(ParserState::CSI_INTERMEDIATE, U" q");
    QTERMX_CHECK(probe.state() == ParserState::GROUND);
    expectEvents(probe.events(), {csiEvent(" ", "", Params{{0}}, "q")});
}

TEST_CASE(states_csi_intermediate_param_after_intermediate_is_malformed)
{
    Probe probe = feedFrom(ParserState::CSI_INTERMEDIATE, U"1");
    QTERMX_CHECK(probe.state() == ParserState::CSI_IGNORE);
}

TEST_CASE(states_csi_intermediate_del_falls_to_default)
{
    // Divergence: xterm.js ignores DEL and stays in CSI_INTERMEDIATE;
    // pyqtermx's table has no DEL rule here → default (IGNORE → GROUND).
    Probe probe = feedFrom(ParserState::CSI_INTERMEDIATE, U"\x7F");
    QTERMX_CHECK(probe.state() == ParserState::GROUND);
    QTERMX_CHECK(probe.events().empty());
}

TEST_CASE(states_csi_intermediate_c0_falls_to_default)
{
    // Divergence: xterm.js executes C0 here and stays; pyqtermx's table
    // has no C0 rules in CSI_INTERMEDIATE → default (IGNORE → GROUND).
    for (const int code : kExecutables) {
        Probe probe = feedFrom(ParserState::CSI_INTERMEDIATE, cp(code));
        QTERMX_CHECK(probe.state() == ParserState::GROUND);
        QTERMX_CHECK(probe.events().empty());
    }
}

// ---------------------------------------------------------------------------
// CSI_IGNORE
// ---------------------------------------------------------------------------

TEST_CASE(states_csi_ignore_prefinal_bytes_swallowed)
{
    for (int code = 0x20; code < 0x40; ++code) {
        Probe probe = feedFrom(ParserState::CSI_IGNORE, cp(code));
        QTERMX_CHECK(probe.state() == ParserState::CSI_IGNORE);
        QTERMX_CHECK(probe.events().empty());
    }
}

TEST_CASE(states_csi_ignore_del_swallowed)
{
    Probe probe = feedFrom(ParserState::CSI_IGNORE, U"\x7F");
    QTERMX_CHECK(probe.state() == ParserState::CSI_IGNORE);
    QTERMX_CHECK(probe.events().empty());
}

TEST_CASE(states_csi_ignore_final_resyncs_to_ground_without_dispatch)
{
    for (int code = 0x40; code < 0x7F; ++code) {
        Probe probe = feedFrom(ParserState::CSI_IGNORE, cp(code));
        QTERMX_CHECK(probe.state() == ParserState::GROUND);
        QTERMX_CHECK(probe.events().empty());
    }
}

TEST_CASE(states_csi_ignore_c0_falls_to_default)
{
    // Divergence: same as CSI_INTERMEDIATE — no C0-execute rules.
    for (const int code : kExecutables) {
        Probe probe = feedFrom(ParserState::CSI_IGNORE, cp(code));
        QTERMX_CHECK(probe.state() == ParserState::GROUND);
        QTERMX_CHECK(probe.events().empty());
    }
}

// ---------------------------------------------------------------------------
// OSC_STRING
// ---------------------------------------------------------------------------

TEST_CASE(states_osc_string_c0_controls_ignored)
{
    for (const int code : kExecutables) {
        if (code == 0x07) {
            continue; // BEL terminates the string instead (OSC_END)
        }
        Probe probe = feedFrom(ParserState::OSC_STRING, cp(code));
        QTERMX_CHECK(probe.state() == ParserState::OSC_STRING);
        QTERMX_CHECK(probe.osc().empty());
        QTERMX_CHECK(probe.events().empty());
    }
}

TEST_CASE(states_osc_string_printables_put)
{
    for (int code = 0x20; code < 0x7F; ++code) {
        Probe probe = feedFrom(ParserState::OSC_STRING, cp(code));
        QTERMX_CHECK(probe.state() == ParserState::OSC_STRING);
        QTERMX_CHECK(probe.osc() == cp(code));
    }
}

TEST_CASE(states_osc_string_del_is_payload)
{
    Probe probe = feedFrom(ParserState::OSC_STRING, U"\x7F");
    QTERMX_CHECK(probe.state() == ParserState::OSC_STRING);
    QTERMX_CHECK(probe.osc() == U"\x7F");
}

TEST_CASE(states_osc_string_unicode_is_payload)
{
    Probe probe = feedFrom(ParserState::OSC_STRING, U"€");
    QTERMX_CHECK(probe.state() == ParserState::OSC_STRING);
    QTERMX_CHECK(probe.osc() == U"€");
}

TEST_CASE(states_osc_string_bel_ends_and_dispatches)
{
    Probe probe = feedFrom(ParserState::OSC_STRING, U"0;title\x07");
    QTERMX_CHECK(probe.state() == ParserState::GROUND);
    expectEvents(probe.events(), {oscEvent(U"0;title")});
}

TEST_CASE(states_osc_string_8bit_st_ends_and_dispatches)
{
    Probe probe = feedFrom(ParserState::OSC_STRING, U"0;title\x9C");
    QTERMX_CHECK(probe.state() == ParserState::GROUND);
    expectEvents(probe.events(), {oscEvent(U"0;title")});
}

TEST_CASE(states_osc_string_esc_ends_and_moves_to_escape)
{
    Probe probe = feedFrom(ParserState::OSC_STRING, U"0;title\x1B");
    QTERMX_CHECK(probe.state() == ParserState::ESCAPE);
    expectEvents(probe.events(), {oscEvent(U"0;title")});
}

TEST_CASE(states_osc_string_can_aborts_without_dispatch)
{
    Probe probe = feedFrom(ParserState::OSC_STRING, U"0;title\x18");
    QTERMX_CHECK(probe.state() == ParserState::GROUND);
    QTERMX_CHECK(probe.events().empty());
}

TEST_CASE(states_osc_string_sub_aborts_without_dispatch)
{
    Probe probe = feedFrom(ParserState::OSC_STRING, U"0;title\x1A");
    QTERMX_CHECK(probe.state() == ParserState::GROUND);
    QTERMX_CHECK(probe.events().empty());
}

TEST_CASE(states_osc_string_empty_osc_dispatches_empty)
{
    // Divergence: xterm.js suppresses the call for an empty payload;
    // pyqtermx's OSC_END always dispatches, even with nothing collected.
    expectEvents(feed(U"\x9D\x9C"), {oscEvent(U"")});
}

// ---------------------------------------------------------------------------
// DCS_* (pyqtermx never dispatches DCS)
// ---------------------------------------------------------------------------

TEST_CASE(states_dcs_entry_c0_controls_ignored)
{
    for (const int code : kExecutables) {
        Probe probe = feedFrom(ParserState::DCS_ENTRY, cp(code));
        QTERMX_CHECK(probe.state() == ParserState::DCS_ENTRY);
        QTERMX_CHECK(probe.events().empty());
    }
}

TEST_CASE(states_dcs_entry_del_ignored)
{
    Probe probe = feedFrom(ParserState::DCS_ENTRY, U"\x7F");
    QTERMX_CHECK(probe.state() == ParserState::DCS_ENTRY);
    QTERMX_CHECK(probe.events().empty());
}

TEST_CASE(states_dcs_entry_digits_go_to_param)
{
    for (int digit = 0; digit < 10; ++digit) {
        Probe probe = feedFrom(ParserState::DCS_ENTRY, cp(0x30 + digit));
        QTERMX_CHECK(probe.state() == ParserState::DCS_PARAM);
        QTERMX_CHECK(probe.events().empty());
    }
}

TEST_CASE(states_dcs_entry_intermediates_collect_to_dcs_intermediate)
{
    for (int code = 0x20; code < 0x30; ++code) {
        Probe probe = feedFrom(ParserState::DCS_ENTRY, cp(code));
        QTERMX_CHECK(probe.state() == ParserState::DCS_INTERMEDIATE);
        QTERMX_CHECK(probe.intermediates() == ascii(code));
    }
}

TEST_CASE(states_dcs_entry_final_enters_ignore)
{
    // Divergence: xterm.js hooks DCS here; pyqtermx swallows it.
    for (int code = 0x40; code < 0x7F; ++code) {
        Probe probe = feedFrom(ParserState::DCS_ENTRY, cp(code));
        QTERMX_CHECK(probe.state() == ParserState::DCS_IGNORE);
        QTERMX_CHECK(probe.events().empty());
    }
}

TEST_CASE(states_dcs_param_c0_controls_ignored)
{
    for (const int code : kExecutables) {
        Probe probe = feedFrom(ParserState::DCS_PARAM, cp(code));
        QTERMX_CHECK(probe.state() == ParserState::DCS_PARAM);
        QTERMX_CHECK(probe.events().empty());
    }
}

TEST_CASE(states_dcs_param_del_ignored)
{
    Probe probe = feedFrom(ParserState::DCS_PARAM, U"\x7F");
    QTERMX_CHECK(probe.state() == ParserState::DCS_PARAM);
    QTERMX_CHECK(probe.events().empty());
}

TEST_CASE(states_dcs_param_params_swallowed_but_state_advances)
{
    Probe probe = feedFrom(ParserState::DCS_PARAM, U"1;2");
    QTERMX_CHECK(probe.state() == ParserState::DCS_PARAM);
    QTERMX_CHECK(probe.events().empty());
}

TEST_CASE(states_dcs_param_private_prefix_stays_in_param)
{
    // Divergence: xterm.js sends a second prefix byte to DCS_IGNORE;
    // pyqtermx's DCS_PARAM treats all 0x30–0x3F identically.
    for (int code = 0x3C; code < 0x40; ++code) {
        Probe probe = feedFrom(ParserState::DCS_PARAM, cp(code));
        QTERMX_CHECK(probe.state() == ParserState::DCS_PARAM);
        QTERMX_CHECK(probe.events().empty());
    }
}

TEST_CASE(states_dcs_param_intermediates_collect_to_dcs_intermediate)
{
    Probe probe = feedFrom(ParserState::DCS_PARAM, U" q");
    QTERMX_CHECK(probe.state() == ParserState::DCS_IGNORE);
    QTERMX_CHECK(probe.intermediates() == " ");
    QTERMX_CHECK(probe.events().empty());
}

TEST_CASE(states_dcs_param_final_enters_ignore)
{
    Probe probe = feedFrom(ParserState::DCS_PARAM, U"1;2a");
    QTERMX_CHECK(probe.state() == ParserState::DCS_IGNORE);
    QTERMX_CHECK(probe.events().empty());
}

TEST_CASE(states_dcs_intermediate_intermediates_collect)
{
    for (int code = 0x20; code < 0x30; ++code) {
        Probe probe = feedFrom(ParserState::DCS_INTERMEDIATE, cp(code));
        QTERMX_CHECK(probe.state() == ParserState::DCS_INTERMEDIATE);
        QTERMX_CHECK(probe.intermediates() == ascii(code));
    }
}

TEST_CASE(states_dcs_intermediate_param_after_intermediate_is_malformed)
{
    Probe probe = feedFrom(ParserState::DCS_INTERMEDIATE, U"1");
    QTERMX_CHECK(probe.state() == ParserState::DCS_IGNORE);
}

TEST_CASE(states_dcs_intermediate_final_enters_ignore)
{
    Probe probe = feedFrom(ParserState::DCS_INTERMEDIATE, U"+a");
    QTERMX_CHECK(probe.state() == ParserState::DCS_IGNORE);
    QTERMX_CHECK(probe.intermediates() == "+");
    QTERMX_CHECK(probe.events().empty());
}

TEST_CASE(states_dcs_intermediate_del_ignored)
{
    Probe probe = feedFrom(ParserState::DCS_INTERMEDIATE, U"\x7F");
    QTERMX_CHECK(probe.state() == ParserState::DCS_INTERMEDIATE);
    QTERMX_CHECK(probe.events().empty());
}

TEST_CASE(states_dcs_ignore_payload_consumed_until_st)
{
    std::vector<int> codes = kExecutables;
    for (int i = 0x20; i < 0x80; ++i) {
        codes.push_back(i);
    }
    codes.push_back(0x7F);
    codes.push_back(0x20AC);
    for (const int code : codes) {
        Probe probe = feedFrom(ParserState::DCS_IGNORE, cp(code));
        QTERMX_CHECK(probe.state() == ParserState::DCS_IGNORE);
        QTERMX_CHECK(probe.events().empty());
    }
}

TEST_CASE(states_dcs_ignore_8bit_st_returns_to_ground)
{
    Probe probe = feedFrom(ParserState::DCS_IGNORE, U"payload\x9C");
    QTERMX_CHECK(probe.state() == ParserState::GROUND);
    QTERMX_CHECK(probe.events().empty());
}

// ---------------------------------------------------------------------------
// SOS / PM
// ---------------------------------------------------------------------------

TEST_CASE(states_sos_pm_string_everything_ignored_until_st)
{
    std::vector<int> codes = kExecutables;
    for (int i = 0x20; i < 0x80; ++i) {
        codes.push_back(i);
    }
    codes.push_back(0x7F);
    for (const int code : codes) {
        Probe probe = feedFrom(ParserState::SOS_PM_STRING, cp(code));
        QTERMX_CHECK(probe.state() == ParserState::SOS_PM_STRING);
        QTERMX_CHECK(probe.events().empty());
    }
}

TEST_CASE(states_sos_pm_string_8bit_st_returns_to_ground)
{
    Probe probe = feedFrom(ParserState::SOS_PM_STRING, U"payload\x9C");
    QTERMX_CHECK(probe.state() == ParserState::GROUND);
    QTERMX_CHECK(probe.events().empty());
}

// ---------------------------------------------------------------------------
// APC_ENTRY (pyqtermx has no passthrough)
// ---------------------------------------------------------------------------

TEST_CASE(states_apc_entry_everything_ignored_until_terminator)
{
    std::vector<int> codes = kExecutables;
    for (int i = 0x20; i < 0x80; ++i) {
        codes.push_back(i);
    }
    codes.push_back(0x7F);
    for (const int code : codes) {
        Probe probe = feedFrom(ParserState::APC_ENTRY, cp(code));
        QTERMX_CHECK(probe.state() == ParserState::APC_ENTRY);
        QTERMX_CHECK(probe.events().empty());
    }
}

TEST_CASE(states_apc_entry_can_sub_end_without_executing)
{
    // Divergence: xterm.js's global CAN/SUB executes here; pyqtermx ends
    // the string with plain IGNORE (no execute, no restart).
    for (const int code : {0x18, 0x1A}) {
        Probe probe = feedFrom(ParserState::APC_ENTRY, cp(code));
        QTERMX_CHECK(probe.state() == ParserState::GROUND);
        QTERMX_CHECK(probe.events().empty());
    }
}

TEST_CASE(states_apc_entry_esc_ends_without_restart)
{
    Probe probe = feedFrom(ParserState::APC_ENTRY, U"\x1B");
    QTERMX_CHECK(probe.state() == ParserState::GROUND);
    QTERMX_CHECK(probe.events().empty());
}

TEST_CASE(states_apc_entry_8bit_st_ends)
{
    Probe probe = feedFrom(ParserState::APC_ENTRY, U"payload\x9C");
    QTERMX_CHECK(probe.state() == ParserState::GROUND);
    QTERMX_CHECK(probe.events().empty());
}

// ---------------------------------------------------------------------------
// End-to-end examples
// ---------------------------------------------------------------------------

TEST_CASE(states_examples_csi_with_print_and_execute)
{
    expectEvents(feed(U"\x1B[<31;5mHello World! öäü€\nabc"),
                 {csiEvent("", "<", Params{{31}, {5}}, "m"),
                  charsEvent(U"Hello World! öäü€"), execEvent(10), charsEvent(U"abc")});
}

TEST_CASE(states_examples_osc)
{
    expectEvents(feed(U"\x1B]0;abc123€öäü\x07"), {oscEvent(U"0;abc123€öäü")});
}

TEST_CASE(states_examples_single_dcs_ignored)
{
    expectEvents(feed(U"\x1BP1;2;3+$aäbc;däe\x9C"), {});
}

TEST_CASE(states_examples_print_plus_dcs_c1_plus_print)
{
    expectEvents(feed(U"abc\x90" U"1;2;3+$abc;de\x9C"), {charsEvent(U"abc")});
}

TEST_CASE(states_examples_print_plus_pm_c1_plus_print)
{
    expectEvents(feed(U"abc\x98" U"123tzf\x9C" U"defg"),
                 {charsEvent(U"abc"), charsEvent(U"defg")});
}

TEST_CASE(states_examples_print_plus_osc_c1_plus_print)
{
    expectEvents(feed(U"abc\x9D" U"123;tzf\x9C" U"defg"),
                 {charsEvent(U"abc"), oscEvent(U"123;tzf"), charsEvent(U"defg")});
}

TEST_CASE(states_examples_single_apc_ignored)
{
    expectEvents(feed(U"\x1B_X3+$aäbc;däe\x9C"), {});
}

TEST_CASE(states_examples_print_plus_apc_c1_plus_print)
{
    expectEvents(feed(U"abc\x9F" U"Abc;de\x9Cxyz"),
                 {charsEvent(U"abc"), charsEvent(U"xyz")});
}

TEST_CASE(states_examples_print_plus_apc_c0_plus_print)
{
    // Divergence: pyqtermx ends the APC at ESC with plain IGNORE, leaving
    // the '\' of the two-byte ST to print in GROUND (xterm.js resumes at
    // ESCAPE, where the '\' is swallowed).
    expectEvents(feed(U"abc\x1B_Abc;de\x1B\\xyz"),
                 {charsEvent(U"abc"), charsEvent(U"\\xyz")});
}

TEST_CASE(states_examples_error_recovery)
{
    expectEvents(feed(U"\x1B[1€abcdefg\x9B<;c"),
                 {charsEvent(U"abcdefg"), csiEvent("", "<", Params{{0}, {0}}, "c")});
}

TEST_CASE(states_examples_7bit_st_after_osc)
{
    // Divergence: the trailing '\' of the two-byte ST dispatches as a
    // no-op escape sequence instead of being swallowed.
    expectEvents(feed(U"abc\x9D" U"123;tzf\x1B\\defg"),
                 {charsEvent(U"abc"), oscEvent(U"123;tzf"), escEvent("", "\\"),
                  charsEvent(U"defg")});
}

TEST_CASE(states_examples_colon_notation_in_csi_params)
{
    expectEvents(feed(U"\x1B[<31;5::123:;8mHello World! öäü€\nabc"),
                 {csiEvent("", "<", Params{{31}, {5, -1, 123, -1}, {8}}, "m"),
                  charsEvent(U"Hello World! öäü€"), execEvent(10), charsEvent(U"abc")});
}

TEST_CASE(states_examples_can_aborts_osc_without_dispatch)
{
    expectEvents(feed(U"\x1B]0;abc123€öäü\x18"), {});
}

TEST_CASE(states_examples_sub_aborts_osc_without_dispatch)
{
    expectEvents(feed(U"\x1B]0;abc123€öäü\x1A"), {});
}

// ---------------------------------------------------------------------------
// Coverage
// ---------------------------------------------------------------------------

TEST_CASE(states_coverage_unicode_in_csi_ignore_falls_to_default)
{
    // Divergence: xterm.js ignores and stays in CSI_IGNORE; pyqtermx's
    // default resyncs to GROUND.
    Probe probe = feedFrom(ParserState::CSI_IGNORE, U"€");
    QTERMX_CHECK(probe.state() == ParserState::GROUND);
    QTERMX_CHECK(probe.events().empty());
}

TEST_CASE(states_coverage_unicode_in_dcs_ignore_is_consumed)
{
    Probe probe = feedFrom(ParserState::DCS_IGNORE, U"€öäü");
    QTERMX_CHECK(probe.state() == ParserState::DCS_IGNORE);
    QTERMX_CHECK(probe.events().empty());
}

TEST_CASE(states_coverage_unicode_in_escape_falls_to_default)
{
    Probe probe = feedFrom(ParserState::ESCAPE, U"€");
    QTERMX_CHECK(probe.state() == ParserState::GROUND);
    QTERMX_CHECK(probe.events().empty());
}

TEST_CASE(states_coverage_8bit_st_in_ground_is_swallowed)
{
    Probe probe = feedFrom(ParserState::GROUND, U"\x9C");
    QTERMX_CHECK(probe.state() == ParserState::GROUND);
    QTERMX_CHECK(probe.events().empty());
}