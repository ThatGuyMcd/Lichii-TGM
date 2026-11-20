// SPDX-License-Identifier: GPL-3.0-or-later

// Run with undefined-behavior sanitization: a zero byte count does not make a null
// memcpy operand valid. The generated header contains the unmodified production paths.
#include <array>
#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <utility>

#include "zero_length_copy_source.h"

namespace {
void Check(bool value) {
    if (!value) {
        std::fputs("Zero-length copy contract failed\n", stderr);
        std::exit(1);
    }
}

void ScratchFirstAllocation() {
    Common::ScratchBuffer<std::uint32_t> buffer;
    Check(buffer.data() == nullptr && buffer.size() == 0 && buffer.capacity() == 0);
    buffer.resize(3);
    Check(buffer.data() != nullptr && buffer.size() == 3 && buffer.capacity() == 3);
    buffer[0] = 0x12345678;
    buffer[1] = 0xabcdef01;
    buffer[2] = 0x76543210;
    buffer.resize(1);
    buffer.resize(9);
    // Growth retains the allocated prefix, including data beyond the last requested size.
    Check(buffer.size() == 9 && buffer.capacity() == 9);
    Check(buffer[0] == 0x12345678 && buffer[1] == 0xabcdef01 && buffer[2] == 0x76543210);
}

void ScratchReuse() {
    Common::ScratchBuffer<std::uint32_t> original(2);
    original[0] = 17;
    original[1] = 23;
    auto moved = std::move(original);
    Check(moved[0] == 17 && moved[1] == 23);
    Check(original.data() == nullptr && original.capacity() == 0);
    original.resize(4);
    Check(original.data() != nullptr && original.size() == 4);
    moved.release();
    Check(moved.data() == nullptr && moved.capacity() == 0);
    moved.resize(5);
    Check(moved.data() != nullptr && moved.size() == 5);
}

void EmptySource() {
    std::vector<std::uint8_t> destination{1, 2, 3, 4};
    const auto expected = destination;
    CopyRegression::PutBytes(destination, 2, {});
    CopyRegression::PutBytes(destination, destination.size(), {});
    Check(destination == expected);
}

void EmptyDestination() {
    std::vector<std::uint8_t> destination;
    CopyRegression::PutBytes(destination, 0, {});
    Check(destination.empty());
}

void CopyControls() {
    std::vector<std::uint8_t> destination{0xa1, 0xa2, 0xa3, 0xa4, 0xa5};
    const std::array<std::uint8_t, 3> payload{0x00, 0xff, 0x7e};
    CopyRegression::PutBytes(destination, 1, payload);
    Check((destination == std::vector<std::uint8_t>{0xa1, 0x00, 0xff, 0x7e, 0xa5}));
    // A non-null zero-sized span is also a no-op.
    CopyRegression::PutBytes(destination, 0, std::span{payload}.first(0));
    Check((destination == std::vector<std::uint8_t>{0xa1, 0x00, 0xff, 0x7e, 0xa5}));
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        return 2;
    }
    const std::string_view mode{argv[1]};
    if (mode == "scratch-first") {
        ScratchFirstAllocation();
    } else if (mode == "scratch-reuse") {
        ScratchReuse();
    } else if (mode == "nup-empty-source") {
        EmptySource();
    } else if (mode == "nup-empty-destination") {
        EmptyDestination();
    } else if (mode == "controls") {
        CopyControls();
    } else {
        return 2;
    }
    return 0;
}
