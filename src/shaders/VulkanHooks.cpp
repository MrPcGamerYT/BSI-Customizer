#include "PresentationHooks.h"
#ifdef _WIN32
#include "Runtime.h"
#include "VulkanPipeline.h"
#include "VulkanPresent.h"
#define VK_USE_PLATFORM_WIN32_KHR
#include <windows.h>
#include <psapi.h>
#include <array>
#include <vulkan/vulkan_win32.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>

namespace BsiShaders {
namespace {
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
struct Entry {const char *name=nullptr;void *callback=nullptr;void *target=nullptr;};
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
    std::atomic<bool> stopping{false};
    std::map<VkInstance,std::shared_ptr<Instance>> instances;
    std::map<VkPhysicalDevice,std::shared_ptr<Instance>> physical;
    std::map<VkDevice,std::shared_ptr<Device>> devices;
    std::map<VkQueue,Queue> queues;
    std::map<VkSurfaceKHR,HWND> surfaces;
};
// A player can ship its own Vulkan loader alongside the OS loader used by
// Qt/ANGLE. Each provider needs its OWN originals and capture registry. Choosing
// GetModuleHandle("vulkan-1.dll") once pins the wrong provider on such players;
// sharing one trampoline table would dispatch their handles into another DLL.
constexpr std::size_t maximumProviders=4, entryCount=16;
struct Provider {
    HMODULE module=nullptr;
    std::array<Entry,entryCount> entries{};
    State captured;
    bool installed=false;
};
struct Providers {
    std::mutex mutex;
    std::atomic<bool> stopping{false};
    std::array<Provider,maximumProviders> slots;
    std::size_t count=0;
};
Providers &providers(){static auto *p=new Providers;return *p;}
thread_local Provider *callingProvider=nullptr;
struct ProviderScope {
    Provider *previous=callingProvider;
    explicit ProviderScope(Provider &p){callingProvider=&p;}
    ~ProviderScope(){callingProvider=previous;}
};
Provider &provider(){return *callingProvider;}
State &state(){return provider().captured;}
#define BSI_VK_ROUTES(X) \
 X(PFN_vkVoidFunction,GetInstanceProcAddr,getInstanceProc,(VkInstance i,const char *n),(i,n)) \
 X(PFN_vkVoidFunction,GetDeviceProcAddr,getDeviceProc,(VkDevice d,const char *n),(d,n)) \
 X(VkResult,CreateInstance,createInstance,(const VkInstanceCreateInfo *i,const VkAllocationCallbacks *a,VkInstance *o),(i,a,o)) \
 X(void,DestroyInstance,destroyInstance,(VkInstance i,const VkAllocationCallbacks *a),(i,a)) \
 X(VkResult,EnumeratePhysicalDevices,enumeratePhysical,(VkInstance i,std::uint32_t *n,VkPhysicalDevice *o),(i,n,o)) \
 X(VkResult,CreateWin32SurfaceKHR,createSurface,(VkInstance i,const VkWin32SurfaceCreateInfoKHR *c,const VkAllocationCallbacks *a,VkSurfaceKHR *o),(i,c,a,o)) \
 X(void,DestroySurfaceKHR,destroySurface,(VkInstance i,VkSurfaceKHR s,const VkAllocationCallbacks *a),(i,s,a)) \
 X(VkResult,CreateDevice,createDevice,(VkPhysicalDevice p,const VkDeviceCreateInfo *i,const VkAllocationCallbacks *a,VkDevice *o),(p,i,a,o)) \
 X(void,DestroyDevice,destroyDevice,(VkDevice d,const VkAllocationCallbacks *a),(d,a)) \
 X(void,GetDeviceQueue,getQueue,(VkDevice d,std::uint32_t f,std::uint32_t i,VkQueue *o),(d,f,i,o)) \
 X(void,GetDeviceQueue2,getQueue2,(VkDevice d,const VkDeviceQueueInfo2 *i,VkQueue *o),(d,i,o)) \
 X(VkResult,CreateSwapchainKHR,createSwapchain,(VkDevice d,const VkSwapchainCreateInfoKHR *i,const VkAllocationCallbacks *a,VkSwapchainKHR *o),(d,i,a,o)) \
 X(void,DestroySwapchainKHR,destroySwapchain,(VkDevice d,VkSwapchainKHR s,const VkAllocationCallbacks *a),(d,s,a)) \
 X(VkResult,AcquireNextImageKHR,acquire,(VkDevice d,VkSwapchainKHR s,std::uint64_t t,VkSemaphore m,VkFence f,std::uint32_t *o),(d,s,t,m,f,o)) \
 X(VkResult,AcquireNextImage2KHR,acquire2,(VkDevice d,const VkAcquireNextImageInfoKHR *i,std::uint32_t *o),(d,i,o)) \
 X(VkResult,QueuePresentKHR,present,(VkQueue q,const VkPresentInfoKHR *i),(q,i))
template<std::size_t Slot>struct Routes {
#define ROUTE(result,name,implementation,params,args) \
    static result VKAPI_CALL name params {ProviderScope scope(providers().slots[Slot]);return implementation args;}
    BSI_VK_ROUTES(ROUTE)
#undef ROUTE
};
template<std::size_t Slot>void initializeProvider(HMODULE module){
    auto &p=providers().slots[Slot];
    p.module=module;
    p.entries={{
#define ENTRY(result,name,implementation,params,args) {"vk" #name,reinterpret_cast<void*>(&Routes<Slot>::name),reinterpret_cast<void*>(GetProcAddress(module,"vk" #name))},
        BSI_VK_ROUTES(ENTRY)
#undef ENTRY
    }};
}
#undef BSI_VK_ROUTES
template<typename Fn>Fn original(const char *name){
    for(const auto &e:provider().entries)if(std::strcmp(e.name,name)==0){auto trampoline=originalPresentationFunction(e.target);return reinterpret_cast<Fn>(trampoline?trampoline:e.target);}
    return nullptr;
}
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
    // These v2 entry points may be proc-only instead of loader exports.
    if(raw&&name)for(const auto &e:provider().entries)
        if((e.target||std::strcmp(name,"vkGetDeviceQueue2")==0||std::strcmp(name,"vkAcquireNextImage2KHR")==0)
           &&std::strcmp(e.name,name)==0)return reinterpret_cast<PFN_vkVoidFunction>(e.callback);
    return raw;
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL getDeviceProc(VkDevice device,const char *name){
    auto raw=original<PFN_vkGetDeviceProcAddr>("vkGetDeviceProcAddr")(device,name);
    // These v2 entry points may be proc-only instead of loader exports.
    if(raw&&name)for(const auto &e:provider().entries)
        if((e.target||std::strcmp(name,"vkGetDeviceQueue2")==0||std::strcmp(name,"vkAcquireNextImage2KHR")==0)
           &&std::strcmp(e.name,name)==0)return reinterpret_cast<PFN_vkVoidFunction>(e.callback);
    return raw;
}
VKAPI_ATTR VkResult VKAPI_CALL createInstance(const VkInstanceCreateInfo *info,const VkAllocationCallbacks *alloc,VkInstance *out){
    const auto hr=original<PFN_vkCreateInstance>("vkCreateInstance")(info,alloc,out);
    if(hr==VK_SUCCESS&&out)try {
        auto instance=std::make_shared<Instance>();instance->handle=*out;auto &s=state();
        {std::lock_guard<std::mutex> lock(s.mutex);if(s.instances.size()>=16)return hr;s.instances[*out]=instance;}
        Runtime::instance().vulkanEvent(VulkanEvent::InstanceCreated);
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
    if(hr==VK_SUCCESS&&out&&info)try{
        {auto &s=state();std::lock_guard<std::mutex> lock(s.mutex);if(s.surfaces.size()<128)s.surfaces[*out]=info->hwnd;}
        Runtime::instance().vulkanEvent(VulkanEvent::SurfaceCreated);
    }catch(...){}
    return hr;
}
VKAPI_ATTR void VKAPI_CALL destroySurface(VkInstance instance,VkSurfaceKHR surface,const VkAllocationCallbacks *alloc){
    {auto &s=state();std::lock_guard<std::mutex> lock(s.mutex);s.surfaces.erase(surface);}original<PFN_vkDestroySurfaceKHR>("vkDestroySurfaceKHR")(instance,surface,alloc);
}
VKAPI_ATTR VkResult VKAPI_CALL createDevice(VkPhysicalDevice physical,const VkDeviceCreateInfo *info,const VkAllocationCallbacks *alloc,VkDevice *out){
    const auto hr=original<PFN_vkCreateDevice>("vkCreateDevice")(physical,info,alloc,out);
    if(hr!=VK_SUCCESS||!out)return hr;
    const auto rejected=[](const std::string &message){Runtime::instance().vulkanEvent(VulkanEvent::DeviceRejected,message);};
    try{
        auto d=std::make_shared<Device>();d->handle=*out;d->physical=physical;auto &s=state();
        {std::lock_guard<std::mutex> lock(s.mutex);
            auto it=s.physical.find(physical);
            if(it==s.physical.end()||s.devices.size()>=32){
                rejected("Vulkan device creation was intercepted without its captured instance/physical-device mapping, or the bounded device registry is full. Fully restart the player; no ownership is guessed.");return hr;
            }
            d->instance=it->second;
        }
        std::string error;
        if(!d->api.load(*out,resolveDevice,&error)){rejected("Vulkan device entry points are incomplete: "+error);return hr;}
        const auto instance=d->instance->handle;
        const auto memory=instanceFunction<PFN_vkGetPhysicalDeviceMemoryProperties>(instance,"vkGetPhysicalDeviceMemoryProperties");
        const auto queues=instanceFunction<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(instance,"vkGetPhysicalDeviceQueueFamilyProperties");
        if(!memory||!queues){rejected("Vulkan device memory/queue-family queries are unavailable.");return hr;}
        memory(physical,&d->memory);std::uint32_t count=0;queues(physical,&count,nullptr);
        if(!count||count>64){rejected("Vulkan queue-family count exceeds the supported capture bounds.");return hr;}
        d->families.resize(count);queues(physical,&count,d->families.data());
        if(!count||count>d->families.size()){rejected("Vulkan queue-family enumeration changed unexpectedly.");return hr;}
        d->families.resize(count);
        if(info)for(std::uint32_t i=0;i<info->queueCreateInfoCount;++i){
            const auto family=info->pQueueCreateInfos[i].queueFamilyIndex;
            if(family<count&&(d->families[family].queueFlags&VK_QUEUE_GRAPHICS_BIT)
               &&std::find(d->graphicsFamilies.begin(),d->graphicsFamilies.end(),family)==d->graphicsFamilies.end())d->graphicsFamilies.push_back(family);
        }
        std::vector<std::pair<VkQueue,std::uint32_t>> createdQueues;
        const auto get=reinterpret_cast<PFN_vkGetDeviceQueue>(resolveDevice(*out,"vkGetDeviceQueue"));
        if(info&&get)for(std::uint32_t i=0;i<info->queueCreateInfoCount&&createdQueues.size()<128;++i){
            const auto &q=info->pQueueCreateInfos[i];
            // vkGetDeviceQueue is valid only for zero-flag queue-create infos.
            // Protected queues remain tracked only by their explicit getter.
            if(q.flags||q.queueFamilyIndex>=count)continue;
            for(std::uint32_t j=0;j<q.queueCount&&createdQueues.size()<128;++j){
                VkQueue queue=VK_NULL_HANDLE;get(*out,q.queueFamilyIndex,j,&queue);
                if(queue)createdQueues.emplace_back(queue,q.queueFamilyIndex);
            }
        }
        {std::lock_guard<std::mutex> lock(s.mutex);s.devices[*out]=d;
            for(const auto &q:createdQueues)if(s.queues.size()<128)s.queues[q.first]={d,q.second,false};}
        Runtime::instance().vulkanEvent(VulkanEvent::DeviceTracked);
    }catch(...){rejected("Vulkan device capture failed; the original device continues without shader processing.");}
    return hr;
}
VKAPI_ATTR void VKAPI_CALL destroyDevice(VkDevice device,const VkAllocationCallbacks *alloc){
    auto d=deviceFor(device);
    if(d){std::lock_guard<std::mutex> lock(d->mutex);d->api.DeviceWaitIdle(device);
        for(const auto &entry:d->chains)Runtime::instance().releasePresentation(Renderer::Vulkan,reinterpret_cast<std::uintptr_t>(entry.first));
        d->chains.clear();}
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
    const auto next=reinterpret_cast<PFN_vkGetDeviceQueue2>(resolveDevice(device,"vkGetDeviceQueue2"));
    if(next)next(device,info,out);
    if(next&&info)try{recordQueue(device,info->queueFamilyIndex,(info->flags&VK_DEVICE_QUEUE_CREATE_PROTECTED_BIT)!=0,out);}catch(...){}
}
VKAPI_ATTR VkResult VKAPI_CALL createSwapchain(VkDevice device,const VkSwapchainCreateInfoKHR *info,const VkAllocationCallbacks *alloc,VkSwapchainKHR *out){
    const auto next=original<PFN_vkCreateSwapchainKHR>("vkCreateSwapchainKHR");auto d=deviceFor(device);HWND window=nullptr;
    if(info){auto &s=state();std::lock_guard<std::mutex> lock(s.mutex);auto it=s.surfaces.find(info->surface);if(it!=s.surfaces.end())window=it->second;}
    VkSwapchainCreateInfoKHR enhanced{};if(info)enhanced=*info;
    constexpr VkImageUsageFlags needed=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    bool capable=false,enhance=false;
    try{
        // Layout/host attachment may finish AFTER swapchain creation. Query the
        // actual image contract now, but only ADD usage for an identified game
        // child. An unmodified swapchain already carrying the required usage
        // can be adopted later after HWND identity and acquisition are proven.
        if(info&&d&&!info->flags&&info->imageArrayLayers==1&&VulkanPipeline::supports(info->imageFormat)
           &&info->imageColorSpace==VK_COLOR_SPACE_SRGB_NONLINEAR_KHR){
            const auto capabilities=instanceFunction<PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR>(d->instance->handle,"vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
            const auto formats=instanceFunction<PFN_vkGetPhysicalDeviceFormatProperties>(d->instance->handle,"vkGetPhysicalDeviceFormatProperties");
            VkSurfaceCapabilitiesKHR caps{};VkFormatProperties properties{};
            if(capabilities&&formats&&capabilities(d->physical,info->surface,&caps)==VK_SUCCESS){
                formats(d->physical,info->imageFormat,&properties);
                const VkFormatFeatureFlags features=VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT|VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
                capable=(caps.supportedUsageFlags&needed)==needed&&(properties.optimalTilingFeatures&features)==features;
                enhance=capable&&Runtime::instance().accepts(Renderer::Vulkan)
                    &&Runtime::instance().ownsGameWindow(reinterpret_cast<std::uintptr_t>(window));
                if(enhance)enhanced.imageUsage|=needed;
            }
        }
    }catch(...){capable=enhance=false;}
    auto hr=next(device,enhance?&enhanced:info,alloc,out);
    if(hr!=VK_SUCCESS&&enhance){enhance=false;hr=next(device,info,alloc,out);} // Never sacrifice game startup for effects.
    if(hr==VK_SUCCESS&&out&&d&&info)try{
        const bool supported=capable&&((enhance?enhanced.imageUsage:info->imageUsage)&needed)==needed;
        auto chain=std::make_shared<Swapchain>();chain->window=window;chain->info=*info;chain->info.pNext=nullptr;chain->info.pQueueFamilyIndices=nullptr;chain->eligible=supported;
        chain->diagnostic=capable?"Vulkan swapchain lacks transfer-source/color-attachment usage; restart the player so its identified game surface can be prepared before creation. Original game frames retained."
            :"Vulkan swapchain format, color space, flags or device image features are unsupported; original game frames retained.";
        if(supported){
            const auto images=reinterpret_cast<PFN_vkGetSwapchainImagesKHR>(resolveDevice(device,"vkGetSwapchainImagesKHR"));std::uint32_t count=0;
            if(!images||images(device,*out,&count,nullptr)!=VK_SUCCESS||!count||count>8)chain->eligible=false;
            else{chain->images.resize(count);if(images(device,*out,&count,chain->images.data())!=VK_SUCCESS)chain->eligible=false;chain->acquired.resize(count,false);}
            if(!chain->eligible)chain->diagnostic="Vulkan swapchain images could not be safely enumerated; original game frames retained.";
        }
        {std::lock_guard<std::mutex> lock(d->mutex);if(d->chains.size()<32)d->chains[*out]=chain;}
        Runtime::instance().vulkanEvent(VulkanEvent::SwapchainTracked);
    }catch(...){}
    return hr;
}
VKAPI_ATTR void VKAPI_CALL destroySwapchain(VkDevice device,VkSwapchainKHR chain,const VkAllocationCallbacks *alloc){
    auto d=deviceFor(device);if(d){std::lock_guard<std::mutex> lock(d->mutex);auto it=d->chains.find(chain);if(it!=d->chains.end()){if(it->second->pipeline)d->api.DeviceWaitIdle(device);d->chains.erase(it);}}
    Runtime::instance().releasePresentation(Renderer::Vulkan,reinterpret_cast<std::uintptr_t>(chain));
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
    const auto next=reinterpret_cast<PFN_vkAcquireNextImage2KHR>(resolveDevice(device,"vkAcquireNextImage2KHR"));
    if(!next)return VK_ERROR_EXTENSION_NOT_PRESENT;
    const auto hr=next(device,info,image);if((hr==VK_SUCCESS||hr==VK_SUBOPTIMAL_KHR)&&image&&info)try{acquired(device,info->swapchain,*image);}catch(...){}
    return hr;
}
VKAPI_ATTR VkResult VKAPI_CALL present(VkQueue queue,const VkPresentInfoKHR *info){
    const auto next=original<PFN_vkQueuePresentKHR>("vkQueuePresentKHR");auto &s=state();
    if(!info||s.stopping.load()||!Runtime::instance().accepts(Renderer::Vulkan))return next(queue,info);
    Runtime::instance().vulkanEvent(VulkanEvent::Presented);
    Queue owner;{std::lock_guard<std::mutex> lock(s.mutex);auto it=s.queues.find(queue);if(it!=s.queues.end())owner=it->second;}
    if(!owner.device){
        Runtime::instance().vulkanEvent(VulkanEvent::QueueMissed,
            "Vulkan presentation was intercepted, but its device/queue creation was not captured. Fully restart the player after applying the patch; unidentified frames remain untouched.");
        return next(queue,info);
    }
    VkSemaphore last=VK_NULL_HANDLE;
    VulkanPresent presentation;std::string contractError;bool identified=false;
    try{
        const bool contract=presentation.prepare(*info,&contractError);
        auto &d=*owner.device;std::lock_guard<std::mutex> lock(d.mutex);
        std::vector<VkSemaphore> waits;waits.reserve(64);
        if(info->waitSemaphoreCount>64||info->swapchainCount>32||!info->pSwapchains||!info->pImageIndices
           ||(info->waitSemaphoreCount&&!info->pWaitSemaphores))return next(queue,info);
        if(info->waitSemaphoreCount)waits.assign(info->pWaitSemaphores,info->pWaitSemaphores+info->waitSemaphoreCount);
        const auto parameters=Runtime::instance().settings();
        for(std::uint32_t i=0;i<info->swapchainCount;++i){
            auto found=d.chains.find(info->pSwapchains[i]);if(found==d.chains.end())continue;auto &chain=*found->second;
            if(!Runtime::instance().eligible(reinterpret_cast<std::uintptr_t>(chain.window))){
                Runtime::instance().vulkanEvent(VulkanEvent::WindowRejected,Runtime::instance().gameWindowDiagnostic(reinterpret_cast<std::uintptr_t>(chain.window)));continue;
            }
            if(!Runtime::instance().claimPresentation(Renderer::Vulkan,reinterpret_cast<std::uintptr_t>(info->pSwapchains[i]),reinterpret_cast<std::uintptr_t>(chain.window)))continue;
            identified=true;
            const auto image=info->pImageIndices[i];const bool fresh=image<chain.acquired.size()&&chain.acquired[image];
            if(image<chain.acquired.size())chain.acquired[image]=false;
            std::string error=chain.diagnostic;bool rendered=false;
            if(parameters.neutral())error="Neutral bypass";
            else if(!contract)error=contractError;
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
                if(chain.pipeline&&chain.family==owner.family&&chain.pipeline->process(queue,image,waits,parameters,&completed,&error)&&completed){last=completed;waits.assign(1,completed);rendered=true;presentation.processed(i,chain.info.imageExtent);}
            }
            Runtime::instance().report(true,true,rendered,rendered?"Shaders active on the identified native Vulkan game surface.":error,rendered,Renderer::Vulkan,rendered);
        }
    }catch(...){
        if(identified)Runtime::instance().report(true,true,false,"Vulkan shader processing failed; native presentation continues.",false,Renderer::Vulkan);
        else Runtime::instance().vulkanEvent(VulkanEvent::DeviceRejected,"Vulkan presentation capture failed before game-window ownership was proven; original frames are retained.");
    }
    // Each input binary semaphore is consumed once. Chained shader submissions
    // consume the previous pass's completion; WSI waits only for the last pass.
    VkPresentInfoKHR modified=presentation.info();if(last){modified.waitSemaphoreCount=1;modified.pWaitSemaphores=&last;}
    const auto hr=next(queue,last?&modified:info);
    if(hr!=VK_SUCCESS&&hr!=VK_SUBOPTIMAL_KHR)try{std::lock_guard<std::mutex> lock(owner.device->mutex);for(std::uint32_t i=0;i<info->swapchainCount;++i){auto it=owner.device->chains.find(info->pSwapchains[i]);if(it!=owner.device->chains.end())it->second->failed=true;}}catch(...){}
    return hr;
}
}
bool scanVulkanHooks(InstallPresentationHook install){
    auto &all=providers();if(!install||all.stopping.load())return false;
    const auto request=Runtime::instance().metrics().requested;
    if(request!=RendererFamily::Automatic&&request!=RendererFamily::Vulkan)return false;
    std::lock_guard<std::mutex> lock(all.mutex);
    HMODULE modules[512]{};DWORD required=0;
    if(!EnumProcessModules(GetCurrentProcess(),modules,sizeof(modules),&required))return false;
    const auto count=(std::min)(std::size_t(required/sizeof(HMODULE)),std::size_t(512));
    for(std::size_t m=0;m<count;++m){
        const auto module=modules[m];
        const auto root=GetProcAddress(module,"vkGetInstanceProcAddr");
        if(!root)continue;
        bool known=false;
        for(std::size_t i=0;i<all.count;++i)
            if(all.slots[i].module==module||all.slots[i].entries[0].target==reinterpret_cast<void*>(root))known=true;
        if(known||all.count==maximumProviders)continue;
        // Match the public loader contract, NOT a filename, and never detour an
        // ICD's unrelated negotiation functions. The two Vulkan-1.1 getters
        // remain optional/proc-only; the complete base lifecycle is mandatory.
        const char *requiredNames[]{"vkGetDeviceProcAddr","vkCreateInstance","vkDestroyInstance","vkEnumeratePhysicalDevices",
            "vkCreateWin32SurfaceKHR","vkDestroySurfaceKHR","vkCreateDevice","vkDestroyDevice","vkGetDeviceQueue",
            "vkCreateSwapchainKHR","vkDestroySwapchainKHR","vkAcquireNextImageKHR","vkQueuePresentKHR"};
        bool complete=true;
        for(const auto *name:requiredNames)complete=GetProcAddress(module,name)&&complete;
        if(!complete)continue;
        HMODULE pinned=nullptr;
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
                              reinterpret_cast<LPCWSTR>(root),&pinned))continue;
        switch(all.count){
        case 0:initializeProvider<0>(module);break;case 1:initializeProvider<1>(module);break;
        case 2:initializeProvider<2>(module);break;case 3:initializeProvider<3>(module);break;
        default:break;
        }
        ++all.count;
    }
    bool attached=false;
    for(std::size_t i=0;i<all.count;++i){
        auto &p=all.slots[i];
        if(p.installed){attached=true;continue;}
        // Every address/table is published before getProc callbacks can run.
        bool ready=true;for(const auto &e:p.entries)if(e.target)ready=install(e.target,e.callback,nullptr)&&ready;
        if(ready){
            p.installed=true;attached=true;
            wchar_t path[1024]{};GetModuleFileNameW(p.module,path,1024);
            const wchar_t *leaf=path;for(const auto *c=path;*c;++c)if(*c==L'\\'||*c==L'/')leaf=c+1;
            char label[1040]{};
            WideCharToMultiByte(CP_UTF8,0,leaf,-1,label,sizeof(label),nullptr,nullptr);
            Runtime::instance().vulkanEvent(VulkanEvent::LoaderHooked,label);
        }
    }
    return attached;
}
void stopVulkanHooks(){
    auto &all=providers();all.stopping.store(true);
    for(auto &p:all.slots)p.captured.stopping.store(true);
}
} // namespace BsiShaders
#endif
