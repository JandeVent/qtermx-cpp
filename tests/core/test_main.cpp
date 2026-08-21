// Shared main for core test executables. Test files define TEST_CASEs only.
#include "harness.h"

int main()
{
    return qtermx::test::runAll();
}