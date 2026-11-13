// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <cstring>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace {
std::vector<VkPhysicalDeviceProperties> properties;
VKAPI_ATTR VkResult VKAPI_CALL Enumerate(VkInstance, u32* count, VkPhysicalDevice* devices) {
    if (devices) {
        for (u32 index = 0; index < *count; ++index) {
            devices[index] = reinterpret_cast<VkPhysicalDevice>(static_cast<uintptr_t>(index + 1));
        }
    }
    *count = static_cast<u32>(properties.size());
    return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL GetProperties(VkPhysicalDevice device, VkPhysicalDeviceProperties* out) {
    *out = properties.at(reinterpret_cast<uintptr_t>(device) - 1);
}
VKAPI_ATTR void VKAPI_CALL Destroy(VkInstance, const VkAllocationCallbacks*) {}
}

TEST_CASE("Vulkan enumeration keeps hardware before software ICDs and vendor order", "[vulkan]") {
    properties.clear();
    const auto add = [](const char* name, VkPhysicalDeviceType type, u32 vendor) {
        VkPhysicalDeviceProperties value{};
        std::strncpy(value.deviceName, name, sizeof(value.deviceName) - 1);
        value.deviceType = type;
        value.vendorID = vendor;
        properties.push_back(value);
    };
    add("llvmpipe", VK_PHYSICAL_DEVICE_TYPE_CPU, 0x10005);
    add("Adreno", VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU, 0x5143);
    add("Radeon", VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU, 0x1002);
    add("GeForce", VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU, 0x10de);
    add("Microsoft Dozen", VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU, 0x1414);
    add("SwiftShader", VK_PHYSICAL_DEVICE_TYPE_CPU, 0x1ae0);
    Vulkan::vk::InstanceDispatch dispatch{};
    dispatch.vkEnumeratePhysicalDevices = Enumerate;
    dispatch.vkGetPhysicalDeviceProperties = GetProperties;
    dispatch.vkDestroyInstance = Destroy;
    Vulkan::vk::Instance instance{reinterpret_cast<VkInstance>(uintptr_t{1}), dispatch};
    const auto devices = instance.EnumeratePhysicalDevices();
    REQUIRE(devices.size() == 6);
    const auto index = [](VkPhysicalDevice device) { return reinterpret_cast<uintptr_t>(device) - 1; };
    REQUIRE(index(devices[0]) == 3);
    REQUIRE(index(devices[1]) == 2);
    REQUIRE(index(devices[2]) == 1);
    REQUIRE(index(devices[3]) == 4);
    REQUIRE(properties[index(devices[4])].deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU);
    REQUIRE(properties[index(devices[5])].deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU);
    REQUIRE(instance.EnumeratePhysicalDevices() == devices);
}
