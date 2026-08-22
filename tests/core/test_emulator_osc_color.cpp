// T20 — OSC 4/10/11/12 color queries + DA1/DSR/DECRPM dialogue — the
// terminal answers the child's theme detection queries and applies its
// cursor color (Phase 5) (port of tests/emulator/test_osc_color.py,
// extended with the query-reply tests the ROADMAP specifies).
//
// TUI apps (opencode, vim, …) query the palette (`OSC 4`), the default
// foreground (`OSC 10`), and the default background (`OSC 11`) to
// decide their light/dark theme; the emulator replies from palette.h
// with the xterm 16-bit `rgb:RRRR/GGGG/BBBB` form. Set forms
// parse-and-ignore (palette mutation is a follow-up); `OSC 12` (cursor
// color) and `OSC 112` (reset) apply — the cursor color is visible
// state. DA1/DSR/DECRPM answer the terminfo-driven apps' queries.
#include "harness.h"
#include "emulator.h"
#include "parser.h"
#include "screen.h"

#include <string>
#include <vector>

using namespace qtermx;
using namespace qtermx::test;

namespace {

// A parser whose emulator appends every reply to `replies`.
struct ReplyHarness {
    Screen screen;
    Emulator emulator;
    Parser parser;
    std::vector<std::string> replies;

    ReplyHarness()
        : emulator(screen, [this](const std::string& r) { replies.push_back(r); })
        , parser(&emulator)
    {
    }

    // Feed bytes in pty-sized chunks — the chunking invariant (T6):
    // byte-wise feeding must equal one big feed, replies included.
    void feed(const std::string& data)
    {
        for (const char byte : data) {
            parser.feedBytes(std::string(1, byte));
        }
        parser.flush();
    }
};

} // namespace

// -- OSC 4 — palette queries --------------------------------------------

TEST_CASE(osc_4_query_all_16_colors)
{
    ReplyHarness h;
    h.feed("\x1b]4;?\x07");
    QTERMX_CHECK(h.replies.size() == 1);
    const std::string& reply = h.replies[0];
    QTERMX_CHECK(reply.rfind("\x1b]4;", 0) == 0);
    QTERMX_CHECK(reply.back() == '\x07');
    // "4", "0", "rgb:…", … — 1 + 16*2 entries.
    std::vector<std::string> entries;
    size_t start = 2; // skip "\x1b]"
    while (true) {
        const size_t semi = reply.find(';', start);
        if (semi == std::string::npos) {
            entries.push_back(reply.substr(start, reply.size() - 1 - start));
            break;
        }
        entries.push_back(reply.substr(start, semi - start));
        start = semi + 1;
    }
    QTERMX_CHECK(entries[0] == "4");
    QTERMX_CHECK(entries.size() == 1 + 16 * 2);
    QTERMX_CHECK(entries[1] == "0" && entries[2] == "rgb:0000/0000/0000"); // ANSI black
    QTERMX_CHECK(entries[3] == "1" && entries[4] == "rgb:cdcd/0000/0000"); // ANSI red
    QTERMX_CHECK(entries[15] == "7" && entries[16] == "rgb:e5e5/e5e5/e5e5"); // ANSI white
    QTERMX_CHECK(entries[31] == "15" && entries[32] == "rgb:ffff/ffff/ffff"); // bright white
}

TEST_CASE(osc_4_query_single_index)
{
    ReplyHarness h;
    h.feed("\x1b]4;1;?\x07");
    QTERMX_CHECK(h.replies.size() == 1);
    QTERMX_CHECK(h.replies[0] == "\x1b]4;1;rgb:cdcd/0000/0000\x07");
}

TEST_CASE(osc_4_query_multiple_indices)
{
    ReplyHarness h;
    h.feed("\x1b]4;1;?;2;?\x07");
    QTERMX_CHECK(h.replies.size() == 1);
    QTERMX_CHECK(h.replies[0] == "\x1b]4;1;rgb:cdcd/0000/0000;2;rgb:0000/cdcd/0000\x07");
}

TEST_CASE(osc_4_query_grouped_indices)
{
    // `4;0;1;?` — indices grouped before a single `?` (a form some
    // apps send) — replies both.
    ReplyHarness h;
    h.feed("\x1b]4;0;1;?\x07");
    QTERMX_CHECK(h.replies.size() == 1);
    QTERMX_CHECK(h.replies[0] == "\x1b]4;0;rgb:0000/0000/0000;1;rgb:cdcd/0000/0000\x07");
}

TEST_CASE(osc_4_query_256_color_indices)
{
    ReplyHarness h;
    h.feed("\x1b]4;196;?;232;?\x07");
    // 196 → cube r=255; 232 → grayscale 8.
    QTERMX_CHECK(h.replies.size() == 1);
    QTERMX_CHECK(h.replies[0] == "\x1b]4;196;rgb:ffff/0000/0000;232;rgb:0808/0808/0808\x07");
}

TEST_CASE(osc_4_set_form_is_ignored)
{
    ReplyHarness h;
    h.feed("\x1b]4;1;#ff0000\x07\x1b]4;2;rgb:0000/ff00/0000\x07");
    QTERMX_CHECK(h.replies.empty());
}

TEST_CASE(osc_4_out_of_range_index_is_skipped)
{
    ReplyHarness h;
    h.feed("\x1b]4;300;?;1;?\x07");
    QTERMX_CHECK(h.replies.size() == 1);
    QTERMX_CHECK(h.replies[0] == "\x1b]4;1;rgb:cdcd/0000/0000\x07");
}

// -- OSC 10/11 — fg/bg queries -------------------------------------------

TEST_CASE(osc_10_and_11_query_defaults)
{
    ReplyHarness h;
    h.feed("\x1b]10;?\x07\x1b]11;?\x07");
    QTERMX_CHECK(h.replies.size() == 2);
    QTERMX_CHECK(h.replies[0] == "\x1b]10;rgb:e8e8/e8e8/e8e8\x07");
    QTERMX_CHECK(h.replies[1] == "\x1b]11;rgb:1010/1010/1010\x07");
}

TEST_CASE(osc_10_and_11_set_forms_are_ignored)
{
    ReplyHarness h;
    h.feed("\x1b]10;#ffffff\x07\x1b]11;rgb:ffff/ffff/ffff\x07");
    QTERMX_CHECK(h.replies.empty());
}

TEST_CASE(set_palette_updates_queries)
{
    ReplyHarness h;
    h.emulator.setPalette("#ffffff", "#000000");
    h.feed("\x1b]10;?\x07\x1b]11;?\x07");
    QTERMX_CHECK(h.replies.size() == 2);
    QTERMX_CHECK(h.replies[0] == "\x1b]10;rgb:ffff/ffff/ffff\x07");
    QTERMX_CHECK(h.replies[1] == "\x1b]11;rgb:0000/0000/0000\x07");
}

// -- OSC 12 — cursor color ---------------------------------------------

TEST_CASE(osc_12_set_form_sets_cursor_color)
{
    ReplyHarness h;
    h.feed("\x1b]12;#1a1a1a\x07");
    QTERMX_CHECK(h.emulator.cursorColor() == std::optional<std::string>("#1a1a1a"));
}

TEST_CASE(osc_12_rgb_form_sets_cursor_color)
{
    ReplyHarness h;
    h.feed("\x1b]12;rgb:1a1a/1a1a/1a1a\x07");
    QTERMX_CHECK(h.emulator.cursorColor() == std::optional<std::string>("#1a1a1a"));
}

TEST_CASE(osc_12_query_replies_the_set_color)
{
    ReplyHarness h;
    h.feed("\x1b]12;#1a1a1a\x07\x1b]12;?\x07");
    QTERMX_CHECK(h.replies.size() == 1);
    QTERMX_CHECK(h.replies[0] == "\x1b]12;rgb:1a1a/1a1a/1a1a\x07");
}

TEST_CASE(osc_12_query_before_set_is_silent)
{
    ReplyHarness h;
    h.feed("\x1b]12;?\x07");
    QTERMX_CHECK(h.replies.empty());
}

TEST_CASE(osc_12_malformed_spec_is_ignored)
{
    ReplyHarness h;
    h.feed("\x1b]12;#12345\x07\x1b]12;rgb:zzzz/0000/0000\x07");
    QTERMX_CHECK(!h.emulator.cursorColor().has_value());
}

TEST_CASE(osc_12_rgb_form_wrong_arity_is_ignored)
{
    ReplyHarness h;
    h.feed("\x1b]12;rgb:ffff/ffff\x07");
    QTERMX_CHECK(!h.emulator.cursorColor().has_value());
}

TEST_CASE(osc_112_resets_cursor_color)
{
    ReplyHarness h;
    h.feed("\x1b]12;#1a1a1a\x07\x1b]112\x07\x1b]12;?\x07");
    QTERMX_CHECK(!h.emulator.cursorColor().has_value());
    QTERMX_CHECK(h.replies.empty());
}

// -- Terminators & robustness --------------------------------------------

TEST_CASE(st_terminator_dispatches_same_reply)
{
    // The ECMA-48 form (ESC \) terminates OSC just like BEL (the
    // parser strips both; the reply always uses BEL, xterm style).
    ReplyHarness h;
    h.feed("\x1b]11;?\x1b\\");
    QTERMX_CHECK(h.replies.size() == 1);
    QTERMX_CHECK(h.replies[0] == "\x1b]11;rgb:1010/1010/1010\x07");
}

TEST_CASE(unknown_osc_is_a_noop)
{
    // Title, clipboard, hyperlink set forms parse-and-ignore until
    // their step.
    ReplyHarness h;
    h.feed("\x1b]0;title\x07\x1b]52;c;AQID\x07\x1b]8;;uri\x07");
    QTERMX_CHECK(h.replies.empty());
}

TEST_CASE(query_without_reply_callback_is_safe)
{
    // No reply callback — queries silently drop; nothing crashes.
    Screen screen;
    Emulator emulator(screen);
    Parser parser(&emulator);
    parser.feedBytes("\x1b]4;?;1;?\x07\x1b]11;?\x07");
    parser.flush();
}

// -- DA1 / DSR / DECRPM (the ROADMAP's query-reply spec) -----------------

TEST_CASE(da1_replies_vt100_with_advanced_video)
{
    // DA1: `ESC [ c` → `ESC [ ? 1 ; 2 c` — terminfo-driven apps hang
    // without this.
    ReplyHarness h;
    h.feed("\x1b[c");
    QTERMX_CHECK(h.replies.size() == 1);
    QTERMX_CHECK(h.replies[0] == "\x1b[?1;2c");
}

TEST_CASE(dsr_cursor_position_replies_row_col)
{
    // DSR 6: `ESC [ 6n` → `ESC [ row ; col R` (1-based).
    ReplyHarness h;
    h.feed("ab\x1b[6n");
    QTERMX_CHECK(h.replies.size() == 1);
    QTERMX_CHECK(h.replies[0] == "\x1b[1;3R");
    h.feed("\x1b[3;5H\x1b[6n");
    QTERMX_CHECK(h.replies.size() == 2);
    QTERMX_CHECK(h.replies[1] == "\x1b[3;5R");
}

TEST_CASE(dsr_other_codes_are_noops)
{
    ReplyHarness h;
    h.feed("\x1b[5n\x1b[0n");
    QTERMX_CHECK(h.replies.empty());
}

TEST_CASE(decrpm_reports_mode_state)
{
    // DECRPM: `ESC [ ? Ps $ p` → `ESC [ ? Ps ; value $ y` — 1 when
    // set, 2 when reset.
    ReplyHarness h;
    h.feed("\x1b[?25h\x1b[?25$p"); // DECTCEM on
    QTERMX_CHECK(h.replies.size() == 1);
    QTERMX_CHECK(h.replies[0] == "\x1b[?25;1$y");
    h.feed("\x1b[?25l\x1b[?25$p"); // DECTCEM off
    QTERMX_CHECK(h.replies.size() == 2);
    QTERMX_CHECK(h.replies[1] == "\x1b[?25;2$y");
    h.feed("\x1b[?999$p"); // unknown mode — reset (the default)
    QTERMX_CHECK(h.replies.size() == 3);
    QTERMX_CHECK(h.replies[2] == "\x1b[?999;2$y");
}