#pragma once
// A bounded, owned copy of the presentation extensions whose contracts survive
// an intervening shader submission. Unknown/device-group chains still bypass.
// No Qt, Win32, loader state, borrowed temporary arrays, or implicit API calls.
#include <vulkan/vulkan_core.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace BsiShaders {
class VulkanPresent {
public:
    static constexpr std::size_t maxSwapchains = 32, maxExtensions = 8;
    VulkanPresent() = default;
    VulkanPresent(const VulkanPresent &) = delete;
    VulkanPresent &operator=(const VulkanPresent &) = delete;
    bool prepare(const VkPresentInfoKHR &source, std::string *error) {
        info_ = source;
        used_ = 0;
        hasRegions_ = false;
        if (!source.swapchainCount || source.swapchainCount > maxSwapchains
            || !source.pSwapchains || !source.pImageIndices
            || source.waitSemaphoreCount > 64 || (source.waitSemaphoreCount && !source.pWaitSemaphores))
            return fail(error, "Vulkan presentation arrays are unsupported; original presentation retained.");
        std::array<const VkBaseInStructure *, maxExtensions> seen{};
        std::array<bool, 5> kinds{};
        auto input = static_cast<const VkBaseInStructure *>(source.pNext);
        while (input) {
            if (used_ == maxExtensions)
                return fail(error, "Vulkan presentation extension chain exceeds the safety bound; original presentation retained.");
            for (std::size_t i = 0; i < used_; ++i)
                if (seen[i] == input) return fail(error, "Cyclic Vulkan presentation extension chain; original presentation retained.");
            seen[used_] = input;
            auto &node = nodes_[used_];
            node.type = input->sType;
            std::size_t kind = 0;
            switch (input->sType) {
            case VK_STRUCTURE_TYPE_PRESENT_REGIONS_KHR: {
                kind = 0;
                node.regions = *reinterpret_cast<const VkPresentRegionsKHR *>(input);
                if (node.regions.swapchainCount != source.swapchainCount || !node.regions.pRegions)
                    return fail(error, "Vulkan incremental-present arrays do not match the swapchains; original presentation retained.");
                for (std::size_t i = 0; i < source.swapchainCount; ++i) {
                    regions_[i] = node.regions.pRegions[i];
                    if (regions_[i].rectangleCount && !regions_[i].pRectangles)
                        return fail(error, "Vulkan incremental-present rectangles are missing; original presentation retained.");
                }
                node.regions.pRegions = regions_.data();
                hasRegions_ = true;
                break;
            }
            case VK_STRUCTURE_TYPE_PRESENT_TIMES_INFO_GOOGLE:
                kind = 1;
                node.times = *reinterpret_cast<const VkPresentTimesInfoGOOGLE *>(input);
                if (node.times.swapchainCount != source.swapchainCount || !node.times.pTimes)
                    return fail(error, "Vulkan presentation timing arrays do not match; original presentation retained.");
                break;
            case VK_STRUCTURE_TYPE_PRESENT_ID_KHR:
                kind = 2;
                node.ids = *reinterpret_cast<const VkPresentIdKHR *>(input);
                if (node.ids.swapchainCount != source.swapchainCount || !node.ids.pPresentIds)
                    return fail(error, "Vulkan presentation ID arrays do not match; original presentation retained.");
                break;
            case VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_EXT:
                kind = 3;
                node.fences = *reinterpret_cast<const VkSwapchainPresentFenceInfoEXT *>(input);
                if (node.fences.swapchainCount != source.swapchainCount || !node.fences.pFences)
                    return fail(error, "Vulkan presentation fence arrays do not match; original presentation retained.");
                break;
            case VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_MODE_INFO_EXT:
                kind = 4;
                node.modes = *reinterpret_cast<const VkSwapchainPresentModeInfoEXT *>(input);
                if (node.modes.swapchainCount != source.swapchainCount || !node.modes.pPresentModes)
                    return fail(error, "Vulkan presentation mode arrays do not match; original presentation retained.");
                break;
            default:
                return fail(error, "Vulkan presentation extension " + std::to_string(input->sType)
                            + " has no audited shader contract; original presentation retained.");
            }
            if (kinds[kind]) return fail(error, "Duplicate Vulkan presentation extension; original presentation retained.");
            kinds[kind] = true;
            ++used_;
            input = input->pNext;
        }
        for (std::size_t i = 0; i < used_; ++i) nodes_[i].link(i + 1 < used_ ? nodes_[i + 1].address() : nullptr);
        info_.pNext = used_ ? nodes_[0].address() : nullptr;
        return true;
    }
    void processed(std::uint32_t index, VkExtent2D extent) {
        if (!hasRegions_ || index >= info_.swapchainCount) return;
        // A color/spatial effect can change pixels outside the app's damage.
        // Widen only the processed SDR, single-layer image to its full extent.
        full_[index] = {{0, 0}, extent, 0};
        regions_[index] = {1, &full_[index]};
    }
    const VkPresentInfoKHR &info() const { return info_; }
private:
    static bool fail(std::string *error, const std::string &message) {
        if (error) *error = message;
        return false;
    }
    struct Node {
        VkStructureType type = VK_STRUCTURE_TYPE_MAX_ENUM;
        VkPresentRegionsKHR regions{};
        VkPresentTimesInfoGOOGLE times{};
        VkPresentIdKHR ids{};
        VkSwapchainPresentFenceInfoEXT fences{};
        VkSwapchainPresentModeInfoEXT modes{};
        const void *address() const {
            switch (type) {
            case VK_STRUCTURE_TYPE_PRESENT_REGIONS_KHR: return &regions;
            case VK_STRUCTURE_TYPE_PRESENT_TIMES_INFO_GOOGLE: return &times;
            case VK_STRUCTURE_TYPE_PRESENT_ID_KHR: return &ids;
            case VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_EXT: return &fences;
            case VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_MODE_INFO_EXT: return &modes;
            default: return nullptr;
            }
        }
        void link(const void *next) {
            switch (type) {
            case VK_STRUCTURE_TYPE_PRESENT_REGIONS_KHR: regions.pNext = next; break;
            case VK_STRUCTURE_TYPE_PRESENT_TIMES_INFO_GOOGLE: times.pNext = next; break;
            case VK_STRUCTURE_TYPE_PRESENT_ID_KHR: ids.pNext = next; break;
            case VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_EXT: fences.pNext = next; break;
            case VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_MODE_INFO_EXT: modes.pNext = next; break;
            default: break;
            }
        }
    };
    VkPresentInfoKHR info_{};
    std::size_t used_ = 0;
    bool hasRegions_ = false;
    std::array<Node, maxExtensions> nodes_{};
    std::array<VkPresentRegionKHR, maxSwapchains> regions_{};
    std::array<VkRectLayerKHR, maxSwapchains> full_{};
};
} // namespace BsiShaders
