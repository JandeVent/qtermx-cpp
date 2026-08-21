#ifndef QTERMX_TEST_PIPELINE_H
#define QTERMX_TEST_PIPELINE_H

// Shared test seam for the screen-family tests (port of the Python
// tests/screen/test_screen.py helpers): drive the full pipeline
// Parser → Emulator → Screen and observe the screen's state.

#include <memory>
#include <string>
#include <vector>

#include "emulator.h"
#include "parser.h"
#include "screen.h"

namespace qtermx::test {

struct Pipeline {
    Screen screen;
    Emulator emulator;
    Parser parser;

    Pipeline(int lines, int columns)
        : screen(lines, columns)
        , emulator(screen)
        , parser(&emulator)
    {
    }

    void feed(std::u32string_view text)
    {
        parser.feed(text);
        parser.flush();
    }
};

inline std::unique_ptr<Pipeline> makePipeline(int lines = 24, int columns = 80)
{
    return std::make_unique<Pipeline>(lines, columns);
}

// Feed text through the full pipeline and return the screen. The
// pipeline lives in a static slot replaced on every call — test-only,
// single-threaded; a returned reference stays valid until the next
// feedTo call (the Python tests reassign `screen` the same way).
inline Screen& feedTo(std::u32string_view text, int lines = 24, int columns = 80)
{
    static std::unique_ptr<Pipeline> p;
    p = makePipeline(lines, columns);
    p->feed(text);
    return p->screen;
}

inline std::vector<std::string> splitLines(const std::string& s)
{
    std::vector<std::string> lines;
    size_t start = 0;
    while (true) {
        const size_t nl = s.find('\n', start);
        if (nl == std::string::npos) {
            lines.push_back(s.substr(start));
            break;
        }
        lines.push_back(s.substr(start, nl - start));
        start = nl + 1;
    }
    return lines;
}

// The text of row `y` with trailing spaces stripped — the Python
// `"".join(cell.data for cell in line.cells).rstrip(" ")` the screen
// tests use to read a row's glyphs.
inline std::string rowText(const Screen& screen, int y)
{
    std::string row = splitLines(screen.render())[y];
    const size_t last = row.find_last_not_of(' ');
    return last == std::string::npos ? "" : row.substr(0, last + 1);
}

// Python str.strip() equivalent: trim leading/trailing whitespace
// (spaces and newlines).
inline std::string strip(const std::string& s)
{
    const auto isWs = [](char c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t'; };
    const size_t first = s.find_first_not_of(" \n\r\t");
    if (first == std::string::npos) {
        return "";
    }
    const size_t last = s.find_last_not_of(" \n\r\t");
    return s.substr(first, last - first + 1);
}

} // namespace qtermx::test

#endif // QTERMX_TEST_PIPELINE_H