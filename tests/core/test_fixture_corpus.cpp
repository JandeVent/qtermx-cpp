// Phase 0 stub: verifies the vendored xterm.js fixture corpus is present
// and well-formed. The real conformance diffing (feed .in through the
// pipeline, compare render() against .text) lands in Phase 1.
#include "harness.h"

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace {

const fs::path kCorpus = fs::path(__FILE__).parent_path().parent_path().parent_path()
                             / "references" / "escape_sequence_files";

std::vector<std::string> listInFiles()
{
    std::vector<std::string> names;
    for (const auto& entry : fs::directory_iterator(kCorpus)) {
        const auto name = entry.path().filename().string();
        if (name.size() > 3 && name.substr(name.size() - 3) == ".in") {
            names.push_back(name.substr(0, name.size() - 3));
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}

} // namespace

TEST_CASE(corpus_directory_exists)
{
    QTERMX_CHECK(fs::is_directory(kCorpus));
}

TEST_CASE(corpus_has_expected_fixture_count)
{
    // 13 .in/.text pairs vendored from xterm.js (t0001..t0081).
    QTERMX_CHECK_EQ(listInFiles().size(), 13u);
}

TEST_CASE(every_in_has_matching_text)
{
    for (const auto& name : listInFiles()) {
        QTERMX_CHECK(fs::exists(kCorpus / (name + ".text")));
    }
}

TEST_CASE(fixtures_are_nonempty)
{
    for (const auto& name : listInFiles()) {
        const auto size = fs::file_size(kCorpus / (name + ".in"));
        QTERMX_CHECK(size > 0);
    }
}