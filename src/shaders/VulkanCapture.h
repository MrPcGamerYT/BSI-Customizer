#pragma once
#include <cstdint>
#include <string>

namespace BsiShaders {
// Lifecycle evidence is deliberately separate from an observed GAME renderer.
// A loaded Vulkan DLL, Qt/ANGLE surface or unowned queue is not game ownership
// and must never increment processedFrames or turn the effects indicator on.
enum class VulkanEvent {
    LoaderHooked, InstanceCreated, DeviceTracked, SurfaceCreated,
    SwapchainTracked, Presented, QueueMissed, WindowRejected, DeviceRejected
};
struct VulkanCapture {
    std::uint64_t loaders=0, instances=0, devices=0, surfaces=0, swapchains=0;
    std::uint64_t presents=0, missedQueues=0, rejectedWindows=0, rejectedDevices=0;
    std::string loader, issue;
    void note(VulkanEvent event, const std::string &detail={}) {
        switch (event) {
        case VulkanEvent::LoaderHooked: ++loaders; loader=detail.substr(0, 260); break;
        case VulkanEvent::InstanceCreated: ++instances; break;
        case VulkanEvent::DeviceTracked: ++devices; break;
        case VulkanEvent::SurfaceCreated: ++surfaces; break;
        case VulkanEvent::SwapchainTracked: ++swapchains; break;
        case VulkanEvent::Presented: ++presents; break;
        case VulkanEvent::QueueMissed: ++missedQueues; break;
        case VulkanEvent::WindowRejected: ++rejectedWindows; break;
        case VulkanEvent::DeviceRejected: ++rejectedDevices; break;
        }
        if (event!=VulkanEvent::LoaderHooked && !detail.empty()) issue=detail.substr(0, 1024);
    }
    std::string waiting() const {
        if (!loaders) return "Vulkan selected, but no complete Vulkan loader has been hooked. No game API has been identified.";
        if (!instances) return "Vulkan loader hooks are ready, but no instance creation was captured. Fully restart the player after applying; an already-created Vulkan device cannot be adopted safely.";
        if (!devices) return issue.empty()
            ? "Vulkan instance creation was captured, but no supported device/queue lifecycle was captured. No effects are active." : issue;
        if (!surfaces) return "Vulkan device captured, but no Win32 presentation surface was captured. Non-Win32/external presentation is not assumed to be the game.";
        if (!swapchains) return "Vulkan Win32 surface captured; waiting for its swapchain lifecycle. No game frames have been processed.";
        if (!presents) return "Vulkan swapchain captured; waiting for an identified game presentation.";
        return issue.empty() ? "Vulkan presentation was intercepted; waiting for a safely identified game child window." : issue;
    }
};
} // namespace BsiShaders
