// Smoke test for the core test harness itself.
#include "harness.h"

TEST_CASE(harness_passes)
{
    QTERMX_CHECK(true);
    QTERMX_CHECK_EQ(1 + 1, 2);
}

TEST_CASE(harness_detects_failure)
{
    bool caught = false;
    try {
        QTERMX_CHECK(1 == 2);
    } catch (const qtermx::test::CheckFailed&) {
        caught = true;
    }
    QTERMX_CHECK(caught);
}