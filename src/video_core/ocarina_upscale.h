// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string_view>
#include "common/common_types.h"

namespace VideoCommon {

enum class OcarinaUpscaleBackend { Native, Fsr, Dlss };

enum class OcarinaUpscalePass { None, ThreeQuarter, Pass1440p, FullSize };

constexpr u64 OcarinaDepthReaderKey = 0x20ab4117f4db5bdbULL;

/// Native keys survive lowering changes. Retain the known lowered variants for older captures.
constexpr OcarinaUpscalePass FindOcarinaUpscalePass(u64 native_key, u64 shader_hash) {
    using enum OcarinaUpscalePass;
    switch (native_key) {
    case 0x6f9367bd41dc0b58ULL:
        return ThreeQuarter;
    case 0xda6df3e54c716df2ULL:
    case 0xe4cda162ff6009e2ULL:
        return Pass1440p;
    }
    switch (shader_hash) {
    case 0xe40f5efb5480e692ULL:
    case 0xef542df87cacfe9eULL:
        return ThreeQuarter;
    case 0x6bd913e1f34e83ebULL:
    case 0x9302bc0d6d1d9bc1ULL:
    case 0x4c1ae2fe58f30a97ULL:
        return Pass1440p;
    case 0xc033c3ddc9f0897bULL:
        return FullSize;
    }
    return None;
}

constexpr bool IsOcarinaDepthReader(u64 native_key, u64 shader_hash) {
    return native_key == OcarinaDepthReaderKey || shader_hash == 0x1fc7a0423116281fULL;
}

/// render_scaled: a DLSS render-scale patch shrank the scene to a size FSR 3.1 takes, so FSR 3.1
/// reconstructs it when DLSS can't run.
constexpr OcarinaUpscaleBackend SelectOcarinaUpscaler(bool fsr_enabled, bool dlss_enabled,
                                                    bool dlss_available, bool render_scaled,
                                                    std::string_view override) {
    if (override == "native") {
        return OcarinaUpscaleBackend::Native;
    }
    if (override == "fsr") {
        return OcarinaUpscaleBackend::Fsr;
    }
    if ((dlss_enabled || override == "dlss") && dlss_available) {
        return OcarinaUpscaleBackend::Dlss;
    }
    return fsr_enabled || render_scaled ? OcarinaUpscaleBackend::Fsr
                                        : OcarinaUpscaleBackend::Native;
}

constexpr const char* OcarinaBackendName(OcarinaUpscaleBackend backend) {
    switch (backend) {
    case OcarinaUpscaleBackend::Native:
        return "the game's upscale";
    case OcarinaUpscaleBackend::Fsr:
        return "FSR";
    case OcarinaUpscaleBackend::Dlss:
        return "NVIDIA DLSS";
    }
    return "";
}

/// Texture LOD bias added to Ocarina's mipmapped samplers while FSR upscales. AMD recommends
/// log2(render / output) - 1: -1.58 for docked 1440p -> 4K.
constexpr float OcarinaFsrLodBias(OcarinaUpscaleBackend backend) {
    return backend == OcarinaUpscaleBackend::Fsr ? -1.58f : 0.0f;
}

/// Whether a sampler with that bias keeps it for an image: only docked (handheld isn't measured),
/// not for images the GPU wrote (render targets would read a mip too sharp), and not once the
/// upscaler failed this session (the game's own pass runs again).
constexpr bool KeepOcarinaFsrLodBias(bool docked, bool upscaler_failed, bool gpu_written) {
    return docked && !upscaler_failed && !gpu_written;
}

} // namespace VideoCommon
