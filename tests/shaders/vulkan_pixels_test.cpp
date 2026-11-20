// Actual native Vulkan commands on an offscreen image. Not ANGLE/GLES, not a
// mock, and NOT a physical-GPU or emulator/WSI presentation certification.
#include "VulkanPipeline.h"
#include "VulkanTextureQuality.h"
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#endif
using namespace BsiShaders;
namespace {
std::atomic<unsigned> validationErrors{0};
VKAPI_ATTR VkBool32 VKAPI_CALL validation(VkDebugUtilsMessageSeverityFlagBitsEXT severity,VkDebugUtilsMessageTypeFlagsEXT,
                                         const VkDebugUtilsMessengerCallbackDataEXT *data,void*){
    if(severity&VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT){++validationErrors;std::cerr<<"VULKAN VALIDATION ERROR: "<<(data&&data->pMessage?data->pMessage:"unknown")<<'\n';}
    return VK_FALSE;
}
void require(bool condition,const std::string &what){if(!condition){std::cerr<<"FAIL: "<<what<<'\n';std::exit(1);}}
void okay(VkResult value,const char *what){if(value!=VK_SUCCESS){std::cerr<<"FAIL: "<<what<<" VkResult="<<value<<'\n';std::exit(1);}}
template<typename T>T info(VkStructureType type){T v{};v.sType=type;return v;}
template<typename T>T instanceProc(PFN_vkGetInstanceProcAddr resolver,VkInstance instance,const char *name){
    auto p=reinterpret_cast<T>(resolver(instance,name));require(p!=nullptr,name);return p;
}
template<typename T>T deviceProc(PFN_vkGetDeviceProcAddr resolver,VkDevice device,const char *name){
    auto p=reinterpret_cast<T>(resolver(device,name));require(p!=nullptr,name);return p;
}
}
int main(){
    const char *path=std::getenv("BSI_SHADERS_VULKAN_LIBRARY");
#ifdef _WIN32
    HMODULE library=path?LoadLibraryA(path):LoadLibraryExW(L"vulkan-1.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    auto get=reinterpret_cast<PFN_vkGetInstanceProcAddr>(library?GetProcAddress(library,"vkGetInstanceProcAddr"):nullptr);
#else
    void *library=dlopen(path?path:"libvulkan.so.1",RTLD_NOW|RTLD_LOCAL);
    auto get=reinterpret_cast<PFN_vkGetInstanceProcAddr>(library?dlsym(library,"vkGetInstanceProcAddr"):nullptr);
#endif
    require(get!=nullptr,"a native Vulkan loader/driver is mandatory, never silently skip");
    auto create=instanceProc<PFN_vkCreateInstance>(get,VK_NULL_HANDLE,"vkCreateInstance");
    auto app=info<VkApplicationInfo>(VK_STRUCTURE_TYPE_APPLICATION_INFO);app.pApplicationName="BSI shader pixel test";app.apiVersion=VK_API_VERSION_1_0;
    auto instanceInfo=info<VkInstanceCreateInfo>(VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO);instanceInfo.pApplicationInfo=&app;
    const bool validate=std::getenv("BSI_SHADERS_VULKAN_VALIDATION")!=nullptr;
    const char *layer="VK_LAYER_KHRONOS_validation",*extension=VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
    auto debug=info<VkDebugUtilsMessengerCreateInfoEXT>(VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT);
    debug.messageSeverity=VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;debug.messageType=VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;debug.pfnUserCallback=validation;
    if(validate){instanceInfo.enabledLayerCount=1;instanceInfo.ppEnabledLayerNames=&layer;instanceInfo.enabledExtensionCount=1;instanceInfo.ppEnabledExtensionNames=&extension;instanceInfo.pNext=&debug;}
    VkInstance instance=VK_NULL_HANDLE;okay(create(&instanceInfo,nullptr,&instance),"create instance (required validation layer missing is a failure)");
    VkDebugUtilsMessengerEXT messenger=VK_NULL_HANDLE;
    if(validate)okay(instanceProc<PFN_vkCreateDebugUtilsMessengerEXT>(get,instance,"vkCreateDebugUtilsMessengerEXT")(instance,&debug,nullptr,&messenger),"create mandatory validation messenger");
    const auto destroyInstance=instanceProc<PFN_vkDestroyInstance>(get,instance,"vkDestroyInstance");
    const auto enumerate=instanceProc<PFN_vkEnumeratePhysicalDevices>(get,instance,"vkEnumeratePhysicalDevices");
    const auto properties=instanceProc<PFN_vkGetPhysicalDeviceProperties>(get,instance,"vkGetPhysicalDeviceProperties");
    const auto memoryProperties=instanceProc<PFN_vkGetPhysicalDeviceMemoryProperties>(get,instance,"vkGetPhysicalDeviceMemoryProperties");
    const auto queueProperties=instanceProc<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(get,instance,"vkGetPhysicalDeviceQueueFamilyProperties");
    const auto createDevice=instanceProc<PFN_vkCreateDevice>(get,instance,"vkCreateDevice");
    std::uint32_t count=0;okay(enumerate(instance,&count,nullptr),"count physical devices");require(count>0,"a Vulkan device must exist");
    std::vector<VkPhysicalDevice> devices(count);okay(enumerate(instance,&count,devices.data()),"enumerate physical devices");
    const auto physical=devices[0];VkPhysicalDeviceProperties props{};properties(physical,&props);
    std::cout<<"Native Vulkan test device: "<<props.deviceName<<", vendor="<<props.vendorID<<", API="<<VK_VERSION_MAJOR(props.apiVersion)<<'.'<<VK_VERSION_MINOR(props.apiVersion)<<'\n';
    VkPhysicalDeviceMemoryProperties memory{};memoryProperties(physical,&memory);
    queueProperties(physical,&count,nullptr);std::vector<VkQueueFamilyProperties> families(count);queueProperties(physical,&count,families.data());
    std::uint32_t family=~0u;for(std::uint32_t i=0;i<count;++i)if(families[i].queueFlags&VK_QUEUE_GRAPHICS_BIT){family=i;break;}
    require(family!=~0u,"a graphics queue is mandatory");
    float priority=1;auto queueInfo=info<VkDeviceQueueCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO);queueInfo.queueFamilyIndex=family;queueInfo.queueCount=1;queueInfo.pQueuePriorities=&priority;
    auto deviceInfo=info<VkDeviceCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO);deviceInfo.queueCreateInfoCount=1;deviceInfo.pQueueCreateInfos=&queueInfo;
    VkDevice device=VK_NULL_HANDLE;okay(createDevice(physical,&deviceInfo,nullptr,&device),"create device");
    const auto resolve=instanceProc<PFN_vkGetDeviceProcAddr>(get,instance,"vkGetDeviceProcAddr");
#define EXTRA(name) const auto name=deviceProc<PFN_vk##name>(resolve,device,"vk" #name)
    EXTRA(DestroyDevice);EXTRA(GetDeviceQueue);EXTRA(CreateBuffer);EXTRA(DestroyBuffer);EXTRA(GetBufferMemoryRequirements);EXTRA(BindBufferMemory);EXTRA(MapMemory);EXTRA(UnmapMemory);EXTRA(CmdCopyBufferToImage);EXTRA(CmdCopyImageToBuffer);
#undef EXTRA
    VkQueue queue=VK_NULL_HANDLE;GetDeviceQueue(device,family,0,&queue);
    VulkanApi g;std::string error;require(g.load(device,resolve,&error),error);
    // Descriptor/API creation proof only. These explicit test-fixture premises
    // are not evidence that a real game's resources/profile are qualified.
    TextureQualityProof quality;quality.renderer=Renderer::Vulkan;quality.requested=RendererFamily::Vulkan;
    quality.use=TextureUse::SampledColor;quality.visibleMipLevels=7;
    quality.gameSurfaceIdentified=quality.resourceOwnershipProven=quality.privateReplacementSupported=quality.externalOverridesReviewed=true;
    auto nativeSampler=info<VkSamplerCreateInfo>(VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO);
    nativeSampler.minFilter=nativeSampler.magFilter=VK_FILTER_LINEAR;nativeSampler.mipmapMode=VK_SAMPLER_MIPMAP_MODE_LINEAR;
    nativeSampler.addressModeU=nativeSampler.addressModeV=nativeSampler.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    nativeSampler.maxLod=6;
    for(auto mode:{TextureQuality::Native,TextureQuality::Light,TextureQuality::Strong}){
        const auto planned=planVulkanTextureQuality(mode,quality,nativeSampler);
        require(planned.descriptor.minLod==(mode==TextureQuality::Native?0.f:mode==TextureQuality::Light?1.5f:3.f),"bounded Vulkan quality sampler description");
        VkSampler sampler=VK_NULL_HANDLE;okay(g.CreateSampler(device,&planned.descriptor,nullptr,&sampler),"create private experimental minimum-LOD sampler");
        g.DestroySampler(device,sampler,nullptr);require(nativeSampler.minLod==0&&nativeSampler.mipLodBias==0,"original Vulkan sampler descriptor remains untouched");
    }
    std::cout<<"PASS: experimental Vulkan minimum-LOD descriptor creates/destroys actual samplers; not a quality pixel or game-binding test\n";
    auto memoryType=[&](std::uint32_t bits,VkMemoryPropertyFlags flags){
        for(std::uint32_t i=0;i<memory.memoryTypeCount;++i)if((bits&(1u<<i))&&(memory.memoryTypes[i].propertyFlags&flags)==flags)return i;
        require(false,"required memory type unavailable");return 0u;
    };
    const std::uint32_t width=64,height=64;const auto size=width*height*4;
    VkBuffer staging=VK_NULL_HANDLE;VkDeviceMemory stagingMemory=VK_NULL_HANDLE,imageMemory=VK_NULL_HANDLE;VkImage image=VK_NULL_HANDLE;
    auto bufferInfo=info<VkBufferCreateInfo>(VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);bufferInfo.size=size;bufferInfo.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    okay(CreateBuffer(device,&bufferInfo,nullptr,&staging),"create staging buffer");
    VkMemoryRequirements needed{};GetBufferMemoryRequirements(device,staging,&needed);
    auto allocation=info<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);allocation.allocationSize=needed.size;allocation.memoryTypeIndex=memoryType(needed.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    okay(g.AllocateMemory(device,&allocation,nullptr,&stagingMemory),"allocate staging memory");okay(BindBufferMemory(device,staging,stagingMemory,0),"bind staging memory");
    void *mapped=nullptr;okay(MapMemory(device,stagingMemory,0,size,0,&mapped),"map staging memory");
    auto imageInfo=info<VkImageCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);imageInfo.imageType=VK_IMAGE_TYPE_2D;imageInfo.format=VK_FORMAT_R8G8B8A8_UNORM;imageInfo.extent={width,height,1};imageInfo.mipLevels=imageInfo.arrayLayers=1;imageInfo.samples=VK_SAMPLE_COUNT_1_BIT;
    imageInfo.usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    okay(g.CreateImage(device,&imageInfo,nullptr,&image),"create test image");g.GetImageMemoryRequirements(device,image,&needed);
    allocation.allocationSize=needed.size;allocation.memoryTypeIndex=memoryType(needed.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    okay(g.AllocateMemory(device,&allocation,nullptr,&imageMemory),"allocate image memory");okay(g.BindImageMemory(device,image,imageMemory,0),"bind image memory");
    VkCommandPool pool=VK_NULL_HANDLE;auto poolInfo=info<VkCommandPoolCreateInfo>(VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO);poolInfo.queueFamilyIndex=family;poolInfo.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    okay(g.CreateCommandPool(device,&poolInfo,nullptr,&pool),"create test command pool");
    VkCommandBuffer command=VK_NULL_HANDLE;auto commandInfo=info<VkCommandBufferAllocateInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO);commandInfo.commandPool=pool;commandInfo.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;commandInfo.commandBufferCount=1;
    okay(g.AllocateCommandBuffers(device,&commandInfo,&command),"allocate test commands");
    auto transfer=[&](bool upload,VkSemaphore wait){
        okay(g.ResetCommandBuffer(command,0),"reset transfer commands");auto begin=info<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);okay(g.BeginCommandBuffer(command,&begin),"begin transfer");
        auto barrier=info<VkImageMemoryBarrier>(VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER);barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;barrier.image=image;barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        barrier.oldLayout=upload?VK_IMAGE_LAYOUT_UNDEFINED:VK_IMAGE_LAYOUT_GENERAL;barrier.newLayout=upload?VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcAccessMask=upload?0:VK_ACCESS_MEMORY_WRITE_BIT;barrier.dstAccessMask=upload?VK_ACCESS_TRANSFER_WRITE_BIT:VK_ACCESS_TRANSFER_READ_BIT;
        g.CmdPipelineBarrier(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
        VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={width,height,1};
        if(upload)CmdCopyBufferToImage(command,staging,image,barrier.newLayout,1,&copy);else CmdCopyImageToBuffer(command,image,barrier.newLayout,staging,1,&copy);
        barrier.oldLayout=barrier.newLayout;barrier.newLayout=VK_IMAGE_LAYOUT_GENERAL;barrier.srcAccessMask=barrier.dstAccessMask;barrier.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
        g.CmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&barrier);
        okay(g.EndCommandBuffer(command),"end transfer");auto submit=info<VkSubmitInfo>(VK_STRUCTURE_TYPE_SUBMIT_INFO);const VkPipelineStageFlags stage=VK_PIPELINE_STAGE_TRANSFER_BIT;
        submit.commandBufferCount=1;submit.pCommandBuffers=&command;if(wait){submit.waitSemaphoreCount=1;submit.pWaitSemaphores=&wait;submit.pWaitDstStageMask=&stage;}
        okay(g.QueueSubmit(queue,1,&submit,VK_NULL_HANDLE),"submit transfer");okay(g.QueueWaitIdle(queue),"complete transfer");
    };
    VulkanTarget target;target.api=g;target.memory=memory;target.format=imageInfo.format;target.extent={width,height};target.queueFamily=family;target.images={image};target.restingLayout=VK_IMAGE_LAYOUT_GENERAL;
    VulkanPipeline pipeline;require(pipeline.initialize(target,&error),error);
    std::vector<unsigned char> flat(size);for(std::uint32_t i=0;i<size;i+=4){flat[i]=204;flat[i+1]=102;flat[i+2]=51;flat[i+3]=173;}
    auto run=[&](const std::vector<unsigned char> &input,const Parameters &settings){
        std::memcpy(mapped,input.data(),size);transfer(true,VK_NULL_HANDLE);VkSemaphore done=VK_NULL_HANDLE;
        require(pipeline.process(queue,0,{},settings,&done,&error),error);
        require((done==VK_NULL_HANDLE)==settings.neutral(),"neutral bypass submits no GPU pass/semaphore");
        transfer(false,done);return std::vector<unsigned char>(static_cast<unsigned char*>(mapped),static_cast<unsigned char*>(mapped)+size);
    };
    Parameters p;p.enabled=true;require(run(flat,p)==flat,"neutral must be byte-exact");p.values.fill(40);p.enabled=false;require(run(flat,p)==flat,"disabled must be byte-exact");
    for(std::size_t control=0;control<6;++control){
        for(int weight:{controls[control].minimum,controls[control].maximum}){
            p=Parameters{};p.enabled=true;p.values[control]=weight;const auto pixels=run(flat,p);const auto expected=grade({204.f/255.f,102.f/255.f,51.f/255.f},p);
            for(int channel=0;channel<3;++channel)require(std::abs(int(pixels[channel])-int(std::lround(expected[channel]*255)))<=1,"native Vulkan color pixels must match CPU reference math");
            for(std::size_t i=3;i<pixels.size();i+=4)require(pixels[i]==173,"source alpha must survive");
        }
    }
    std::vector<unsigned char> pattern(size);
    for(std::uint32_t y=0;y<height;++y)for(std::uint32_t x=0;x<width;++x){
        float v=.15f+.65f/(1.f+std::exp(std::fmax(-50.f,std::fmin(50.f,(float(y)-x)*.8f))));if(x>27&&x<35&&y>27&&y<35)v=1;
        auto i=(y*width+x)*4;pattern[i]=static_cast<unsigned char>(std::lround(v*255));pattern[i+1]=static_cast<unsigned char>(std::lround(v*.8f*255));pattern[i+2]=static_cast<unsigned char>(std::lround(v*.6f*255));pattern[i+3]=173;
    }
    for(std::size_t control=6;control<10;++control){p=Parameters{};p.enabled=true;p.values[control]=85;require(run(pattern,p)!=pattern,"every spatial Vulkan effect must change actual image pixels");}
    VkSemaphore done=VK_NULL_HANDLE;p.values[0]=101;require(!pipeline.process(queue,0,{},p,&done,&error)&&!done,"out-of-range values cannot submit");
    p.values[0]=20;require(!pipeline.process(queue,5,{},p,&done,&error)&&!done,"unknown image index cannot submit");
    pipeline.release();require(pipeline.initialize(target,&error),error);p=Parameters{};p.enabled=true;p.values[2]=40;require(run(flat,p)!=flat,"resource recreation must still change pixels");pipeline.release();
    g.DestroyCommandPool(device,pool,nullptr);g.DestroyImage(device,image,nullptr);g.FreeMemory(device,imageMemory,nullptr);UnmapMemory(device,stagingMemory);DestroyBuffer(device,staging,nullptr);g.FreeMemory(device,stagingMemory,nullptr);
    DestroyDevice(device,nullptr);
    if(messenger)instanceProc<PFN_vkDestroyDebugUtilsMessengerEXT>(get,instance,"vkDestroyDebugUtilsMessengerEXT")(instance,messenger,nullptr);
    destroyInstance(instance,nullptr);require(validationErrors.load()==0,"Khronos validation reported API/synchronization/resource-lifetime errors");
    if(validate)std::cout<<"PASS: required Khronos validation layer reports zero Vulkan errors, including device resource destruction\n";
    std::cout<<"PASS: native Vulkan production pipeline renders all 10 effects to real image pixels; neutral byte identity, reference math, alpha, semaphore handoff/reuse, parameter bounds, invalid image rejection and resource recreation\n";
    std::cout<<"LIMIT: offscreen API test only; Win32 surface/presentation hooks, native player integration and physical Intel/AMD/NVIDIA validation remain separate requirements\n";
}
