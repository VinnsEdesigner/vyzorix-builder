// okfw — SHA-256, for the digest the pipeline pins.
#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace vyzorix::okfw {

// Sha256 accumulates bytes and produces the 32-byte digest.
class Sha256 {
public:
    Sha256() { reset(); }

    void reset();
    void update(const void* data, size_t len);
    void update(const std::string& data) { update(data.data(), data.size()); }

    // finish returns the 32-byte digest and leaves the object usable only after
    // reset().
    [[nodiscard]] std::array<std::uint8_t, 32> finish();

    // hex is the lowercase hex digest of the accumulated input.
    [[nodiscard]] std::string hex();

private:
    void transform(const std::uint8_t* block);

    std::array<std::uint32_t, 8> state_{};
    std::array<std::uint8_t, 64> buffer_{};
    std::uint64_t length_ = 0;
    size_t buffer_len_ = 0;
};

// sha256_file hashes a file's bytes. It streams, so a large artifact does not
// have to fit in memory.
[[nodiscard]] bool sha256_file(const std::string& path, std::string& hex_out);

// sha256_hex is the digest of a string, for tests and small inputs.
[[nodiscard]] std::string sha256_hex(const std::string& data);

}  // namespace vyzorix::okfw
