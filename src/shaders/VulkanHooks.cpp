#include "PresentationHooks.h"
#ifdef _WIN32
#include "Runtime.h"
#include "VulkanPipeline.h"
#define VK_USE_PLATFORM_WIN32_KHR
#include <windows.h>
#include <vulkan/vulkan_win32.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>

namespace BsiShaders {
namespace {
#define BSI_VK_INTERCEPTED(X) \
 X(GetInstanceProcAddr) X(GetDeviceProcAddr) X(CreateInstance) X(DestroyInstance) X(EnumeratePhysicalDevices) \
 X(CreateWin32SurfaceKHR) X(DestroySurfaceKHR) X(CreateDevice) X(DestroyDevice) X(GetDeviceQueue) X(GetDeviceQueue2) \
 X(CreateSwapchainKHR) X(DestroySwapchainKHR) X(AcquireNextImageKHR) X(AcquireNextImage2KHR) X(QueuePresentKHR)
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL getInstanceProc(VkInstance,const char*);
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL getDeviceProc(VkDevice,const char*);
VKAPI_ATTR VkResult VKAPI_CALL createInstance(const VkInstanceCreateInfo*,const VkAllocationCallbacks*,VkInstance*);
VKAPI_ATTR void VKAPI_CALL destroyInstance(VkInstance,const VkAllocationCallbacks*);
VKAPI_ATTR VkResult VKAPI_CALL enumeratePhysical(VkInstance,std::uint32_t*,VkPhysicalDevice*);
VKAPI_ATTR VkResult VKAPI_CALL createSurface(VkInstance,const VkWin32SurfaceCreateInfoKHR*,const VkAllocationCallbacks*,VkSurfaceKHR*);
VKAPI_ATTR void VKAPI_CALL destroySurface(VkInstance,VkSurfaceKHR,const VkAllocationCallbacks*);
VKAPI_ATTR VkResult VKAPI_CALL createDevice(VkPhysicalDevice,const VkDeviceCreateInfo*,const VkAllocationCallbacks*,VkDevice*);
VKAPI_ATTR void VKAPI_CALL destroyDevice(VkDevice,const VkAllocationCallbacks*);
VKAPI_ATTR void VKAPI_CALL getQueue(VkDevice,std::uint32_t,std::uint32_t,VkQueue*);
VKAPI_ATTR void VKAPI_CALL getQueue2(VkDevice,const VkDeviceQueueInfo2*,VkQueue*);
VKAPI_ATTR VkResult VKAPI_CALL createSwapchain(VkDevice,const VkSwapchainCreateInfoKHR*,const VkAllocationCallbacks*,VkSwapchainKHR*);
VKAPI_ATTR void VKAPI_CALL destroySwapchain(VkDevice,VkSwapchainKHR,const VkAllocationCallbacks*);
VKAPI_ATTR VkResult VKAPI_CALL acquire(VkDevice,VkSwapchainKHR,std::uint64_t,VkSemaphore,VkFence,std::uint32_t*);
VKAPI_ATTR VkResult VKAPI_CALL acquire2(VkDevice,const VkAcquireNextImageInfoKHR*,std::uint32_t*);
VKAPI_ATTR VkResult VKAPI_CALL present(VkQueue,const VkPresentInfoKHR*);
struct Entry {const char *name;void *callback;void *target=nullptr;};
Entry entries[]{
    {"vkGetInstanceProcAddr",reinterpret_cast<void*>(&getInstanceProc)}, {"vkGetDeviceProcAddr",reinterpret_cast<void*>(&getDeviceProc)},
    {"vkCreateInstance",reinterpret_cast<void*>(&createInstance)}, {"vkDestroyInstance",reinterpret_cast<void*>(&destroyInstance)},
    {"vkEnumeratePhysicalDevices",reinterpret_cast<void*>(&enumeratePhysical)}, {"vkCreateWin32SurfaceKHR",reinterpret_cast<void*>(&createSurface)},
    {"vkDestroySurfaceKHR",reinterpret_cast<void*>(&destroySurface)}, {"vkCreateDevice",reinterpret_cast<void*>(&createDevice)},
    {"vkDestroyDevice",reinterpret_cast<void*>(&destroyDevice)}, {"vkGetDeviceQueue",reinterpret_cast<void*>(&getQueue)},
    {"vkGetDeviceQueue2",reinterpret_cast<void*>(&getQueue2)}, {"vkCreateSwapchainKHR",reinterpret_cast<void*>(&createSwapchain)},
    {"vkDestroySwapchainKHR",reinterpret_cast<void*>(&destroySwapchain)}, {"vkAcquireNextImageKHR",reinterpret_cast<void*>(&acquire)},
    {"vkAcquireNextImage2KHR",reinterpret_cast<void*>(&acquire2)}, {"vkQueuePresentKHR",reinterpret_cast<void*>(&present)}
};
template<typename Fn>Fn original(const char *name){
    for(const auto &e:entries)if(std::strcmp(e.name,name)==0){auto trampoline=originalPresentationFunction(e.target);return reinterpret_cast<Fn>(trampoline?trampoline:e.target);}
    return nullptr;
}
struct Instance {VkInstance handle=VK_NULL_HANDLE;};
struct Swapchain {
    HWND window=nullptr;
    VkSwapchainCreateInfoKHR info{};
    bool eligible=false,failed=false;
    std::string diagnostic;
    std::vector<VkImage> images;
    std::vector<bool> acquired;
    std::unique_ptr<VulkanPipeline> pipeline;
    std::uint32_t family=~0u;
};
struct Device {
    std::mutex mutex;
    VkDevice handle=VK_NULL_HANDLE;
    VkPhysicalDevice physical=VK_NULL_HANDLE;
    std::shared_ptr<Instance> instance;
    VulkanApi api;
    VkPhysicalDeviceMemoryProperties memory{};
    std::vector<VkQueueFamilyProperties> families;
    std::vector<std::uint32_t> graphicsFamilies;
    std::map<VkSwapchainKHR,std::shared_ptr<Swapchain>> chains;
};
struct Queue {std::shared_ptr<Device> device;std::uint32_t family=~0u;bool protectedQueue=false;};
struct State {
    std::mutex mutex;
    std::atomic<bool> stopping{false};bool installed=false,addressesReady=false;
    std::map<VkInstance,std::shared_ptr<Instance>> instances;
    std::map<VkPhysicalDevice,std::shared_ptr<Instance>> physical;
    std::map<VkDevice,std::shared_ptr<Device>> devices;
    std::map<VkQueue,Queue> queues;
    std::map<VkSurfaceKHR,HWND> surfaces;
};
State &state(){static auto *s=new State;return *s;}
template<typename Fn>Fn instanceFunction(VkInstance instance,const char *name){return reinterpret_cast<Fn>(original<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr")(instance,name));}
PFN_vkVoidFunction VKAPI_CALL resolveDevice(VkDevice device,const char *name){
    auto raw=original<PFN_vkGetDeviceProcAddr>("vkGetDeviceProcAddr")(device,name);
    auto trampoline=originalPresentationFunction(reinterpret_cast<void*>(raw));return trampoline?reinterpret_cast<PFN_vkVoidFunction>(trampoline):raw;
}
std::shared_ptr<Device> deviceFor(VkDevice device){auto &s=state();std::lock_guard<std::mutex> lock(s.mutex);auto it=s.devices.find(device);return it==s.devices.end()?nullptr:it->second;}
void physicalFor(VkInstance instance,std::uint32_t count,const VkPhysicalDevice *physical){
    if(!physical)return;auto &s=state();std::lock_guard<std::mutex> lock(s.mutex);auto found=s.instances.find(instance);if(found==s.instances.end())return;
    for(std::uint32_t i=0;i<count&&s.physical.size()<64;++i)s.physical[physical[i]]=found->second;
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL getInstanceProc(VkInstance instance,const char *name){
    auto raw=original<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr")(instance,name);
    if(raw&&name)for(const auto &e:entries)if(e.target&&std::strcmp(e.name,name)==0)return reinterpret_cast<PFN_vkVoidFunction>(e.callback);
    return raw;
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL getDeviceProc(VkDevice device,const char *name){
    auto raw=original<PFN_vkGetDeviceProcAddr>("vkGetDeviceProcAddr")(device,name);
    if(raw&&name)for(const auto &e:entries)if(e.target&&std::strcmp(e.name,name)==0)return reinterpret_cast<PFN_vkVoidFunction>(e.callback);
    return raw;
}
VKAPI_ATTR VkResult VKAPI_CALL createInstance(const VkInstanceCreateInfo *info,const VkAllocationCallbacks *alloc,VkInstance *out){
    const auto hr=original<PFN_vkCreateInstance>("vkCreateInstance")(info,alloc,out);
    if(hr==VK_SUCCESS&&out)try {
        auto instance=std::make_shared<Instance>();instance->handle=*out;auto &s=state();
        {std::lock_guard<std::mutex> lock(s.mutex);if(s.instances.size()>=16)return hr;s.instances[*out]=instance;}
        auto enumerate=instanceFunction<PFN_vkEnumeratePhysicalDevices>(*out,"vkEnumeratePhysicalDevices");
        // Resolve through the trampoline if the loader gave us its own export.
        auto trampoline=originalPresentationFunction(reinterpret_cast<void*>(enumerate));if(trampoline)enumerate=reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(trampoline);
        std::uint32_t count=0;if(enumerate&&enumerate(*out,&count,nullptr)==VK_SUCCESS&&count&&count<=64){std::vector<VkPhysicalDevice> physical(count);if(enumerate(*out,&count,physical.data())==VK_SUCCESS)physicalFor(*out,count,physical.data());}
    }catch(...){}
    return hr;
}
VKAPI_ATTR void VKAPI_CALL destroyInstance(VkInstance instance,const VkAllocationCallbacks *alloc){
    {auto &s=state();std::lock_guard<std::mutex> lock(s.mutex);s.instances.erase(instance);for(auto it=s.physical.begin();it!=s.physical.end();)if(it->second->handle==instance)it=s.physical.erase(it);else ++it;}
    original<PFN_vkDestroyInstance>("vkDestroyInstance")(instance,alloc);
}
VKAPI_ATTR VkResult VKAPI_CALL enumeratePhysical(VkInstance instance,std::uint32_t *count,VkPhysicalDevice *out){
    const auto hr=original<PFN_vkEnumeratePhysicalDevices>("vkEnumeratePhysicalDevices")(instance,count,out);if((hr==VK_SUCCESS||hr==VK_INCOMPLETE)&&count&&out)try{physicalFor(instance,*count,out);}catch(...){}
    return hr;
}
VKAPI_ATTR VkResult VKAPI_CALL createSurface(VkInstance instance,const VkWin32SurfaceCreateInfoKHR *info,const VkAllocationCallbacks *alloc,VkSurfaceKHR *out){
    const auto hr=original<PFN_vkCreateWin32SurfaceKHR>("vkCreateWin32SurfaceKHR")(instance,info,alloc,out);
    if(hr==VK_SUCCESS&&out&&info)try{auto &s=state();std::lock_guard<std::mutex> lock(s.mutex);if(s.surfaces.size()<128)s.surfaces[*out]=info->hwnd;}catch(...){}
    return hr;
}
VKAPI_ATTR void VKAPI_CALL destroySurface(VkInstance instance,VkSurfaceKHR surface,const VkAllocationCallbacks *alloc){
    {auto &s=state();std::lock_guard<std::mutex> lock(s.mutex);s.surfaces.erase(surface);}original<PFN_vkDestroySurfaceKHR>("vkDestroySurfaceKHR")(instance,surface,alloc);
}
VKAPI_ATTR VkResult VKAPI_CALL createDevice(VkPhysicalDevice physical,const VkDeviceCreateInfo *info,const VkAllocationCallbacks *alloc,VkDevice *out){
    const auto hr=original<PFN_vkCreateDevice>("vkCreateDevice")(physical,info,alloc,out);
    if(hr==VK_SUCCESS&&out)try{
        auto d=std::make_shared<Device>();d->handle=*out;d->physical=physical;auto &s=state();
        {std::lock_guard<std::mutex> lock(s.mutex);auto it=s.physical.find(physical);if(it==s.physical.end()||s.devices.size()>=32)return hr;d->instance=it->second;}
        std::string error;if(!d->api.load(*out,resolveDevice,&error))return hr;
        const auto instance=d->instance->handle;
        const auto memory=instanceFunction<PFN_vkGetPhysicalDeviceMemoryProperties>(instance,"vkGetPhysicalDeviceMemoryProperties");
        const auto queues=instanceFunction<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(instance,"vkGetPhysicalDeviceQueueFamilyProperties");
        if(!memory||!queues)return hr;memory(physical,&d->memory);std::uint32_t count=0;queues(physical,&count,nullptr);
        if(!count||count>64)return hr;d->families.resize(count);queues(physical,&count,d->families.data());
        if(info)for(std::uint32_t i=0;i<info->queueCreateInfoCount;++i){
            const auto family=info->pQueueCreateInfos[i].queueFamilyIndex;
            if(family<count&&(d->families[family].queueFlags&VK_QUEUE_GRAPHICS_BIT)
               &&std::find(d->graphicsFamilies.begin(),d->graphicsFamilies.end(),family)==d->graphicsFamilies.end())d->graphicsFamilies.push_back(family);
        }
        {std::lock_guard<std::mutex> lock(s.mutex);s.devices[*out]=d;}
    }catch(...){}
    return hr;
}
VKAPI_ATTR void VKAPI_CALL destroyDevice(VkDevice device,const VkAllocationCallbacks *alloc){
    auto d=deviceFor(device);
    if(d){std::lock_guard<std::mutex> lock(d->mutex);d->api.DeviceWaitIdle(device);d->chains.clear();}
    {auto &s=state();std::lock_guard<std::mutex> lock(s.mutex);for(auto it=s.queues.begin();it!=s.queues.end();)if(it->second.device==d)it=s.queues.erase(it);else ++it;s.devices.erase(device);}
    original<PFN_vkDestroyDevice>("vkDestroyDevice")(device,alloc);
}
void recordQueue(VkDevice device,std::uint32_t family,bool protectedQueue,VkQueue *queue){
    auto d=deviceFor(device);if(!queue||!*queue||!d||family>=d->families.size())return;
    auto &s=state();std::lock_guard<std::mutex> lock(s.mutex);if(s.queues.size()<128)s.queues[*queue]={d,family,protectedQueue};
}
VKAPI_ATTR void VKAPI_CALL getQueue(VkDevice device,std::uint32_t family,std::uint32_t index,VkQueue *out){
    original<PFN_vkGetDeviceQueue>("vkGetDeviceQueue")(device,family,index,out);try{recordQueue(device,family,false,out);}catch(...){}
}
VKAPI_ATTR void VKAPI_CALL getQueue2(VkDevice device,const VkDeviceQueueInfo2 *info,VkQueue *out){
    original<PFN_vkGetDeviceQueue2>("vkGetDeviceQueue2")(device,info,out);if(info)try{recordQueue(device,info->queueFamilyIndex,(info->flags&VK_DEVICE_QUEUE_CREATE_PROTECTED_BIT)!=0,out);}catch(...){}
}
VKAPI_ATTR VkResult VKAPI_CALL createSwapchain(VkDevice device,const VkSwapchainCreateInfoKHR *info,const VkAllocationCallbacks *alloc,VkSwapchainKHR *out){
    const auto next=original<PFN_vkCreateSwapchainKHR>("vkCreateSwapchainKHR");auto d=deviceFor(device);HWND window=nullptr;
    if(info){auto &s=state();std::lock_guard<std::mutex> lock(s.mutex);auto it=s.surfaces.find(info->surface);if(it!=s.surfaces.end())window=it->second;}
    VkSwapchainCreateInfoKHR enhanced{};if(info)enhanced=*info;
    bool supported=false;
    try{
        if(info&&d&&Runtime::instance().accepts(Renderer::Vulkan)&&Runtime::instance().eligible(reinterpret_cast<std::uintptr_t>(window))
           &&!info->flags&&info->imageArrayLayers==1&&VulkanPipeline::supports(info->imageFormat)&&info->imageColorSpace==VK_COLOR_SPACE_SRGB_NONLINEAR_KHR){
            const auto capabilities=instanceFunction<PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR>(d->instance->handle,"vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
            const auto formats=instanceFunction<PFN_vkGetPhysicalDeviceFormatProperties>(d->instance->handle,"vkGetPhysicalDeviceFormatProperties");
            VkSurfaceCapabilitiesKHR caps{};VkFormatProperties properties{};const VkImageUsageFlags needed=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
            if(capabilities&&formats&&capabilities(d->physical,info->surface,&caps)==VK_SUCCESS){
                formats(d->physical,info->imageFormat,&properties);
                const VkFormatFeatureFlags features=VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT|VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
                supported=(caps.supportedUsageFlags&needed)==needed&&(properties.optimalTilingFeatures&features)==features;
                if(supported)enhanced.imageUsage|=needed;
            }
        }
    }catch(...){supported=false;}
    auto hr=next(device,supported?&enhanced:info,alloc,out);
    if(hr!=VK_SUCCESS&&supported){supported=false;hr=next(device,info,alloc,out);} // Never sacrifice game startup for effects.
    if(hr==VK_SUCCESS&&out&&d&&info)try{
        auto chain=std::make_shared<Swapchain>();chain->window=window;chain->info=*info;chain->info.pNext=nullptr;chain->info.pQueueFamilyIndices=nullptr;chain->eligible=supported;
        chain->diagnostic="This Vulkan surface was not identified early enough, or lacks the required SDR image usage/features; original game frames retained.";
        if(supported){
            const auto images=reinterpret_cast<PFN_vkGetSwapchainImagesKHR>(resolveDevice(device,"vkGetSwapchainImagesKHR"));std::uint32_t count=0;
            if(!images||images(device,*out,&count,nullptr)!=VK_SUCCESS||!count||count>8)chain->eligible=false;
            else{chain->images.resize(count);if(images(device,*out,&count,chain->images.data())!=VK_SUCCESS)chain->eligible=false;chain->acquired.resize(count,false);}
        }
        std::lock_guard<std::mutex> lock(d->mutex);if(d->chains.size()<32)d->chains[*out]=chain;
    }catch(...){}
    return hr;
}
VKAPI_ATTR void VKAPI_CALL destroySwapchain(VkDevice device,VkSwapchainKHR chain,const VkAllocationCallbacks *alloc){
    auto d=deviceFor(device);if(d){std::lock_guard<std::mutex> lock(d->mutex);auto it=d->chains.find(chain);if(it!=d->chains.end()){if(it->second->pipeline)d->api.DeviceWaitIdle(device);d->chains.erase(it);}}
    original<PFN_vkDestroySwapchainKHR>("vkDestroySwapchainKHR")(device,chain,alloc);
}
void acquired(VkDevice device,VkSwapchainKHR chain,std::uint32_t image){
    auto d=deviceFor(device);if(!d)return;std::lock_guard<std::mutex> lock(d->mutex);auto it=d->chains.find(chain);if(it!=d->chains.end()&&image<it->second->acquired.size())it->second->acquired[image]=true;
}
VKAPI_ATTR VkResult VKAPI_CALL acquire(VkDevice device,VkSwapchainKHR chain,std::uint64_t timeout,VkSemaphore semaphore,VkFence fence,std::uint32_t *image){
    const auto hr=original<PFN_vkAcquireNextImageKHR>("vkAcquireNextImageKHR")(device,chain,timeout,semaphore,fence,image);if((hr==VK_SUCCESS||hr==VK_SUBOPTIMAL_KHR)&&image)try{acquired(device,chain,*image);}catch(...){}
    return hr;
}
VKAPI_ATTR VkResult VKAPI_CALL acquire2(VkDevice device,const VkAcquireNextImageInfoKHR *info,std::uint32_t *image){
    const auto hr=original<PFN_vkAcquireNextImage2KHR>("vkAcquireNextImage2KHR")(device,info,image);if((hr==VK_SUCCESS||hr==VK_SUBOPTIMAL_KHR)&&image&&info)try{acquired(device,info->swapchain,*image);}catch(...){}
    return hr;
}
VKAPI_ATTR VkResult VKAPI_CALL present(VkQueue queue,const VkPresentInfoKHR *info){
    const auto next=original<PFN_vkQueuePresentKHR>("vkQueuePresentKHR");auto &s=state();
    if(!info||s.stopping.load()||!Runtime::instance().accepts(Renderer::Vulkan))return next(queue,info);
    Queue owner;{std::lock_guard<std::mutex> lock(s.mutex);auto it=s.queues.find(queue);if(it==s.queues.end())return next(queue,info);owner=it->second;}
    VkSemaphore last=VK_NULL_HANDLE;
    try{
        auto &d=*owner.device;std::lock_guard<std::mutex> lock(d.mutex);
        std::vector<VkSemaphore> waits;waits.reserve(64);
        if(info->waitSemaphoreCount>64||info->swapchainCount>32)return next(queue,info);
        if(info->waitSemaphoreCount)waits.assign(info->pWaitSemaphores,info->pWaitSemaphores+info->waitSemaphoreCount);
        const auto parameters=Runtime::instance().settings();
        for(std::uint32_t i=0;i<info->swapchainCount;++i){
            auto found=d.chains.find(info->pSwapchains[i]);if(found==d.chains.end())continue;auto &chain=*found->second;
            if(!Runtime::instance().eligible(reinterpret_cast<std::uintptr_t>(chain.window)))continue;
            const auto image=info->pImageIndices[i];const bool fresh=image<chain.acquired.size()&&chain.acquired[image];
            if(image<chain.acquired.size())chain.acquired[image]=false;
            std::string error=chain.diagnostic;bool rendered=false;
            if(parameters.neutral())error="Neutral bypass";
            else if(info->pNext)error="Vulkan extended presentation requires an explicit extension contract; original presentation retained.";
            else if(owner.protectedQueue||!(d.families[owner.family].queueFlags&VK_QUEUE_GRAPHICS_BIT))error="The Vulkan presentation queue is protected or has no graphics capability; shader processing safely bypassed.";
            else if(chain.info.imageSharingMode==VK_SHARING_MODE_EXCLUSIVE
                    &&(d.graphicsFamilies.size()!=1||d.graphicsFamilies[0]!=owner.family))error="Vulkan exclusive images with ambiguous cross-family ownership are bypassed; no unsafe ownership transfer was guessed.";
            else if(!fresh)error="Vulkan game image acquisition was not observed; shaders bypassed rather than reusing an unsafe semaphore.";
            else if(chain.failed)error="This Vulkan swapchain reported a presentation/device failure; shaders bypassed until recreation.";
            else if(chain.eligible){
                if(!chain.pipeline){
                    chain.family=owner.family;VulkanTarget target;target.api=d.api;target.memory=d.memory;target.format=chain.info.imageFormat;target.extent=chain.info.imageExtent;target.queueFamily=owner.family;target.images=chain.images;
                    auto pipeline=std::make_unique<VulkanPipeline>();if(pipeline->initialize(target,&error))chain.pipeline=std::move(pipeline);
                }
                VkSemaphore completed=VK_NULL_HANDLE;
                if(chain.pipeline&&chain.family==owner.family&&chain.pipeline->process(queue,image,waits,parameters,&completed,&error)&&completed){last=completed;waits.assign(1,completed);rendered=true;}
            }
            Runtime::instance().report(true,true,rendered,rendered?"Shaders active on the identified native Vulkan game surface.":error,rendered,Renderer::Vulkan,rendered);
        }
    }catch(...){Runtime::instance().report(true,true,false,"Vulkan shader processing failed; native presentation continues.",false,Renderer::Vulkan);}
    // Each input binary semaphore is consumed once. Chained shader submissions
    // consume the previous pass's completion; WSI waits only for the last pass.
    VkPresentInfoKHR modified=*info;if(last){modified.waitSemaphoreCount=1;modified.pWaitSemaphores=&last;}
    const auto hr=next(queue,last?&modified:info);
    if(hr!=VK_SUCCESS&&hr!=VK_SUBOPTIMAL_KHR)try{std::lock_guard<std::mutex> lock(owner.device->mutex);for(std::uint32_t i=0;i<info->swapchainCount;++i){auto it=owner.device->chains.find(info->pSwapchains[i]);if(it!=owner.device->chains.end())it->second->failed=true;}}catch(...){}
    return hr;
}
}
bool scanVulkanHooks(InstallPresentationHook install){
    auto &s=state();if(s.stopping.load())return false;if(s.installed)return true;
    const auto request=Runtime::instance().metrics().requested;if(request!=RendererFamily::Automatic&&request!=RendererFamily::Vulkan)return false;
    HMODULE module=GetModuleHandleW(L"vulkan-1.dll");if(!module)return false;
    // Publish all export addresses before making getProc callbacks visible.
    if(!s.addressesReady){for(auto &e:entries)e.target=reinterpret_cast<void*>(GetProcAddress(module,e.name));s.addressesReady=true;}
    if(!entries[0].target||!entries[1].target)return false;
    bool ready=true;for(auto &e:entries)if(e.target)ready=install(e.target,e.callback,nullptr)&&ready;
    s.installed=ready;return ready;
}
void stopVulkanHooks(){state().stopping.store(true);}
} // namespace BsiShaders
#endif
