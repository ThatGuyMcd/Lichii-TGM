// SPDX-License-Identifier: GPL-3.0-or-later

#include <openssl/evp.h>
#include "video_core/native_indirect_reset.h"

namespace VideoCommon::NativeIndirectReset {

bool MatchesFingerprint(std::span<const u8> code, const Fingerprint& expected) {
    if (code.size() != ProgramByteCount) {
        return false;
    }
    Fingerprint digest{};
    unsigned int length{};
    return EVP_Digest(code.data(), code.size(), digest.data(), &length, EVP_sha256(), nullptr) ==
               1 &&
           length == digest.size() && digest == expected;
}

bool Matches(std::span<const u8> code) {
    // Require the exact compatibility fingerprint before replacing guest shader execution.
    constexpr Fingerprint fingerprint{
        0x3d, 0x3f, 0xa3, 0x57, 0x80, 0xb2, 0x83, 0x49, 0xa4, 0x38, 0x63,
        0x20, 0x9a, 0x27, 0x9d, 0xca, 0x6b, 0x49, 0xc7, 0x45, 0x03, 0xb2,
        0xe1, 0x66, 0x3a, 0x16, 0xf2, 0xa2, 0xd2, 0x19, 0xcc, 0x4c,
    };
    constexpr u32 AttributeLoad = 0x321;
    return code.size() == ProgramByteCount &&
           ((u32{code[1]} << 8 | code[0]) & 0xfffU) == AttributeLoad &&
           MatchesFingerprint(code, fingerprint);
}

} // namespace VideoCommon::NativeIndirectReset
