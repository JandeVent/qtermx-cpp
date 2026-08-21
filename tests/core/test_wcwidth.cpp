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