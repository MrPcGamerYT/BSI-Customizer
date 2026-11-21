// Authored DLL-dispatch double, NOT a Vulkan implementation/ICD. No shaders or
// GPU commands execute here. Two copies have deliberately different opaque
// handles and reject cross-provider dispatch. Only the neutral ownership path
// uses these stubs; real Vulkan pipeline pixels have a separate mandatory test.
#define VK_USE_PLATFORM_WIN32_KHR
#define VK_NO_PROTOTYPES
#include <windows.h>
#include <vulkan/vulkan.h>
#include <cstring>
#include <cstdint>

#define API extern "C" __declspec(dllexport)
namespace {
int instanceToken=0,physicalToken=0,deviceToken=0,queueToken=0;
int surfaceTokens[16]{},chainTokens[16]{};
unsigned surfaces=0,chains=0;
volatile LONG calls=0,housekeeping=0;
template<typename Handle>Handle handle(void *p){return reinterpret_cast<Handle>(p);}
void VKAPI_CALL unused(){InterlockedIncrement(&housekeeping);}
void VKAPI_CALL memory(VkPhysicalDevice,VkPhysicalDeviceMemoryProperties *out){*out={};out->memoryTypeCount=1;out->memoryTypes[0].propertyFlags=VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;}
void VKAPI_CALL families(VkPhysicalDevice,std::uint32_t *count,VkQueueFamilyProperties *out){if(out&&*count){*out={};out->queueFlags=VK_QUEUE_GRAPHICS_BIT;out->queueCount=1;}*count=1;}
VkResult VKAPI_CALL idle(VkDevice d){return d==handle<VkDevice>(&deviceToken)?VK_SUCCESS:VK_ERROR_DEVICE_LOST;}
}
API VkResult VKAPI_CALL vkCreateInstance(const VkInstanceCreateInfo*,const VkAllocationCallbacks*,VkInstance *out){InterlockedIncrement(&housekeeping);*out=handle<VkInstance>(&instanceToken);return VK_SUCCESS;}
// Distinct observable bodies matter: MSVC /OPT:ICF otherwise folds these
// unused-argument destruction exports into ONE address. Real loader dispatch
// thunks are distinct; a fabricated alias would correctly reject two different
// MinHook replacements and never satisfy the complete-provider contract.
API void VKAPI_CALL vkDestroyInstance(VkInstance,const VkAllocationCallbacks*){InterlockedExchangeAdd(&housekeeping,2);}
API VkResult VKAPI_CALL vkEnumeratePhysicalDevices(VkInstance i,std::uint32_t *count,VkPhysicalDevice *out){if(i!=handle<VkInstance>(&instanceToken))return VK_ERROR_INITIALIZATION_FAILED;if(out&&*count)*out=handle<VkPhysicalDevice>(&physicalToken);*count=1;return VK_SUCCESS;}
API VkResult VKAPI_CALL vkCreateWin32SurfaceKHR(VkInstance i,const VkWin32SurfaceCreateInfoKHR*,const VkAllocationCallbacks*,VkSurfaceKHR *out){if(i!=handle<VkInstance>(&instanceToken)||surfaces==16)return VK_ERROR_INITIALIZATION_FAILED;*out=handle<VkSurfaceKHR>(&surfaceTokens[surfaces++]);return VK_SUCCESS;}
API void VKAPI_CALL vkDestroySurfaceKHR(VkInstance,VkSurfaceKHR,const VkAllocationCallbacks*){InterlockedExchangeAdd(&housekeeping,3);}
API VkResult VKAPI_CALL vkCreateDevice(VkPhysicalDevice p,const VkDeviceCreateInfo*,const VkAllocationCallbacks*,VkDevice *out){if(p!=handle<VkPhysicalDevice>(&physicalToken))return VK_ERROR_INITIALIZATION_FAILED;*out=handle<VkDevice>(&deviceToken);return VK_SUCCESS;}
API void VKAPI_CALL vkDestroyDevice(VkDevice,const VkAllocationCallbacks*){InterlockedExchangeAdd(&housekeeping,4);}
API void VKAPI_CALL vkGetDeviceQueue(VkDevice d,std::uint32_t,std::uint32_t,VkQueue *out){InterlockedIncrement(&housekeeping);*out=d==handle<VkDevice>(&deviceToken)?handle<VkQueue>(&queueToken):VK_NULL_HANDLE;}
// The v2 functions are proc-only (not DLL exports), as on some real providers.
void VKAPI_CALL queue2(VkDevice d,const VkDeviceQueueInfo2*,VkQueue *out){vkGetDeviceQueue(d,0,0,out);}
API VkResult VKAPI_CALL vkCreateSwapchainKHR(VkDevice d,const VkSwapchainCreateInfoKHR*,const VkAllocationCallbacks*,VkSwapchainKHR *out){if(d!=handle<VkDevice>(&deviceToken)||chains==16)return VK_ERROR_INITIALIZATION_FAILED;*out=handle<VkSwapchainKHR>(&chainTokens[chains++]);return VK_SUCCESS;}
API void VKAPI_CALL vkDestroySwapchainKHR(VkDevice,VkSwapchainKHR,const VkAllocationCallbacks*){InterlockedExchangeAdd(&housekeeping,5);}
API VkResult VKAPI_CALL vkAcquireNextImageKHR(VkDevice d,VkSwapchainKHR,std::uint64_t,VkSemaphore,VkFence,std::uint32_t *out){InterlockedIncrement(&housekeeping);*out=0;return d==handle<VkDevice>(&deviceToken)?VK_SUCCESS:VK_ERROR_DEVICE_LOST;}
VkResult VKAPI_CALL acquire2(VkDevice d,const VkAcquireNextImageInfoKHR*,std::uint32_t *out){return vkAcquireNextImageKHR(d,VK_NULL_HANDLE,0,VK_NULL_HANDLE,VK_NULL_HANDLE,out);}
API VkResult VKAPI_CALL vkQueuePresentKHR(VkQueue q,const VkPresentInfoKHR*){InterlockedIncrement(&calls);return q==handle<VkQueue>(&queueToken)?VK_SUCCESS:VK_ERROR_DEVICE_LOST;}
API PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance,const char*);
API PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice,const char*);
namespace {
PFN_vkVoidFunction lookup(const char *name){
#define FUNCTION(fn) if(std::strcmp(name,#fn)==0)return reinterpret_cast<PFN_vkVoidFunction>(&fn)
    FUNCTION(vkGetInstanceProcAddr);FUNCTION(vkGetDeviceProcAddr);FUNCTION(vkCreateInstance);FUNCTION(vkDestroyInstance);
    FUNCTION(vkEnumeratePhysicalDevices);FUNCTION(vkCreateWin32SurfaceKHR);FUNCTION(vkDestroySurfaceKHR);
    FUNCTION(vkCreateDevice);FUNCTION(vkDestroyDevice);FUNCTION(vkGetDeviceQueue);FUNCTION(vkCreateSwapchainKHR);
    FUNCTION(vkDestroySwapchainKHR);FUNCTION(vkAcquireNextImageKHR);FUNCTION(vkQueuePresentKHR);
#undef FUNCTION
    if(std::strcmp(name,"vkGetDeviceQueue2")==0)return reinterpret_cast<PFN_vkVoidFunction>(&queue2);
    if(std::strcmp(name,"vkAcquireNextImage2KHR")==0)return reinterpret_cast<PFN_vkVoidFunction>(&acquire2);
    if(std::strcmp(name,"vkGetPhysicalDeviceMemoryProperties")==0)return reinterpret_cast<PFN_vkVoidFunction>(&memory);
    if(std::strcmp(name,"vkGetPhysicalDeviceQueueFamilyProperties")==0)return reinterpret_cast<PFN_vkVoidFunction>(&families);
    if(std::strcmp(name,"vkDeviceWaitIdle")==0)return reinterpret_cast<PFN_vkVoidFunction>(&idle);
    return nullptr;
}
}
API PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance,const char *name){return lookup(name);}
API PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice,const char *name){auto p=lookup(name);return p?p:&unused;}
API LONG bsiFixturePresentCalls(){return calls;}
