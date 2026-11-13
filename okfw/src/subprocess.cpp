#include "okfw/subprocess.hpp"

#include "okfw/interrupt.hpp"
#include "okfw/log.hpp"

#include <array>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <thread>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

namespace vyzorix::okfw {
namespace {

// find_in_path walks the PATH entries looking for an executable file named prog.
std::string find_in_path(const std::string& prog) {
    const char* path_env = std::getenv("PATH");
    if (path_env == nullptr) return "";

    std::stringstream path(path_env);
    std::string dir;
    while (std::getline(path, dir, ':')) {
        if (dir.empty()) dir = ".";
        std::string candidate = dir;
        candidate += '/';
        candidate += prog;
        if (access(candidate.c_str(), X_OK) == 0) return candidate;
    }
    return "";
}

}  // namespace

void run_quiet(const std::vector<std::string>& argv) {
    auto result = run(argv);
    if (!result) log(Level::Debug, "cleanup: " + result.error().message);
}

std::string which(const std::string& program) {
    if (program.find('/') != std::string::npos) {
        return access(program.c_str(), X_OK) == 0 ? program : "";
    }
    return find_in_path(program);
}

std::string error_first_line(const std::string& text) {
    size_t nl = text.find('\n');
    std::string line = nl == std::string::npos ? text : text.substr(0, nl);
    // Trim trailing carriage returns a subprocess may have left.
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
    return line;
}

std::string error_last_line(const std::string& text) {
    std::string last;
    size_t start = 0;
    while (start <= text.size()) {
        size_t nl = text.find('\n', start);
        std::string line = text.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (!line.empty()) last = line;
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    return last.empty() ? error_first_line(text) : last;
}

Result<ProcessOutput> run_with_timeout(const std::vector<std::string>& argv,
                                       int timeout_seconds,
                                       const std::string& working_dir,
                                       const std::string& stdin_data) {
    if (argv.empty()) return Result<ProcessOutput>::fail(error("run: empty argv"));

    int in_pipe[2];
    int out_pipe[2];
    int err_pipe[2];
    if (pipe(in_pipe) != 0 || pipe(out_pipe) != 0 || pipe(err_pipe) != 0) {
        return Result<ProcessOutput>::fail(error("run: could not create pipes"));
    }

    pid_t pid = fork();
    if (pid < 0) return Result<ProcessOutput>::fail(error("run: could not fork"));

    if (pid == 0) {
        // Child: wire the pipes and exec.
        dup2(in_pipe[0], STDIN_FILENO);
        dup2(out_pipe[1], STDOUT_FILENO);
        dup2(err_pipe[1], STDERR_FILENO);

        close(in_pipe[0]); close(in_pipe[1]);
        close(out_pipe[0]); close(out_pipe[1]);
        close(err_pipe[0]); close(err_pipe[1]);

        if (!working_dir.empty() && chdir(working_dir.c_str()) != 0) {
            std::fprintf(stderr, "run: chdir %s failed\n", working_dir.c_str());
            _exit(127);
        }

        // execvp takes char* const[]; the strings are not modified, which is
        // exactly the case POSIX's signature cannot express.
        std::vector<char*> cargv;
        cargv.reserve(argv.size() + 1);
        for (const auto& a : argv) cargv.push_back(const_cast<char*>(a.c_str()));  // NOLINT(cppcoreguidelines-pro-type-const-cast)
        cargv.push_back(nullptr);

        execvp(cargv[0], cargv.data());
        std::fprintf(stderr, "run: exec %s failed: %s\n", argv[0].c_str(), std::strerror(errno));
        _exit(127);
    }

    // Parent.
    close(in_pipe[0]);
    close(out_pipe[1]);
    close(err_pipe[1]);

    // Write the request in full, then close stdin so the child sees EOF. A single
    // write() may be partial, and a truncated request is a silent decode failure
    // on the other side.
    size_t offset = 0;
    while (offset < stdin_data.size()) {
        ssize_t n = ::write(in_pipe[1], stdin_data.data() + offset, stdin_data.size() - offset);
        if (n <= 0) break;
        offset += static_cast<size_t>(n);
    }
    close(in_pipe[1]);

    ProcessOutput result;

    // Drain stdout and stderr *together*, with poll(). Reading one to EOF before
    // the other deadlocks when the child fills the other pipe's buffer while not
    // reading the first: the child blocks on write, never exits, and the parent
    // blocks on read. `docker build` writes progress to stderr, so this is the
    // common case, not a corner.
    auto set_nonblocking = [](int fd) {
        int flags = fcntl(fd, F_GETFL, 0);
        if (flags != -1) fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    };
    set_nonblocking(out_pipe[0]);
    set_nonblocking(err_pipe[0]);

    std::array<char, 65536> buf{};
    bool out_open = true;
    bool err_open = true;
    bool timed_out = false;
    bool cancelled = false;
    const auto start = std::chrono::steady_clock::now();

    // read_available drains what is ready on one fd. It returns true when the fd
    // reached EOF (and has been closed), false otherwise.
    auto read_available = [&](int fd, std::string& sink) -> bool {
        for (;;) {
            ssize_t n = ::read(fd, buf.data(), buf.size());
            if (n > 0) {
                sink.append(buf.data(), static_cast<size_t>(n));
                continue;
            }
            if (n == 0) { close(fd); return true; }  // EOF
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) return false;  // nothing more now
            close(fd);  // a real read error: treat as EOF so we never spin
            return true;
        }
    };

    while (out_open || err_open) {
        int remaining_ms = -1;
        if (timeout_seconds > 0) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();
            remaining_ms = timeout_seconds * 1000 - static_cast<int>(elapsed);
            if (remaining_ms <= 0) { timed_out = true; break; }
        }

        struct pollfd fds[2];
        int nfds = 0;
        if (out_open) { fds[nfds].fd = out_pipe[0]; fds[nfds].events = POLLIN; nfds++; }
        if (err_open) { fds[nfds].fd = err_pipe[0]; fds[nfds].events = POLLIN; nfds++; }

        int pr = ::poll(fds, static_cast<nfds_t>(nfds), remaining_ms);
        if (pr < 0) {
            // A signal interrupted the wait. If it was a cancellation request,
            // stop the child and unwind; otherwise retry the poll.
            if (errno == EINTR) {
                if (interrupted()) { cancelled = true; break; }
                continue;
            }
            break;
        }
        if (pr == 0) { timed_out = true; break; }

        for (int i = 0; i < nfds; i++) {
            if (!(fds[i].revents & (POLLIN | POLLHUP | POLLERR))) continue;
            if (fds[i].fd == out_pipe[0]) out_open = !read_available(out_pipe[0], result.out);
            else err_open = !read_available(err_pipe[0], result.err);
        }
    }

    if (cancelled) {
        // Terminate the child and reap it, so a cancelled build leaves no
        // orphan process (contract R3).
        if (out_open) close(out_pipe[0]);
        if (err_open) close(err_pipe[0]);
        ::kill(pid, SIGTERM);
        int status = 0;
        if (::waitpid(pid, &status, WNOHANG) != pid) {
            ::kill(pid, SIGKILL);
            ::waitpid(pid, &status, 0);
        }
        return Result<ProcessOutput>::fail(error("cancelled"));
    }

    if (timed_out) {
        // Ask politely, then insist. SIGTERM lets a well-behaved child clean up;
        // a wedged one gets SIGKILL. Reaping either way prevents a zombie.
        if (out_open) close(out_pipe[0]);
        if (err_open) close(err_pipe[0]);

        ::kill(pid, SIGTERM);
        for (int i = 0; i < 20; i++) {  // up to ~2s
            int status = 0;
            if (::waitpid(pid, &status, WNOHANG) == pid) {
                return Result<ProcessOutput>::fail(error("timed out after " + std::to_string(timeout_seconds) + "s"));
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        ::kill(pid, SIGKILL);
        int status = 0;
        ::waitpid(pid, &status, 0);
        return Result<ProcessOutput>::fail(error("timed out after " + std::to_string(timeout_seconds) + "s"));
    }

    int status = 0;
    waitpid(pid, &status, 0);
    result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;

    return Result<ProcessOutput>::ok(std::move(result));
}

Result<ProcessOutput> run(const std::vector<std::string>& argv,
                          const std::string& working_dir,
                          const std::string& stdin_data) {
    return run_with_timeout(argv, 0, working_dir, stdin_data);
}

}  // namespace vyzorix::okfw
