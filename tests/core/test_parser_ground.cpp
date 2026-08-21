// T1 — Ground dispatch: printable text and C0 controls flow to the
// handler (port of tests/parser/test_ground.py).
#include "harness.h"
#include "test_recorder.h"

using namespace qtermx;
using namespace qtermx::test;

TEST_CASE(ground_printable_run_is_one_text_event)
{
    expectEvents(feed(U"hello"), {charsEvent(U"hello")});
}

TEST_CASE(ground_printable_run_accumulates_across_feeds)
{
    Recorder recorder;
    Parser parser(&recorder);
    parser.feed(U"he");
    parser.feed(U"llo");
    parser.flush();
    expectEvents(recorder.events, {charsEvent(U"hello")});
}

TEST_CASE(ground_control_breaks_printable_run)
{
    expectEvents(feed(U"a\nb"), {charsEvent(U"a"), execEvent(10), charsEvent(U"b")});
}

TEST_CASE(ground_all_c0_controls_execute)
{
    // 0x00–0x1F except ESC (0x1B), which starts an escape sequence.
    for (int code = 0x00; code < 0x1B; ++code) {
        expectEvents(feed(std::u32string(1, static_cast<char32_t>(code))),
                     {execEvent(code)});
    }
    for (int code = 0x1C; code < 0x20; ++code) {
        expectEvents(feed(std::u32string(1, static_cast<char32_t>(code))),
                     {execEvent(code)});
    }
}

TEST_CASE(ground_del_is_ignored_in_ground)
{
    expectEvents(feed(U"a\x7F" "b"), {charsEvent(U"a"), charsEvent(U"b")});
}

TEST_CASE(ground_utf8_multibyte_character_is_one_text_event)
{
    expectEvents(feed(U"héllo"), {charsEvent(U"héllo")});
}

TEST_CASE(ground_feed_bytes_decodes_incrementally)
{
    Recorder recorder;
    Parser parser(&recorder);
    parser.feedBytes("\xc3"); // first byte of é
    parser.feedBytes("\xa9");
    parser.flush();
    expectEvents(recorder.events, {charsEvent(U"é")});
}

TEST_CASE(ground_lone_continuation_byte_becomes_replacement_char)
{
    expectEvents(feedBytes("\x9b"), {charsEvent(U"\uFFFD")});
}