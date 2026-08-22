// wcwidth cell-width measurement (port of the Python `wcwidth` package
// semantics the screen relies on). The table must match the Python
// oracle for the ranges the corpus and real programs exercise.
#include "harness.h"
#include "wcwidth.h"

TEST_CASE(wcwidth_ascii_is_one)
{
    QTERMX_CHECK(qtermx::wcwidth('a') == 1);
    QTERMX_CHECK(qtermx::wcwidth(' ') == 1);
    QTERMX_CHECK(qtermx::wcwidth('~') == 1);
}

TEST_CASE(wcwidth_combining_is_zero)
{
    QTERMX_CHECK(qtermx::wcwidth(0x0301) == 0); // combining acute
    QTERMX_CHECK(qtermx::wcwidth(0x0300) == 0); // combining grave
}

TEST_CASE(wcwidth_cjk_is_two)
{
    QTERMX_CHECK(qtermx::wcwidth(0x4F60) == 2); // 你
    QTERMX_CHECK(qtermx::wcwidth(0x4E2D) == 2); // 中
    QTERMX_CHECK(qtermx::wcwidth(0xAC00) == 2); // Hangul syllable
}

TEST_CASE(wcwidth_emoji_is_two)
{
    QTERMX_CHECK(qtermx::wcwidth(0x1F600) == 2); // 😀
    QTERMX_CHECK(qtermx::wcwidth(0x1F44D) == 2); // 👍
}

TEST_CASE(wcwidth_regional_indicators_are_two)
{
    // Python wcwidth 0.8.2 (the oracle) gives regional indicators
    // (flag emoji building blocks) width 2 — the Kuhn-era table must
    // be extended to match.
    QTERMX_CHECK(qtermx::wcwidth(0x1F1E6) == 2); // regional indicator A
    QTERMX_CHECK(qtermx::wcwidth(0x1F1FF) == 2); // regional indicator Z
}

TEST_CASE(wcwidth_euro_sign_is_one)
{
    QTERMX_CHECK(qtermx::wcwidth(0x20AC) == 1); // €
}

TEST_CASE(wcwidth_controls_are_minus_one)
{
    QTERMX_CHECK(qtermx::wcwidth(0x00) == 0); // NUL is 0 (Kuhn)
    QTERMX_CHECK(qtermx::wcwidth(0x07) == -1);
    QTERMX_CHECK(qtermx::wcwidth(0x1F) == -1);
    QTERMX_CHECK(qtermx::wcwidth(0x7F) == -1);
    QTERMX_CHECK(qtermx::wcwidth(0x9F) == -1);
}

// Regression tests for Unicode 17.0 table update (from wcwidth 0.8.2)
TEST_CASE(wcwidth_unicode17_zero_width_format_chars)
{
    // Zero-width format characters added/confirmed in Unicode 17.0
    QTERMX_CHECK(qtermx::wcwidth(0x200B) == 0); // zero-width space
    QTERMX_CHECK(qtermx::wcwidth(0x200C) == 0); // zero-width non-joiner
    QTERMX_CHECK(qtermx::wcwidth(0x200D) == 0); // zero-width joiner
    QTERMX_CHECK(qtermx::wcwidth(0xFEFF) == 0); // BOM (zero-width)
    QTERMX_CHECK(qtermx::wcwidth(0x2060) == 0); // word joiner
    QTERMX_CHECK(qtermx::wcwidth(0x2061) == 0); // function application
    QTERMX_CHECK(qtermx::wcwidth(0x2062) == 0); // invisible times
    QTERMX_CHECK(qtermx::wcwidth(0x2063) == 0); // invisible separator
    QTERMX_CHECK(qtermx::wcwidth(0x2064) == 0); // invisible plus
}

TEST_CASE(wcwidth_unicode17_wide_emoji)
{
    // Emoji and wide characters confirmed in Unicode 17.0
    QTERMX_CHECK(qtermx::wcwidth(0x1F600) == 2); // 😀 grinning face
    QTERMX_CHECK(qtermx::wcwidth(0x1F680) == 2); // 🚀 rocket
    QTERMX_CHECK(qtermx::wcwidth(0x1F308) == 2); // 🌈 rainbow
    QTERMX_CHECK(qtermx::wcwidth(0x1F4A5) == 2); // 💥 collision
    QTERMX_CHECK(qtermx::wcwidth(0x1F525) == 2); // 🔥 fire
}

TEST_CASE(wcwidth_unicode17_historic_scripts)
{
    // Historic scripts that are NOT wide (width 1) in Unicode 17.0
    // Egyptian hieroglyphs (U+13000-U+1342F) are width 1
    QTERMX_CHECK(qtermx::wcwidth(0x13000) == 1); // Egyptian hieroglyph
    // Gothic alphabet (U+10330-U+1034F) is width 1
    QTERMX_CHECK(qtermx::wcwidth(0x10330) == 1); // Gothic letter
}