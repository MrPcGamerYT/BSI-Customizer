#include "VulkanPipeline.h"
#include "VulkanSpirv.h"
#include <array>
#include <cstdint>
#include <limits>

namespace BsiShaders {
namespace {
template<typename T> T vkInfo(VkStructureType type){T value{};value.sType=type;return value;}
bool result(VkResult value,const char *operation,std::string *error){
    if(value==VK_SUCCESS)return true;
    if(error)*error=std::string(operation)+" failed (VkResult "+std::to_string(value)+"); original presentation retained.";
    return false;
}
VkImageMemoryBarrier barrier(VkImage image,VkImageLayout from,VkImageLayout to,VkAccessFlags read,VkAccessFlags write){
    auto b=vkInfo<VkImageMemoryBarrier>(VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER);
    b.srcAccessMask=read;b.dstAccessMask=write;b.oldLayout=from;b.newLayout=to;
    b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.image=image;
    b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};return b;
}
}
bool VulkanApi::load(VkDevice dev,PFN_vkGetDeviceProcAddr resolve,std::string *error){
    if(!dev||!resolve||!error)return false;
    device=dev;
#define BSI_LOAD_VK(name) name=reinterpret_cast<PFN_vk##name>(resolve(dev,"vk" #name)); if(!name){*error="Vulkan device entry point unavailable: vk" #name;return false;}
    BSI_VULKAN_DEVICE_FUNCTIONS(BSI_LOAD_VK)
#undef BSI_LOAD_VK
    return true;
}
struct VulkanPipeline::Impl {
    VulkanTarget target;
    VkRenderPass pass=VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorLayout=VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool=VK_NULL_HANDLE;
    VkPipelineLayout layout=VK_NULL_HANDLE;
    VkPipeline pipeline=VK_NULL_HANDLE;
    VkSampler sampler=VK_NULL_HANDLE;
    VkCommandPool commands=VK_NULL_HANDLE;
    bool ready=false;
    struct Frame {
        VkImage copy=VK_NULL_HANDLE;
        VkDeviceMemory memory=VK_NULL_HANDLE;
        VkImageView copyView=VK_NULL_HANDLE,targetView=VK_NULL_HANDLE;
        VkFramebuffer framebuffer=VK_NULL_HANDLE;
        VkDescriptorSet descriptor=VK_NULL_HANDLE;
        VkCommandBuffer command=VK_NULL_HANDLE;
        VkSemaphore done=VK_NULL_HANDLE;
        VkFence fence=VK_NULL_HANDLE;
        bool submitted=false;
    };
    std::vector<Frame> frames;
};
VulkanPipeline::VulkanPipeline():impl(new Impl){}
VulkanPipeline::~VulkanPipeline(){release();}
bool VulkanPipeline::supports(VkFormat format){
    return format==VK_FORMAT_R8G8B8A8_UNORM||format==VK_FORMAT_B8G8R8A8_UNORM
        ||format==VK_FORMAT_R8G8B8A8_SRGB||format==VK_FORMAT_B8G8R8A8_SRGB;
}
bool VulkanPipeline::initialize(const VulkanTarget &target,std::string *error){
    if(!error)return false;
    if(impl->ready)return true;
    if(!supports(target.format)||!target.api.device||target.images.empty()||target.images.size()>8
       ||!target.extent.width||!target.extent.height
       ||std::uint64_t(target.extent.width)*target.extent.height*target.images.size()>134217728
       ||(target.restingLayout!=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR&&target.restingLayout!=VK_IMAGE_LAYOUT_GENERAL)){
        *error="Unsupported Vulkan SDR format, image count, layout or surface dimensions; no game pixels changed.";return false;
    }
    // All required functions are loaded before allocating anything. Partial
    // failures are owned by this object and are freed on the next retry/destruct.
    release();impl->target=target;auto &p=*impl;auto &g=p.target.api;const auto d=g.device;
    p.frames.resize(target.images.size());
    auto pool=vkInfo<VkCommandPoolCreateInfo>(VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO);
    pool.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;pool.queueFamilyIndex=target.queueFamily;
    if(!result(g.CreateCommandPool(d,&pool,nullptr,&p.commands),"vkCreateCommandPool",error))return false;
    auto descriptor=vkInfo<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
    const VkDescriptorSetLayoutBinding binding{0,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr};
    descriptor.bindingCount=1;descriptor.pBindings=&binding;
    if(!result(g.CreateDescriptorSetLayout(d,&descriptor,nullptr,&p.descriptorLayout),"vkCreateDescriptorSetLayout",error))return false;
    const auto count=std::uint32_t(p.frames.size());
    const VkDescriptorPoolSize descriptorSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,count};
    auto descriptors=vkInfo<VkDescriptorPoolCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);
    descriptors.maxSets=count;descriptors.poolSizeCount=1;descriptors.pPoolSizes=&descriptorSize;
    if(!result(g.CreateDescriptorPool(d,&descriptors,nullptr,&p.descriptorPool),"vkCreateDescriptorPool",error))return false;
    auto sampling=vkInfo<VkSamplerCreateInfo>(VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO);
    sampling.magFilter=sampling.minFilter=VK_FILTER_LINEAR;sampling.mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampling.addressModeU=sampling.addressModeV=sampling.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if(!result(g.CreateSampler(d,&sampling,nullptr,&p.sampler),"vkCreateSampler",error))return false;
    const VkPushConstantRange constants{VK_SHADER_STAGE_FRAGMENT_BIT,0,48};
    auto layout=vkInfo<VkPipelineLayoutCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
    layout.setLayoutCount=1;layout.pSetLayouts=&p.descriptorLayout;layout.pushConstantRangeCount=1;layout.pPushConstantRanges=&constants;
    if(!result(g.CreatePipelineLayout(d,&layout,nullptr,&p.layout),"vkCreatePipelineLayout",error))return false;
    VkAttachmentDescription attachment{};attachment.format=target.format;attachment.samples=VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;attachment.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;attachment.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;attachment.finalLayout=target.restingLayout;
    const VkAttachmentReference reference{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};subpass.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount=1;subpass.pColorAttachments=&reference;
    VkSubpassDependency end{};end.srcSubpass=0;end.dstSubpass=VK_SUBPASS_EXTERNAL;
    end.srcStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;end.dstStageMask=VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    end.srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;end.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT;
    auto pass=vkInfo<VkRenderPassCreateInfo>(VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO);
    pass.attachmentCount=1;pass.pAttachments=&attachment;pass.subpassCount=1;pass.pSubpasses=&subpass;
    pass.dependencyCount=1;pass.pDependencies=&end;
    if(!result(g.CreateRenderPass(d,&pass,nullptr,&p.pass),"vkCreateRenderPass",error))return false;
    VkShaderModule modules[2]{};
    auto code=vkInfo<VkShaderModuleCreateInfo>(VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO);
    code.codeSize=sizeof(vulkanVertexSpirv);code.pCode=vulkanVertexSpirv;
    if(!result(g.CreateShaderModule(d,&code,nullptr,&modules[0]),"vkCreateShaderModule(vertex)",error))return false;
    code.codeSize=sizeof(vulkanFragmentSpirv);code.pCode=vulkanFragmentSpirv;
    const auto fragmentResult=g.CreateShaderModule(d,&code,nullptr,&modules[1]);
    if(!result(fragmentResult,"vkCreateShaderModule(fragment)",error)){g.DestroyShaderModule(d,modules[0],nullptr);return false;}
    std::array<VkPipelineShaderStageCreateInfo,2> stages{};
    for(unsigned i=0;i<2;++i){stages[i]=vkInfo<VkPipelineShaderStageCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);stages[i].stage=i?VK_SHADER_STAGE_FRAGMENT_BIT:VK_SHADER_STAGE_VERTEX_BIT;stages[i].module=modules[i];stages[i].pName="main";}
    auto input=vkInfo<VkPipelineVertexInputStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO);
    auto assembly=vkInfo<VkPipelineInputAssemblyStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO);assembly.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    auto viewport=vkInfo<VkPipelineViewportStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO);viewport.viewportCount=viewport.scissorCount=1;
    auto raster=vkInfo<VkPipelineRasterizationStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO);raster.polygonMode=VK_POLYGON_MODE_FILL;raster.lineWidth=1.f;
    auto samples=vkInfo<VkPipelineMultisampleStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO);samples.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState output{};output.colorWriteMask=0xf;
    auto blend=vkInfo<VkPipelineColorBlendStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO);blend.attachmentCount=1;blend.pAttachments=&output;
    const VkDynamicState dynamicStates[]{VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};
    auto dynamic=vkInfo<VkPipelineDynamicStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO);dynamic.dynamicStateCount=2;dynamic.pDynamicStates=dynamicStates;
    auto pipeline=vkInfo<VkGraphicsPipelineCreateInfo>(VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO);
    pipeline.stageCount=2;pipeline.pStages=stages.data();pipeline.pVertexInputState=&input;pipeline.pInputAssemblyState=&assembly;
    pipeline.pViewportState=&viewport;pipeline.pRasterizationState=&raster;pipeline.pMultisampleState=&samples;
    pipeline.pColorBlendState=&blend;pipeline.pDynamicState=&dynamic;pipeline.layout=p.layout;pipeline.renderPass=p.pass;
    const auto built=g.CreateGraphicsPipelines(d,VK_NULL_HANDLE,1,&pipeline,nullptr,&p.pipeline);
    for(auto shader:modules)g.DestroyShaderModule(d,shader,nullptr);
    if(!result(built,"vkCreateGraphicsPipelines",error))return false;
    for(std::uint32_t i=0;i<count;++i){
        auto &f=p.frames[i];
        auto image=vkInfo<VkImageCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);image.imageType=VK_IMAGE_TYPE_2D;
        image.format=target.format;image.extent={target.extent.width,target.extent.height,1};image.mipLevels=image.arrayLayers=1;
        image.samples=VK_SAMPLE_COUNT_1_BIT;image.tiling=VK_IMAGE_TILING_OPTIMAL;
        image.usage=VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT;image.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
        if(!result(g.CreateImage(d,&image,nullptr,&f.copy),"vkCreateImage",error))return false;
        VkMemoryRequirements required{};g.GetImageMemoryRequirements(d,f.copy,&required);
        std::uint32_t memory=~0u;
        for(std::uint32_t j=0;j<target.memory.memoryTypeCount;++j)
            if((required.memoryTypeBits&(1u<<j))&&(target.memory.memoryTypes[j].propertyFlags&VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)){memory=j;break;}
        if(memory==~0u){*error="No compatible device-local Vulkan image memory; shaders bypassed.";return false;}
        auto allocation=vkInfo<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);allocation.allocationSize=required.size;allocation.memoryTypeIndex=memory;
        if(!result(g.AllocateMemory(d,&allocation,nullptr,&f.memory),"vkAllocateMemory",error)
           ||!result(g.BindImageMemory(d,f.copy,f.memory,0),"vkBindImageMemory",error))return false;
        auto view=vkInfo<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);view.viewType=VK_IMAGE_VIEW_TYPE_2D;
        view.format=target.format;view.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};view.image=f.copy;
        if(!result(g.CreateImageView(d,&view,nullptr,&f.copyView),"vkCreateImageView(copy)",error))return false;
        view.image=target.images[i];
        if(!result(g.CreateImageView(d,&view,nullptr,&f.targetView),"vkCreateImageView(target)",error))return false;
        auto framebuffer=vkInfo<VkFramebufferCreateInfo>(VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO);framebuffer.renderPass=p.pass;
        framebuffer.attachmentCount=1;framebuffer.pAttachments=&f.targetView;framebuffer.width=target.extent.width;framebuffer.height=target.extent.height;framebuffer.layers=1;
        if(!result(g.CreateFramebuffer(d,&framebuffer,nullptr,&f.framebuffer),"vkCreateFramebuffer",error))return false;
        auto set=vkInfo<VkDescriptorSetAllocateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);set.descriptorPool=p.descriptorPool;set.descriptorSetCount=1;set.pSetLayouts=&p.descriptorLayout;
        if(!result(g.AllocateDescriptorSets(d,&set,&f.descriptor),"vkAllocateDescriptorSets",error))return false;
        const VkDescriptorImageInfo source{p.sampler,f.copyView,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        auto write=vkInfo<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);write.dstSet=f.descriptor;write.descriptorCount=1;write.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;write.pImageInfo=&source;
        g.UpdateDescriptorSets(d,1,&write,0,nullptr);
        auto command=vkInfo<VkCommandBufferAllocateInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO);command.commandPool=p.commands;command.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;command.commandBufferCount=1;
        if(!result(g.AllocateCommandBuffers(d,&command,&f.command),"vkAllocateCommandBuffers",error))return false;
        auto fence=vkInfo<VkFenceCreateInfo>(VK_STRUCTURE_TYPE_FENCE_CREATE_INFO);fence.flags=VK_FENCE_CREATE_SIGNALED_BIT;
        auto semaphore=vkInfo<VkSemaphoreCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO);
        if(!result(g.CreateFence(d,&fence,nullptr,&f.fence),"vkCreateFence",error)
           ||!result(g.CreateSemaphore(d,&semaphore,nullptr,&f.done),"vkCreateSemaphore",error))return false;
    }
    p.ready=true;return true;
}
bool VulkanPipeline::process(VkQueue queue,std::uint32_t image,const std::vector<VkSemaphore> &waits,
                             const Parameters &settings,VkSemaphore *completed,std::string *error){
    if(!completed||!error)return false;
    *completed=VK_NULL_HANDLE;
    if(!settings.valid()){*error="Shader parameters are out of range.";return false;}
    if(settings.neutral())return true;
    auto &p=*impl;if(!p.ready||!queue||image>=p.frames.size()||waits.size()>64){*error="Vulkan image/queue is not ready for shader processing.";return false;}
    auto &g=p.target.api;auto &f=p.frames[image];const auto d=g.device;
    if(f.submitted&&!result(g.WaitForFences(d,1,&f.fence,VK_TRUE,1000000000ull),"vkWaitForFences",error))return false;
    if(!result(g.ResetCommandBuffer(f.command,0),"vkResetCommandBuffer",error))return false;
    auto begin=vkInfo<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if(!result(g.BeginCommandBuffer(f.command,&begin),"vkBeginCommandBuffer",error))return false;
    VkImageMemoryBarrier start[]{
        barrier(p.target.images[image],p.target.restingLayout,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_ACCESS_MEMORY_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT),
        barrier(f.copy,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,0,VK_ACCESS_TRANSFER_WRITE_BIT)};
    g.CmdPipelineBarrier(f.command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,2,start);
    VkImageCopy copy{};copy.srcSubresource=copy.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.extent={p.target.extent.width,p.target.extent.height,1};
    g.CmdCopyImage(f.command,p.target.images[image],VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,f.copy,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
    VkImageMemoryBarrier draw[]{
        barrier(p.target.images[image],VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_ACCESS_TRANSFER_READ_BIT,VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT),
        barrier(f.copy,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT)};
    g.CmdPipelineBarrier(f.command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT|VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,0,0,nullptr,0,nullptr,2,draw);
    auto render=vkInfo<VkRenderPassBeginInfo>(VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO);render.renderPass=p.pass;render.framebuffer=f.framebuffer;render.renderArea.extent=p.target.extent;
    g.CmdBeginRenderPass(f.command,&render,VK_SUBPASS_CONTENTS_INLINE);
    g.CmdBindPipeline(f.command,VK_PIPELINE_BIND_POINT_GRAPHICS,p.pipeline);
    g.CmdBindDescriptorSets(f.command,VK_PIPELINE_BIND_POINT_GRAPHICS,p.layout,0,1,&f.descriptor,0,nullptr);
    std::array<float,12> constants{};
    for(std::size_t i=0;i<settings.values.size();++i)constants[i]=settings.values[i]/100.f;
    constants[10]=1.f/p.target.extent.width;constants[11]=1.f/p.target.extent.height;
    g.CmdPushConstants(f.command,p.layout,VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(constants),constants.data());
    const VkViewport viewport{0,0,float(p.target.extent.width),float(p.target.extent.height),0,1};
    const VkRect2D scissor{{0,0},p.target.extent};
    g.CmdSetViewport(f.command,0,1,&viewport);g.CmdSetScissor(f.command,0,1,&scissor);g.CmdDraw(f.command,3,1,0,0);
    g.CmdEndRenderPass(f.command);
    if(!result(g.EndCommandBuffer(f.command),"vkEndCommandBuffer",error))return false;
    std::vector<VkPipelineStageFlags> stages(waits.size(),VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
    auto submission=vkInfo<VkSubmitInfo>(VK_STRUCTURE_TYPE_SUBMIT_INFO);submission.waitSemaphoreCount=std::uint32_t(waits.size());submission.pWaitSemaphores=waits.data();submission.pWaitDstStageMask=stages.data();
    submission.commandBufferCount=1;submission.pCommandBuffers=&f.command;submission.signalSemaphoreCount=1;submission.pSignalSemaphores=&f.done;
    if(!result(g.ResetFences(d,1,&f.fence),"vkResetFences",error))return false;
    const auto submitted=g.QueueSubmit(queue,1,&submission,f.fence);
    f.submitted=submitted==VK_SUCCESS;
    if(!result(submitted,"vkQueueSubmit(shader)",error))return false;
    *completed=f.done;return true;
}
void VulkanPipeline::release(){
    auto &p=*impl;auto &g=p.target.api;const auto d=g.device;if(!d)return;
    for(auto &f:p.frames){
        // No freeing in-flight command buffers/images. If a device is lost,
        // Vulkan still permits destruction and the wait returns that error.
        if(f.submitted)g.WaitForFences(d,1,&f.fence,VK_TRUE,std::numeric_limits<std::uint64_t>::max());
        if(f.framebuffer)g.DestroyFramebuffer(d,f.framebuffer,nullptr);
        if(f.copyView)g.DestroyImageView(d,f.copyView,nullptr);
        if(f.targetView)g.DestroyImageView(d,f.targetView,nullptr);
        if(f.copy)g.DestroyImage(d,f.copy,nullptr);
        if(f.memory)g.FreeMemory(d,f.memory,nullptr);
        if(f.fence)g.DestroyFence(d,f.fence,nullptr);
        if(f.done)g.DestroySemaphore(d,f.done,nullptr);
    }
    if(p.commands)g.DestroyCommandPool(d,p.commands,nullptr);
    if(p.pipeline)g.DestroyPipeline(d,p.pipeline,nullptr);
    if(p.pass)g.DestroyRenderPass(d,p.pass,nullptr);
    if(p.descriptorPool)g.DestroyDescriptorPool(d,p.descriptorPool,nullptr);
    if(p.layout)g.DestroyPipelineLayout(d,p.layout,nullptr);
    if(p.descriptorLayout)g.DestroyDescriptorSetLayout(d,p.descriptorLayout,nullptr);
    if(p.sampler)g.DestroySampler(d,p.sampler,nullptr);
    impl.reset(new Impl);
}
} // namespace BsiShaders
