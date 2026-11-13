#include "support/test.hpp"

#include "okfw/subprocess.hpp"

using namespace vyzorix;

// These run real processes: the subprocess layer is the one place where the
// behaviour is the OS's, not ours, so a mock would test nothing. They are fast
// (a shell one-liner) and need no network.

VYZ_TEST(subprocess_captures_both_streams) {
    auto r = okfw::run({"/bin/sh", "-c", "echo out; echo err 1>&2"});
    CHECK(r.has_value());
    if (!r.has_value()) return;

    CHECK_EQ(r.value().exit_code, 0);
    CHECK(r.value().out.find("out") != std::string::npos);
    CHECK(r.value().err.find("err") != std::string::npos);
}

VYZ_TEST(subprocess_does_not_deadlock_when_stderr_fills_first) {
    // The child writes ~256 KB to stderr and nothing to stdout. The 64 KB stderr
    // pipe buffer fills, so the child blocks on write; a parent that drains
    // stdout to EOF before touching stderr waits forever. poll() on both fds is
    // what makes this terminate.
    auto r = okfw::run({"/bin/sh", "-c",
                        "yes err | head -c 262144 1>&2; echo tail 1>&2"});
    CHECK(r.has_value());
    if (!r.has_value()) return;

    CHECK_EQ(r.value().exit_code, 0);
    CHECK(r.value().err.size() >= 262144);
    CHECK(r.value().err.find("tail") != std::string::npos);
}

VYZ_TEST(subprocess_does_not_deadlock_when_stdout_fills_first) {
    // The mirror image: stdout fills, stderr is quiet.
    auto r = okfw::run({"/bin/sh", "-c", "yes out | head -c 262144; echo tail"});
    CHECK(r.has_value());
    if (!r.has_value()) return;

    CHECK_EQ(r.value().exit_code, 0);
    CHECK(r.value().out.size() >= 262144);
    CHECK(r.value().out.find("tail") != std::string::npos);
}

VYZ_TEST(subprocess_interleaved_output_is_all_captured) {
    // Both streams large and interleaved: neither may starve the other.
    auto r = okfw::run({"/bin/sh", "-c",
                        "i=0; while [ $i -lt 400 ]; do echo o$i; echo e$i 1>&2; i=$((i+1)); done"});
    CHECK(r.has_value());
    if (!r.has_value()) return;

    CHECK_EQ(r.value().exit_code, 0);
    CHECK(r.value().out.find("o399") != std::string::npos);
    CHECK(r.value().err.find("e399") != std::string::npos);
}

VYZ_TEST(subprocess_reports_a_nonzero_exit_as_a_value) {
    // A failing tool is a normal outcome the caller classifies, not an error.
    auto r = okfw::run({"/bin/sh", "-c", "echo boom 1>&2; exit 3"});
    CHECK(r.has_value());
    if (!r.has_value()) return;

    CHECK_EQ(r.value().exit_code, 3);
    CHECK(r.value().err.find("boom") != std::string::npos);
}

VYZ_TEST(subprocess_feeds_stdin) {
    auto r = okfw::run({"/bin/sh", "-c", "cat"}, "", "hello stdin");
    CHECK(r.has_value());
    if (!r.has_value()) return;

    CHECK_EQ(r.value().out, std::string("hello stdin"));
}

VYZ_TEST(subprocess_timeout_kills_a_wedged_child) {
    // A child that never exits must not hang the caller: the timeout terminates
    // it and returns a refusal, not a result.
    auto r = okfw::run_with_timeout({"/bin/sh", "-c", "sleep 300"}, 1);
    CHECK(!r.has_value());
    if (!r.has_value()) {
        CHECK(r.error().message.find("timed out") != std::string::npos);
    }
}

VYZ_TEST(subprocess_timeout_does_not_fire_when_the_child_finishes) {
    auto r = okfw::run_with_timeout({"/bin/sh", "-c", "echo fast"}, 30);
    CHECK(r.has_value());
    if (r.has_value()) CHECK_EQ(r.value().out.find("fast") != std::string::npos, true);
}
