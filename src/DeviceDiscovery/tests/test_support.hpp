#pragma once

// Tiny dependency-free assertion helpers so the test suite builds with nothing
// but a C++17 compiler.

#include <cstdlib>
#include <iostream>
#include <string>

namespace testing {

inline int& failures() {
    static int count = 0;
    return count;
}

inline void report(bool ok, const std::string& what, const char* file, int line) {
    if (ok) {
        std::cout << "  ok   " << what << "\n";
        return;
    }
    ++failures();
    std::cout << "  FAIL " << what << " (" << file << ":" << line << ")\n";
}

inline int summary(const char* suite) {
    if (failures() == 0) {
        std::cout << suite << ": all checks passed\n";
        return 0;
    }
    std::cout << suite << ": " << failures() << " check(s) failed\n";
    return 1;
}

}  // namespace testing

#define CHECK(expr) ::testing::report((expr), #expr, __FILE__, __LINE__)

#define CHECK_EQ(a, b)                                                                    \
    do {                                                                                  \
        auto lhs_value = (a);                                                             \
        auto rhs_value = (b);                                                             \
        const bool ok = (lhs_value == rhs_value);                                         \
        if (!ok) {                                                                        \
            std::cout << "    expected: " << rhs_value << "\n    actual:   " << lhs_value \
                      << "\n";                                                            \
        }                                                                                 \
        ::testing::report(ok, #a " == " #b, __FILE__, __LINE__);                          \
    } while (false)

#define SECTION(name) std::cout << "[" << (name) << "]\n"
