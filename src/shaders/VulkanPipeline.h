#pragma once
#include "Parameters.h"
#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <vulkan/vulkan_core.h>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace BsiShaders {
// Resolve per VkDevice, never by guessing a vendor/ICD or reusing another
// device's function pointers. No Vulkan loader/proxy is linked or shipped.
#define BSI_VULKAN_DEVICE_FUNCTIONS(X) \
 X(CreateImage) X(DestroyImage) X(GetImageMemoryRequirements) X(AllocateMemory) X(FreeMemory) X(BindImageMemory) \
 X(CreateImageView) X(DestroyImageView) X(CreateRenderPass) X(DestroyRenderPass) X(CreateFramebuffer) X(DestroyFramebuffer) \
 X(CreateDescriptorSetLayout) X(DestroyDescriptorSetLayout) X(CreateDescriptorPool) X(DestroyDescriptorPool) \
 X(AllocateDescriptorSets) X(UpdateDescriptorSets) X(CreateSampler) X(DestroySampler) \
 X(CreatePipelineLayout) X(DestroyPipelineLayout) X(CreateShaderModule) X(DestroyShaderModule) \
 X(CreateGraphicsPipelines) X(DestroyPipeline) X(CreateCommandPool) X(DestroyCommandPool) X(AllocateCommandBuffers) \
 X(ResetCommandBuffer) X(BeginCommandBuffer) X(EndCommandBuffer) X(CmdPipelineBarrier) X(CmdCopyImage) \
 X(CmdBeginRenderPass) X(CmdEndRenderPass) X(CmdBindPipeline) X(CmdBindDescriptorSets) X(CmdPushConstants) \
 X(CmdSetViewport) X(CmdSetScissor) X(CmdDraw) X(CreateFence) X(DestroyFence) X(WaitForFences) X(ResetFences) \
 X(CreateSemaphore) X(DestroySemaphore) X(QueueSubmit) X(QueueWaitIdle) X(DeviceWaitIdle)
struct VulkanApi {
    VkDevice device=VK_NULL_HANDLE;
#define BSI_DECLARE_VK(name) PFN_vk##name name=nullptr;
    BSI_VULKAN_DEVICE_FUNCTIONS(BSI_DECLARE_VK)
#undef BSI_DECLARE_VK
    bool load(VkDevice dev,PFN_vkGetDeviceProcAddr resolve,std::string *error);
};
struct VulkanTarget {
    VulkanApi api;
    VkPhysicalDeviceMemoryProperties memory{};
    VkFormat format=VK_FORMAT_UNDEFINED;
    VkExtent2D extent{};
    std::uint32_t queueFamily=0;
    std::vector<VkImage> images;
    VkImageLayout restingLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
};
// One pipeline per identified swapchain. All commands go into OUR command
// buffers: the application's command state is never patched or reset.
class VulkanPipeline {
public:
    VulkanPipeline();
    ~VulkanPipeline();
    VulkanPipeline(const VulkanPipeline&)=delete;
    VulkanPipeline &operator=(const VulkanPipeline&)=delete;
    static bool supports(VkFormat format);
    bool initialize(const VulkanTarget &target,std::string *error);
    // Call only after this swapchain image has been (re)acquired by the app.
    // The per-image semaphore is not reused before WSI has finished its wait.
    // On success, present must wait on *completed INSTEAD OF consuming waits a
    // second time. Neutral bypass returns a null semaphore without API calls.
    bool process(VkQueue queue,std::uint32_t image,const std::vector<VkSemaphore> &waits,
                 const Parameters &settings,VkSemaphore *completed,std::string *error);
    void release(); // while VkDevice is alive; drains only our pending work
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace BsiShaders
