#include <chrono>
#include <cstring>
#include <exception>
#include <iostream>

#include "core/position.h"
#include "framework.h"

namespace test {

namespace {
int g_checks = 0;
int g_failures = 0;
}  // namespace

std::vector<Case>& registry() {
    static std::vector<Case> cases;
    return cases;
}

void count_check() { ++g_checks; }

void report_failure(const char* file, int line, const std::string& what) {
    ++g_failures;
    std::cerr << "  FAILED " << file << ':' << line << ": " << what << '\n';
}

}  // namespace test

// Usage: tests [substring]   (runs the cases whose name contains substring)
int main(int argc, char** argv) {
    chess::init();
    const char* filter = argc > 1 ? argv[1] : "";
    int run = 0, failed_cases = 0;
    const auto start = std::chrono::steady_clock::now();

    for (const test::Case& c : test::registry()) {
        if (!std::strstr(c.name, filter)) continue;
        ++run;
        const int failures_before = test::g_failures;
        const auto t0 = std::chrono::steady_clock::now();
        try {
            c.fn();
        } catch (const test::Abort&) {
        } catch (const std::exception& e) {
            test::report_failure(c.name, 0, std::string("uncaught exception: ") + e.what());
        }
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - t0)
                            .count();
        const bool ok = test::g_failures == failures_before;
        failed_cases += !ok;
        std::cout << (ok ? "[ OK ] " : "[FAIL] ") << c.name << " (" << ms << " ms)\n";
    }

    const auto total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now() - start)
                              .count();
    std::cout << '\n'
              << run - failed_cases << '/' << run << " test cases passed, " << test::g_checks
              << " checks, " << test::g_failures << " failures (" << total_ms << " ms)\n";
    return failed_cases == 0 && run > 0 ? 0 : 1;
}
