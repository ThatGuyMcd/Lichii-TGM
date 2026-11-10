// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "common/common_types.h"
#include "common/settings_enums.h"

// The Legend of Zelda: Ocarina of Time's built-in aspect Add-Ons (file_sys/patch_manager.cpp)
// change its render targets, camera and UI together. The original 21:9 profile stays 3440x1440.
namespace Core::OotUltrawide {

constexpr u64 ProgramId = 0x0400486024DEC000ULL;
constexpr std::string_view HeapDefPath = "System/HeapSize/Product.Nin_Ounce_NVN2.HeapDef.byml.zs";

struct RenderProfile {
    Settings::AspectRatio aspect;
    std::string_view addon;
    u32 width;
};

constexpr u32 TargetHeight = 1440;
constexpr u32 SceneHeight = 1080;
constexpr u32 ShippedTargetWidth = 2560;
inline constexpr std::array<RenderProfile, 7> AspectProfiles{{
    {Settings::AspectRatio::R21_9, "Ultrawide 21:9 (built-in)", 3440},
    {Settings::AspectRatio::R4_3, "Aspect ratio 4:3 (built-in)", 1920},
    {Settings::AspectRatio::R16_10, "Aspect ratio 16:10 (built-in)", 2304},
    {Settings::AspectRatio::R3_2, "Aspect ratio 3:2 (built-in)", 2160},
    {Settings::AspectRatio::R2_1, "Aspect ratio 2:1 (built-in)", 2880},
    {Settings::AspectRatio::R32_9, "Aspect ratio 32:9 (built-in)", 5120},
    {Settings::AspectRatio::R32_10, "Aspect ratio 32:10 (built-in)", 4608},
}};

[[nodiscard]] constexpr const RenderProfile* ProfileForAspect(Settings::AspectRatio aspect) {
    for (const auto& profile : AspectProfiles) {
        if (profile.aspect == aspect) {
            return &profile;
        }
    }
    return nullptr;
}

[[nodiscard]] constexpr f32 TargetAspect(const RenderProfile& profile) {
    return static_cast<f32>(profile.width) / TargetHeight;
}

/// The selected profile while docked, without an upscaler requiring the original 16:9 targets.
[[nodiscard]] const RenderProfile* ActiveProfile();
/// Compatibility name: true for any active aspect Add-On, including narrower profiles.
[[nodiscard]] bool UltrawideRendering();

/// The game's render targets live in the HeapDef's "DynamicTextureAllocatorMem" heap (312320 KiB,
/// inside "Graphics"), sized for its 2560x1440 picture. It grows by its size times the 3440x1440
/// picture's extra pixels, in whole MiB: 312320 KiB * (3440 / 2560 - 1) = 107360 -> 107520 KiB.
constexpr u32 ShippedTextureHeapKiB = 312320;
constexpr u32 TextureHeapExtraKiB = 107520;
/// The application pool and the DRAM grow by as much (in 2 MiB pages), so "Graphics" growing
/// takes nothing from the game's other heaps ("Reserved" is the rest of the pool).
constexpr u64 MemoryExtraBytes = 106ULL << 20;

[[nodiscard]] constexpr u32 TextureHeapExtraKiBFor(const RenderProfile& profile) {
    if (profile.width <= ShippedTargetWidth) {
        return 0;
    }
    constexpr u64 rounding = u64{ShippedTargetWidth} * 1024;
    const u64 extra = u64{ShippedTextureHeapKiB} * (profile.width - ShippedTargetWidth);
    return static_cast<u32>((extra + rounding - 1) / rounding * 1024);
}

[[nodiscard]] constexpr u64 MemoryExtraBytesFor(const RenderProfile& profile) {
    constexpr u64 page = 2ULL << 20;
    return ((u64{TextureHeapExtraKiBFor(profile)} << 10) + page - 1) / page * page;
}

/// The active profile's memory growth for a Switch 2 program, else 0 (KSystemControl).
[[nodiscard]] u64 ApplicationMemoryExtra();

/// The HeapDef table (a zstd-compressed BYML) with "DynamicTextureAllocatorMem" and "Graphics" grown
/// by extra_kib, or nothing when it is not the table this expects.
[[nodiscard]] std::optional<std::vector<u8>> GrowHeapDef(std::span<const u8> compressed,
                                                         u32 extra_kib);

/// The UI layouts (SARC of "BFFH" layout files, zstd against the dictionary "zs.zsdic" in the SARC
/// Pack/ZsDic.pack.zs).
constexpr std::string_view LayoutArchivePath =
    "UI/Layout2Archive/Common.Product.100.Nin_Ounce_NVN2.blarc.zs";
constexpr std::string_view DictionaryPackPath = "Pack/ZsDic.pack.zs";
constexpr std::string_view LayoutDictionaryName = "zs.zsdic";

/// A pane of a UI layout moved in x (layout units: the 3840x2160 grid, origin in the middle).
struct PaneShift {
    std::string_view layout; ///< the layout file in the archive, e.g. "PlayerStatus_00.buil"
    std::string_view pane;
    f32 original_x;
    f32 new_x;
};

/// The HUD's own edge anchors: PlayerStatus_00's N_ScaleL_00 (hearts, magic, Stone of Agony) and
/// N_ScaleR_00 (item buttons, shortcut) sit at x = -1920 / +1920, the 16:9 grid's edges. The
/// Add-On's UI canvas squeeze (16/9 over 43/18) puts the grid into the centred 16:9; the 21:9
/// screen's edges are at +-1920 / squeeze = +-2580, so the anchors move out by 660.
constexpr f32 GridHalfWidth = 1920.0f;
constexpr f32 EdgeShift = 660.0f; // 1920 * (43/18) / (16/9) - 1920
[[nodiscard]] constexpr f32 EdgeShiftFor(const RenderProfile& profile) {
    return GridHalfWidth * (static_cast<f32>(profile.width) - ShippedTargetWidth) /
           ShippedTargetWidth;
}
/// The minimap (MapMenu_00, bottom right) has no edge anchor: its frame, floor label, dungeon
/// capture and capture group sit at fixed x, and they move out as one by the same 660. Each HUD
/// layout's N_VRS_00 marks its element's screen region (530x530 on the minimap, 240x240 on the item
/// buttons) and moves with it.
inline constexpr PaneShift HudEdgeShifts[]{
    {"PlayerStatus_00.buil", "N_ScaleL_00", -GridHalfWidth, -GridHalfWidth - EdgeShift},
    {"PlayerStatus_00.buil", "N_ScaleR_00", GridHalfWidth, GridHalfWidth + EdgeShift},
    {"PlayerStatus_00.buil", "N_VRS_00", 1626.0f, 1626.0f + EdgeShift},
    {"MapMenu_00.buil", "N_VRS_00", 1536.0f, 1536.0f + EdgeShift},
    {"MapMenu_00.buil", "N_MiniMapCap_00", 1470.0f, 1470.0f + EdgeShift},
    {"MapMenu_00.buil", "P_MiniMapFrame_00", 1536.0f, 1536.0f + EdgeShift},
    {"MapMenu_00.buil", "S_Floor_00", 1706.0f, 1706.0f + EdgeShift},
    {"MapMenu_00.buil", "P_CapDungeon_00", 1536.0f, 1536.0f + EdgeShift},
};

[[nodiscard]] constexpr auto HudEdgeShiftsFor(const RenderProfile& profile) {
    auto shifts = std::to_array(HudEdgeShifts);
    const f32 extra = EdgeShiftFor(profile);
    for (auto& shift : shifts) {
        shift.new_x = shift.original_x + (shift.original_x < 0 ? -extra : extra);
    }
    return shifts;
}

/// A SARC archive's first file named `name`, or nothing.
[[nodiscard]] std::optional<std::span<const u8>> SarcFile(std::span<const u8> sarc,
                                                          std::string_view name);

/// Moves the panes in every copy of their layout inside the SARC `sarc`, in place. Each pane must be
/// found with its original x (layouts repeat in the archive; every copy is moved). False, with
/// `sarc` unchanged, when one is not.
[[nodiscard]] bool ShiftPanes(std::vector<u8>& sarc, std::span<const PaneShift> shifts);

/// The layout archive (zstd against `dictionary`) with the HUD's edge anchors moved to the 21:9
/// screen's edges (HudEdgeShifts), or nothing when it is not the archive this expects.
[[nodiscard]] std::optional<std::vector<u8>> AnchorHudToEdges(std::span<const u8> compressed,
                                                              std::span<const u8> dictionary);

/// Narrow profiles move the same HUD anchors inward; menu layouts need in-game visual checks.
[[nodiscard]] std::optional<std::vector<u8>> AnchorHudToEdges(std::span<const u8> compressed,
                                                              std::span<const u8> dictionary,
                                                              const RenderProfile& profile);

} // namespace Core::OotUltrawide
