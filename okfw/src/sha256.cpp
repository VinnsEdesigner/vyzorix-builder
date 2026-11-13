#include "okfw/sha256.hpp"

#include <cstdio>
#include <cstring>
#include <vector>

namespace vyzorix::okfw {
namespace {

const std::uint32_t kK[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

inline std::uint32_t rotr(std::uint32_t x, std::uint32_t n) { return (x >> n) | (x << (32 - n)); }

}  // namespace

void Sha256::reset() {
    state_ = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
              0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    buffer_.fill(0);
    length_ = 0;
    buffer_len_ = 0;
}

void Sha256::transform(const std::uint8_t* block) {
    std::uint32_t w[64];
    for (std::size_t i = 0; i < 16; i++) {
        w[i] = (static_cast<std::uint32_t>(block[i * 4]) << 24) |
               (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16) |
               (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8) |
               static_cast<std::uint32_t>(block[i * 4 + 3]);
    }
    for (int i = 16; i < 64; i++) {
        std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    std::uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
    std::uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];

    for (int i = 0; i < 64; i++) {
        std::uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        std::uint32_t ch = (e & f) ^ (~e & g);
        std::uint32_t t1 = h + s1 + ch + kK[i] + w[i];
        std::uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        std::uint32_t t2 = s0 + maj;

        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    state_[0] += a; state_[1] += b; state_[2] += c; state_[3] += d;
    state_[4] += e; state_[5] += f; state_[6] += g; state_[7] += h;
}

void Sha256::update(const void* data, size_t len) {
    const auto* p = static_cast<const std::uint8_t*>(data);
    length_ += len;

    while (len > 0) {
        size_t take = 64 - buffer_len_;
        if (take > len) take = len;
        std::memcpy(buffer_.data() + buffer_len_, p, take);
        buffer_len_ += take;
        p += take;
        len -= take;

        if (buffer_len_ == 64) {
            transform(buffer_.data());
            buffer_len_ = 0;
        }
    }
}

std::array<std::uint8_t, 32> Sha256::finish() {
    std::uint64_t bit_len = length_ * 8;

    std::uint8_t pad = 0x80;
    update(&pad, 1);
    std::uint8_t zero = 0x00;
    while (buffer_len_ != 56) update(&zero, 1);

    std::uint8_t len_bytes[8];
    for (int i = 0; i < 8; i++) len_bytes[i] = static_cast<std::uint8_t>(bit_len >> (56 - i * 8));
    update(len_bytes, 8);

    std::array<std::uint8_t, 32> out{};
    for (std::size_t i = 0; i < 8; i++) {
        out[i * 4] = static_cast<std::uint8_t>(state_[i] >> 24);
        out[i * 4 + 1] = static_cast<std::uint8_t>(state_[i] >> 16);
        out[i * 4 + 2] = static_cast<std::uint8_t>(state_[i] >> 8);
        out[i * 4 + 3] = static_cast<std::uint8_t>(state_[i]);
    }
    return out;
}

std::string Sha256::hex() {
    static const char* digits = "0123456789abcdef";
    auto digest = finish();
    std::string out;
    out.reserve(64);
    for (std::uint8_t b : digest) {
        out.push_back(digits[b >> 4]);
        out.push_back(digits[b & 0x0f]);
    }
    return out;
}

bool sha256_file(const std::string& path, std::string& hex_out) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) return false;

    Sha256 hasher;
    std::vector<std::uint8_t> buf(65536);
    bool ok = true;
    for (;;) {
        std::size_t n = std::fread(buf.data(), 1, buf.size(), f);  // NOLINT(clang-analyzer-unix.Stream)
        if (n > 0) {
            hasher.update(buf.data(), n);
        }
        if (n < buf.size()) {
            // A short read ends the stream: either EOF (fine) or an error.
            ok = std::feof(f) != 0;
            break;
        }
    }
    std::fclose(f);

    if (!ok) return false;
    hex_out = hasher.hex();
    return true;
}

std::string sha256_hex(const std::string& data) {
    Sha256 h;
    h.update(data);
    return h.hex();
}

}  // namespace vyzorix::okfw
