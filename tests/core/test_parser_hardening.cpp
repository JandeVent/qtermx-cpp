// T6 — Hardening: streaming invariance and the no-crash guarantee (port
// of tests/parser/test_hardening.py).
//
// Two load-bearing properties, exhaustive:
// 1. Byte-split invariance: every corpus input fed whole and fed in
//    *every* possible chunking (byte splits can land mid-UTF-8, mid-CSI,
//    mid-OSC, ...) produces an identical event sequence.
// 2. No-crash guarantee: arbitrary bytes never raise, and after garbage
//    the parser recovers to ground and parses clean input correctly.
#include <random>

#include "harness.h"
#include "test_recorder.h"

using namespace qtermx;
using namespace qtermx::test;

namespace {

// Corpus exercising every state family plus awkward boundaries.
const std::vector<std::string> kCorpus = {
    "",
    "hello",
    "\x1b",
    "\x1b[",
    "\x1b[31mred\x1b[0m",
    "\x1b[1;2;3:4:5m",
    "\x1b[>1;2q",
    "\x1b[?25l",
    "\x1b[9999999999m", // param overflow
    "\x1b[:::::::::m",  // subparam overflow
    "\x1b]0;title\x07",
    "\x1b]0;title\x1b\\",
    "\x1b]",
    "\x1bP1;2|payload\x1b\\",
    "\x90payload\x9c",
    "\x1bXpayload\x1b\\",
    "\x1b^pm\x9c",
    "\x9e\x9c",
    "\x1b_payload\x9c",
    "\x9f\x9c",
    "\x1b(0\x1b(B",
    "\x1b#8",
    "\x1b" "7\x1b" "8\x1b=",
    "\x1b[31;44",
    "caf\xc3\xa9",     // é
    "\xf0\x9f\x98\x80", // 😀
    "\xf0\x9f",         // truncated 4-byte UTF-8
    "\x1b[\xc3",        // truncated UTF-8 inside CSI
    "\x1b]8;;https://x.example/\x07link",
    "\x1bP|" + std::string("\x20\x21\x22\x23\x24\x25\x26\x27\x28\x29\x2a\x2b\x2c\x2d\x2e\x2f"
                           "\x30\x31\x32\x33\x34\x35\x36\x37\x38\x39\x3a\x3b\x3c\x3d\x3e\x3f"
                           "\x40\x41\x42\x43\x44\x45\x46\x47\x48\x49\x4a\x4b\x4c\x4d\x4e\x4f"
                           "\x50\x51\x52\x53\x54\x55\x56\x57\x58\x59\x5a\x5b\x5c\x5d\x5e\x5f"
                           "\x60\x61\x62\x63\x64\x65\x66\x67\x68\x69\x6a\x6b\x6c\x6d\x6e\x6f"
                           "\x70\x71\x72\x73\x74\x75\x76\x77\x78\x79\x7a\x7b\x7c\x7d\x7e\x7f") +
        "\x1b\\",
    "\x1b[31m\x1b[2J\x1b[H\x1b(B\x1b]0;t\x07" "caf\xc3\xa9",
};

// Clean stream with UTF-8 + BEL.
const std::string kGood = "\x1b[31mHello \xf0\x9f\x91\x8d\x07";

std::vector<Event> run(const std::string& data, const std::vector<int>& chunkSizes)
{
    Recorder recorder;
    Parser parser(&recorder);
    size_t offset = 0;
    for (const int size : chunkSizes) {
        parser.feedBytes(data.substr(offset, size));
        offset += size;
    }
    parser.flush();
    return recorder.events;
}

std::vector<Event> whole(const std::string& data)
{
    return run(data, {static_cast<int>(data.size())});
}

// Sample `maxCuts` distinct cut positions in (0, len); returns the
// resulting chunk sizes (shared by allChunkings and the random test).
std::vector<int> randomChunkSizes(std::mt19937& rng, size_t len, size_t maxCuts)
{
    std::vector<size_t> cuts;
    const size_t nCuts = std::min(maxCuts, len - 1);
    std::uniform_int_distribution<size_t> dist(1, len - 1);
    while (cuts.size() < nCuts) {
        const size_t cut = dist(rng);
        if (std::find(cuts.begin(), cuts.end(), cut) == cuts.end()) {
            cuts.push_back(cut);
        }
    }
    std::sort(cuts.begin(), cuts.end());
    std::vector<int> sizes;
    size_t start = 0;
    for (const size_t cut : cuts) {
        sizes.push_back(static_cast<int>(cut - start));
        start = cut;
    }
    sizes.push_back(static_cast<int>(len - start));
    return sizes;
}

// Every composition of `data` into contiguous non-empty chunks; long
// inputs sample fixed + random cuts instead of 2^(n-1) chunkings.
std::vector<std::vector<int>> allChunkings(const std::string& data, std::mt19937& rng)
{
    std::vector<std::vector<int>> result;
    if (data.size() <= 12) {
        // 2^(n-1) chunkings; the empty input has exactly one (Python's
        // max(len - 1, 0) — size_t arithmetic would wrap to SIZE_MAX).
        const size_t nChunkings = data.empty() ? 1 : (size_t{1} << (data.size() - 1));
        for (size_t mask = 0; mask < nChunkings; ++mask) {
            std::vector<int> chunks;
            size_t start = 0;
            for (size_t i = 0; i + 1 < data.size(); ++i) {
                if ((mask >> i) & 1) {
                    chunks.push_back(static_cast<int>(i + 1 - start));
                    start = i + 1;
                }
            }
            chunks.push_back(static_cast<int>(data.size() - start));
            result.push_back(chunks);
        }
        return result;
    }
    // Long inputs: sample fixed + random cuts.
    return {{static_cast<int>(data.size())}, randomChunkSizes(rng, data.size(), 8)};
}

} // namespace

TEST_CASE(hardening_whole_and_single_bytes_agree)
{
    for (const auto& data : kCorpus) {
        const std::vector<Event> expected = whole(data);
        std::vector<int> single(data.size(), 1);
        if (single.empty()) {
            single = {0};
        }
        const std::vector<Event> actual = run(data, single);
        if (actual != expected) {
            throw CheckFailed{"chunk mismatch for " + data + "\n       expected " +
                              eventsToString(expected) + "\n       got      " +
                              eventsToString(actual)};
        }
    }
}

TEST_CASE(hardening_byte_split_invariance)
{
    std::mt19937 rng(0x20260801);
    for (const auto& data : kCorpus) {
        const std::vector<Event> expected = whole(data);
        for (const auto& chunking : allChunkings(data, rng)) {
            const std::vector<Event> actual = run(data, chunking);
            if (actual != expected) {
                throw CheckFailed{"chunking mismatch for " + data + "\n       expected " +
                                  eventsToString(expected) + "\n       got      " +
                                  eventsToString(actual)};
            }
        }
    }
}

TEST_CASE(hardening_random_corpus_chunked_vs_whole)
{
    std::mt19937 rng(20260801);
    std::uniform_int_distribution<int> byteDist(0, 0xFF);
    std::uniform_int_distribution<int> lenDist(1, 299);
    for (int i = 0; i < 25; ++i) {
        std::string data;
        const int len = lenDist(rng);
        for (int j = 0; j < len; ++j) {
            data.push_back(static_cast<char>(byteDist(rng)));
        }
        const std::vector<Event> expected = whole(data);
        const std::vector<int> sizes = randomChunkSizes(rng, data.size(), 6);
        const std::vector<Event> actual = run(data, sizes);
        if (actual != expected) {
            throw CheckFailed{"random chunking mismatch\n       expected " +
                              eventsToString(expected) + "\n       got      " +
                              eventsToString(actual)};
        }
    }
}

TEST_CASE(hardening_fuzz_never_raises)
{
    std::mt19937 rng(0xDECAFBAD);
    std::uniform_int_distribution<int> byteDist(0, 0xFF);
    std::uniform_int_distribution<int> lenDist(0, 2047);
    for (int i = 0; i < 200; ++i) {
        std::string data;
        const int len = lenDist(rng);
        for (int j = 0; j < len; ++j) {
            data.push_back(static_cast<char>(byteDist(rng)));
        }
        run(data, {static_cast<int>(data.size())}); // must not throw
    }
}

TEST_CASE(hardening_fuzz_with_escape_payloads_never_raises)
{
    std::mt19937 rng(0xF00DFACE);
    std::uniform_int_distribution<int> byteDist(0, 0xFF);
    std::uniform_int_distribution<int> lenDist(0, 127);
    const std::vector<std::string> prefixes = {"\x1b[", "\x1b", "\x1b]", "\x1bP",
                                               "\x1b_", "\x9b", "\x9d"};
    std::uniform_int_distribution<int> prefixDist(0, static_cast<int>(prefixes.size()) - 1);
    for (int i = 0; i < 200; ++i) {
        std::string payload;
        const int len = lenDist(rng);
        for (int j = 0; j < len; ++j) {
            payload.push_back(static_cast<char>(byteDist(rng)));
        }
        const std::string data = prefixes[prefixDist(rng)] + payload;
        run(data, {static_cast<int>(data.size())}); // must not throw
    }
}

TEST_CASE(hardening_recovers_to_ground_after_garbage)
{
    // Garbage then CAN: everything parsed afterwards must be clean.
    std::mt19937 rng(0xBADC0DE);
    std::uniform_int_distribution<int> byteDist(0, 0xFF);
    std::uniform_int_distribution<int> lenDist(0, 511);
    for (int i = 0; i < 50; ++i) {
        std::string garbage;
        const int len = lenDist(rng);
        for (int j = 0; j < len; ++j) {
            garbage.push_back(static_cast<char>(byteDist(rng)));
        }
        Recorder recorder;
        Parser parser(&recorder);
        parser.feedBytes(garbage); // may print/execute while in GROUND: unasserted
        parser.feedBytes("\x18");  // CAN: every state terminates to GROUND
        parser.feedBytes(kGood);
        parser.flush();
        // The clean stream after the CAN must parse perfectly, whatever
        // happened before it.
        const std::vector<Event> events = recorder.events;
        const size_t n = events.size();
        if (n < 3 || !(events[n - 3] == csiEvent("", "", Params{{31}}, "m")) ||
            !(events[n - 2] == charsEvent(U"Hello 👍")) || !(events[n - 1] == execEvent(7))) {
            throw CheckFailed{"recovery mismatch: " + eventsToString(events)};
        }
    }
}

TEST_CASE(hardening_partial_utf8_spans_chunks)
{
    // A 4-byte character split 2|2 and a 3-byte one split 1|1|1.
    expectEvents(run("\xf0\x9f", {2}), {});
    Recorder recorder;
    Parser parser(&recorder);
    parser.feedBytes("\xf0\x9f");
    parser.feedBytes("\x98\x80");
    parser.flush();
    expectEvents(recorder.events, {charsEvent(U"\U0001F600")});
}

TEST_CASE(hardening_flush_boundary_matches_chunked_feed)
{
    Recorder recorder;
    Parser parser(&recorder);
    parser.feed(U"ab");
    parser.feed(U"\x1B[31m");
    parser.flush();
    expectEvents(recorder.events,
                 {charsEvent(U"ab"), csiEvent("", "", Params{{31}}, "m")});
}

TEST_CASE(hardening_mid_sequence_utf8_does_not_desync)
{
    // A non-parameter code point inside CSI falls to the default (xterm.js
    // ERROR): it is ignored and the parser returns to GROUND.
    Recorder recorder;
    Parser parser(&recorder);
    parser.feedBytes("\x1b[");
    parser.feedBytes("\xc3\xa9m"); // é is not a param byte: ignored
    parser.feedBytes("\x1b[31mok");
    parser.flush();
    expectEvents(recorder.events,
                 {charsEvent(U"m"), csiEvent("", "", Params{{31}}, "m"),
                  charsEvent(U"ok")});
}

TEST_CASE(hardening_truncated_sequences_then_resume)
{
    struct Case {
        std::string data;
        std::vector<Event> trailing;
    };
    const Case cases[] = {
        {"\x1b[?", {execEvent(24)}},  // CSI_PARAM: CAN executes
        {"\x1b[>", {execEvent(24)}},
        {"\x1b]0;", {}},              // OSC_STRING: CAN aborts without executing
        {"\x1bP", {execEvent(24)}},   // DCS_ENTRY: CAN cancels via global
        {"\x1b_", {}},                // APC_ENTRY: CAN ends without executing
        {"\x1b(", {execEvent(24)}},   // CHARSET: CAN executes
        {"\x1b#", {execEvent(24)}},
        {"\x1b ", {execEvent(24)}},   // ESCAPE_INTERMEDIATE
    };
    for (const auto& c : cases) {
        Recorder recorder;
        Parser parser(&recorder);
        parser.feedBytes(c.data);
        parser.feedBytes("\x18"); // cancel
        parser.feedBytes("ok");
        parser.flush();
        std::vector<Event> expected = c.trailing;
        expected.push_back(charsEvent(U"ok"));
        expectEvents(recorder.events, expected);
    }
}