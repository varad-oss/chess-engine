// A deliberately tiny unit-test framework (no external dependencies).
//
//   TEST_CASE(name) { CHECK(cond); CHECK_EQ(a, b); REQUIRE(cond); }
//
// CHECK records a failure and continues; REQUIRE aborts the current test case.
#pragma once

#include <sstream>
#include <string>
#include <vector>

namespace test {

struct Case {
    const char* name;
    void (*fn)();
};

std::vector<Case>& registry();

struct Registrar {
    Registrar(const char* name, void (*fn)()) { registry().push_back({name, fn}); }
};

struct Abort {};

void report_failure(const char* file, int line, const std::string& what);
void count_check();

}  // namespace test

#define TEST_CASE(name)                                         \
    static void name();                                         \
    static const test::Registrar name##_registrar(#name, name); \
    static void name()

#define CHECK(cond)                                                       \
    do {                                                                  \
        test::count_check();                                              \
        if (!(cond)) test::report_failure(__FILE__, __LINE__, #cond);     \
    } while (0)

#define CHECK_EQ(a, b)                                                    \
    do {                                                                  \
        test::count_check();                                              \
        const auto& check_a_ = (a);                                       \
        const auto& check_b_ = (b);                                       \
        if (!(check_a_ == check_b_)) {                                    \
            std::ostringstream check_os_;                                 \
            check_os_ << #a " == " #b "  [" << check_a_ << " vs " << check_b_ << "]"; \
            test::report_failure(__FILE__, __LINE__, check_os_.str());    \
        }                                                                 \
    } while (0)

#define REQUIRE(cond)                                                     \
    do {                                                                  \
        test::count_check();                                              \
        if (!(cond)) {                                                    \
            test::report_failure(__FILE__, __LINE__, #cond);              \
            throw test::Abort{};                                          \
        }                                                                 \
    } while (0)
