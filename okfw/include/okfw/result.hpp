// okfw — Result and Error: a value-or-refusal type.
#pragma once

#include <string>
#include <utility>

namespace vyzorix::okfw {

// Error is a refusal with a human-readable message. Fallible functions return
// Result<T, Error>, so a failure is a value, not an exception across a boundary.
struct Error {
    std::string message;
};

[[nodiscard]] inline Error error(std::string message) { return Error{std::move(message)}; }

// Result is either a value or an Error — a small stand-in for std::expected.
template <typename T>
class Result {
public:
    static Result ok(T value) { return Result(std::move(value)); }
    static Result fail(Error e) { return {std::move(e)}; }

    [[nodiscard]] bool has_value() const { return ok_; }
    explicit operator bool() const { return ok_; }

    // value() is valid only when has_value(); it is deliberately not checked.
    [[nodiscard]] const T& value() const { return value_; }
    [[nodiscard]] T& value() { return value_; }
    [[nodiscard]] const Error& error() const { return err_; }

private:
    Result(T value) : ok_(true), value_(std::move(value)) {}
    Result(Error e) : ok_(false), err_(std::move(e)) {}

    bool ok_;
    T value_{};
    Error err_{};
};

// Result<void> carries only the error.
template <>
class Result<void> {
public:
    static Result ok() { return {}; }
    static Result fail(Error e) { return {std::move(e)}; }

    [[nodiscard]] bool has_value() const { return ok_; }
    explicit operator bool() const { return ok_; }
    [[nodiscard]] const Error& error() const { return err_; }

private:
    Result() : ok_(true) {}
    Result(Error e) : ok_(false), err_(std::move(e)) {}

    bool ok_;
    Error err_{};
};

}  // namespace vyzorix::okfw
