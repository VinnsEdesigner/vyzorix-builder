#include "support/test.hpp"

#include "okfw/sha256.hpp"

using vyzorix::okfw::sha256_hex;

VYZ_TEST(sha256_known_vectors) {
    // The canonical vectors, so a change to the implementation is caught here and
    // not by a device that refuses a digest.
    CHECK_EQ(sha256_hex(""),
             "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK_EQ(sha256_hex("abc"),
             "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK_EQ(sha256_hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
             "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

VYZ_TEST(sha256_long_input_spans_blocks) {
    // 1000 'a' characters crosses many 64-byte blocks, exercising the buffering.
    std::string thousand_a(1000, 'a');
    CHECK_EQ(sha256_hex(thousand_a),
             "41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3");
}
