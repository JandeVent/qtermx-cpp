// T02 — Mode registry (ANSI + DEC-private namespaces) and its setters
// (port of tests/screen/test_modes.py). SM/RM and DECSET/DECRST each
// collapse to one loop over parameters, toggling modes in one of two
// registries. DECAWM (autowrap ?7) is ON by default; everything else
// starts off. A sequence with no parameters defaults to mode 0, which
// is no-op.
#include "harness.h"
#include "test_pipeline.h"

using namespace qtermx;
using namespace qtermx::test;

TEST_CASE(modes_default_off_except_autowrap)
{
    Screen& screen = feedTo(U"");
    QTERMX_CHECK(!screen.mode(kIrm));
    QTERMX_CHECK(!screen.mode(kNlm));
    QTERMX_CHECK(!screen.mode(kDecom, true));
    QTERMX_CHECK(screen.mode(kDecawm, true));
}

TEST_CASE(modes_sm_sets_ansi_mode)
{
    Screen& screen = feedTo(U"\x1B[4h"); // SM: insert mode on
    QTERMX_CHECK(screen.mode(kIrm));
}

TEST_CASE(modes_rm_resets_ansi_mode)
{
    Screen& screen = feedTo(U"\x1B[4h\x1B[4l");
    QTERMX_CHECK(!screen.mode(kIrm));
}

TEST_CASE(modes_decset_sets_private_mode)
{
    Screen& screen = feedTo(U"\x1B[?7l\x1B[?6h");
    QTERMX_CHECK(!screen.mode(kDecawm, true));
    QTERMX_CHECK(screen.mode(kDecom, true));
}

TEST_CASE(modes_decrst_resets_private_mode)
{
    Screen& screen = feedTo(U"\x1B[?7h\x1B[?7l");
    QTERMX_CHECK(!screen.mode(kDecawm, true));
}

TEST_CASE(modes_multiple_params_one_sequence)
{
    Screen& screen = feedTo(U"\x1B[4;20h");
    QTERMX_CHECK(screen.mode(kIrm));
    QTERMX_CHECK(screen.mode(kNlm));
}

TEST_CASE(modes_dec_multiple_params)
{
    Screen& screen = feedTo(U"\x1B[?6;7h");
    QTERMX_CHECK(screen.mode(kDecom, true));
    QTERMX_CHECK(screen.mode(kDecawm, true));
}

TEST_CASE(modes_namespaces_are_separate)
{
    // ?4 is DEC private (smooth scroll): toggling it must not touch ANSI
    // IRM (4) or vice versa.
    Screen& screen = feedTo(U"\x1B[?4h");
    QTERMX_CHECK(!screen.mode(kIrm));
    QTERMX_CHECK(screen.mode(4, true));
    Screen& screen2 = feedTo(U"\x1B[?4l");
    QTERMX_CHECK(!screen2.mode(4, true));
}

TEST_CASE(modes_empty_params_default_to_mode_zero)
{
    // SM/RM with no parameters → mode 0: no-op, must not crash or
    // toggle anything real.
    Screen& screen = feedTo(U"\x1B[h\x1B[l");
    QTERMX_CHECK(!screen.mode(kIrm));
    QTERMX_CHECK(screen.mode(kDecawm, true));
}