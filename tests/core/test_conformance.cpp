// T07 — xterm escape-sequence fixture conformance (port of
// tests/test_conformance.py, Phase 1 form: direct Parser → Emulator →
// Screen pipeline, no Session yet).
//
// The `.in`/`.text` pairs vendored from xterm.js feed through the full
// pipeline and `render()` is diffed against the expected `.text` at
// 80×25.
//
// Corpus conventions (from the Python runner):
// - the corpus was generated through a pty, so a bare LF reached the
//   terminal as CR LF (onlcr) — we normalize the same way before feeding;
// - `.text` rows keep their trailing spaces on content rows, but blank
//   rows are empty strings.
//
// Skipped fixtures:
// - t0004-LF: its `.in` is *typed input* (the `.text` includes the
//   driver echo) — reproducible only through a real pty (Phase 4).
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include "emulator.h"
#include "harness.h"
#include "parser.h"
#include "screen.h"

using namespace qtermx;
using namespace qtermx::test;

namespace {

const std::filesystem::path kCorpus =
    std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() /
    "references" / "escape_sequence_files";

// Fixtures not runnable at Phase 1, with the reason.
const std::set<std::string> kSkipped = {"t0004-LF"};

std::vector<std::string> fixtureNames()
{
    std::vector<std::string> names;
    for (const auto& entry : std::filesystem::directory_iterator(kCorpus)) {
        const std::string name = entry.path().filename().string();
        if (name.size() > 3 && name.substr(name.size() - 3) == ".in") {
            names.push_back(name.substr(0, name.size() - 3));
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}

std::string readFile(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// The corpus was generated through a pty: normalize bare LF to CR LF.
std::string normalizeOnlcr(const std::string& data)
{
    std::string out;
    out.reserve(data.size());
    for (const char c : data) {
        if (c == '\n') {
            out += "\r\n";
        } else {
            out += c;
        }
    }
    return out;
}

std::vector<std::string> splitRows(const std::string& s)
{
    std::vector<std::string> rows;
    size_t start = 0;
    while (true) {
        const size_t nl = s.find('\n', start);
        if (nl == std::string::npos) {
            rows.push_back(s.substr(start));
            break;
        }
        rows.push_back(s.substr(start, nl - start));
        start = nl + 1;
    }
    return rows;
}

// Feed the fixture's `.in` through the pipeline; return the rendered rows.
std::vector<std::string> runFixture(const std::string& name)
{
    Screen screen(25, 80);
    Emulator emulator(screen);
    Parser parser(&emulator);
    parser.feedBytes(normalizeOnlcr(readFile(kCorpus / (name + ".in"))));
    parser.flush();
    return splitRows(screen.render());
}

std::vector<std::string> expectedRows(const std::string& name)
{
    std::vector<std::string> rows = splitRows(readFile(kCorpus / (name + ".text")));
    if (!rows.empty() && rows.back().empty()) {
        rows.pop_back(); // the 26th split-line is just the trailing newline
    }
    return rows;
}

void assertRows(const std::vector<std::string>& actual,
                const std::vector<std::string>& expected, const std::string& name)
{
    if (actual.size() != 25 || expected.size() != 25) {
        throw CheckFailed{name + ": expected 25 rows, got " +
                          std::to_string(actual.size()) + " actual / " +
                          std::to_string(expected.size()) + " expected"};
    }
    for (size_t y = 0; y < expected.size(); ++y) {
        const std::string& exp = expected[y];
        const std::string& row = actual[y];
        if (exp.empty()) {
            // Blank row: everything must be spaces.
            if (row.find_first_not_of(' ') != std::string::npos) {
                throw CheckFailed{name + ": row " + std::to_string(y) +
                                  " expected blank, got " + row};
            }
        } else {
            if (row.rfind(exp, 0) != 0) {
                throw CheckFailed{name + ": row " + std::to_string(y) +
                                  " expected prefix " + exp + ", got " + row};
            }
            if (row.substr(exp.size()).find_first_not_of(' ') != std::string::npos) {
                throw CheckFailed{name + ": row " + std::to_string(y) +
                                  " has unexpected content past " + exp + ": " + row};
            }
        }
    }
}

} // namespace

TEST_CASE(conformance_all_fixtures_pass)
{
    const std::vector<std::string> names = fixtureNames();
    int ran = 0;
    for (const auto& name : names) {
        if (kSkipped.find(name) != kSkipped.end()) {
            continue;
        }
        assertRows(runFixture(name), expectedRows(name), name);
        ++ran;
    }
    // 13 fixtures total, 1 skipped at Phase 1 (t0004-LF needs a pty) →
    // 12 must run.
    QTERMX_CHECK(ran == 12);
}