#include "support/test.hpp"

int main() {
    int failed = 0;
    for (const auto& c : test::registry()) {
        test::failures() = 0;
        c.fn();
        if (test::failures() == 0) {
            std::cout << "PASS " << c.name << "\n";
        } else {
            std::cout << "FAIL " << c.name << "\n";
            failed++;
        }
    }
    if (failed > 0) std::cerr << failed << " case(s) failed\n";
    return failed == 0 ? 0 : 1;
}
