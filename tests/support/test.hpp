// A tiny test harness. No framework dependency: the repo's other C++ suites use a
// small custom harness, and a framework is one more package that must exist in
// every build rootfs.
#pragma once

#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace test {

struct Case {
    std::string name;
    std::function<void()> fn;
};

inline std::vector<Case>& registry() {
    static std::vector<Case> cases;
    return cases;
}

struct Register {
    Register(const std::string& name, std::function<void()> fn) { registry().push_back({name, std::move(fn)}); }
};

// Failures accumulate in a per-case counter so one assertion does not hide the
// rest of the case.
inline int& failures() {
    static int n = 0;
    return n;
}

inline void fail(const std::string& file, int line, const std::string& msg) {
    std::cerr << "  FAIL " << file << ":" << line << ": " << msg << "\n";
    failures()++;
}

}  // namespace test

#define VYZ_TEST(name)                                             \
    static void name();                                            \
    static ::test::Register reg_##name(#name, name);               \
    static void name()

#define CHECK(cond)                                                \
    do {                                                           \
        if (!(cond)) ::test::fail(__FILE__, __LINE__, "CHECK(" #cond ")"); \
    } while (0)

#define CHECK_EQ(a, b)                                             \
    do {                                                           \
        if (!((a) == (b))) ::test::fail(__FILE__, __LINE__, "CHECK_EQ(" #a ", " #b ")"); \
    } while (0)
