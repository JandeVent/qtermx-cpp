#ifndef QTERMX_TEST_HARNESS_H
#define QTERMX_TEST_HARNESS_H

// Minimal assert-based test harness for the Qt-free core.
// Headless-runnable: each test executable links this and calls runAll().
// See docs/adr/0001-test-harness.md for the decision.

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace qtermx::test {

struct TestCase {
    const char* name;
    std::function<void()> fn;
};

inline std::vector<TestCase>& registry()
{
    static std::vector<TestCase> r;
    return r;
}

struct Registrar {
    Registrar(const char* name, std::function<void()> fn)
    {
        registry().push_back({name, std::move(fn)});
    }
};

// Thrown on CHECK failure; caught by runAll() to report and continue.
struct CheckFailed {
    std::string message;
};

inline int runAll()
{
    int passed = 0;
    int failed = 0;
    for (const auto& tc : registry()) {
        try {
            tc.fn();
            std::printf("[PASS] %s\n", tc.name);
            std::fflush(stdout);
            ++passed;
        } catch (const CheckFailed& e) {
            std::printf("[FAIL] %s\n       %s\n", tc.name, e.message.c_str());
            std::fflush(stdout);
            ++failed;
        } catch (const std::exception& e) {
            std::printf("[FAIL] %s\n       unexpected exception: %s\n",
                        tc.name, e.what());
            std::fflush(stdout);
            ++failed;
        }
    }
    std::printf("\n%d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}

// Context-rich check: throws CheckFailed with a caller-built message.
inline void check(bool cond, const std::string& message)
{
    if (!cond) {
        throw CheckFailed{message};
    }
}

} // namespace qtermx::test

#define QTERMX_CHECK(cond)                                                     \
    do {                                                                       \
        if (!(cond)) {                                                         \
            throw ::qtermx::test::CheckFailed{                                \
                std::string("CHECK failed: ") + #cond + " at " + __FILE__ +   \
                ":" + std::to_string(__LINE__)};                              \
        }                                                                      \
    } while (0)

#define QTERMX_CHECK_EQ(a, b)                                                  \
    do {                                                                       \
        auto va = (a);                                                         \
        auto vb = (b);                                                         \
        if (!(va == vb)) {                                                     \
            throw ::qtermx::test::CheckFailed{                                \
                std::string("CHECK_EQ failed: ") + #a + " == " + #b +         \
                " at " + __FILE__ + ":" + std::to_string(__LINE__)};          \
        }                                                                      \
    } while (0)

#define TEST_CASE(name)                                                        \
    static void name();                                                        \
    static ::qtermx::test::Registrar registrar_##name(#name, name);            \
    static void name()

#endif // QTERMX_TEST_HARNESS_H