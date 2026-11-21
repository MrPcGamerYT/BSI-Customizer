// Windows MinHook + two differently named authored Vulkan-loader doubles.
// Proves provider dispatch/lifecycle/HWND exclusion, NOT a real Vulkan driver,
// game pipeline, installed player or physical GPU. No fake processed-frame pass.
#include "Runtime.h"
#define VK_USE_PLATFORM_WIN32_KHR
#define VK_NO_PROTOTYPES
#include <windows.h>
#include <vulkan/vulkan.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <set>
#include <thread>
#include <vector>
using namespace BsiShaders;
namespace {
void require(bool value,const char *message){if(!value){std::cerr<<"FAIL: "<<message<<'\n'<<Runtime::instance().metrics().status<<'\n';std::exit(1);}}
void okay(VkResult result,const char *message){require(result==VK_SUCCESS,message);}
LRESULT CALLBACK windowProc(HWND w,UINT m,WPARAM a,LPARAM b){return DefWindowProcW(w,m,a,b);}
struct Fixture {
    HMODULE module=nullptr;
    PFN_vkGetInstanceProcAddr instanceProc=nullptr;
    PFN_vkGetDeviceProcAddr deviceProc=nullptr;
    VkInstance instance=VK_NULL_HANDLE;
    VkDevice device=VK_NULL_HANDLE;
    VkQueue queue=VK_NULL_HANDLE;
    std::vector<VkSurfaceKHR> surfaces;
    std::vector<VkSwapchainKHR> chains;
    template<typename Fn>Fn global(const char *name){return reinterpret_cast<Fn>(instanceProc(instance,name));}
    template<typename Fn>Fn function(const char *name){return reinterpret_cast<Fn>(deviceProc(device,name));}
    void open(const char *path){
        module=LoadLibraryA(path);require(module!=nullptr,"load a deliberately nonstandard-named test provider");
        instanceProc=reinterpret_cast<PFN_vkGetInstanceProcAddr>(reinterpret_cast<void*>(GetProcAddress(module,"vkGetInstanceProcAddr")));
        deviceProc=reinterpret_cast<PFN_vkGetDeviceProcAddr>(reinterpret_cast<void*>(GetProcAddress(module,"vkGetDeviceProcAddr")));
        require(instanceProc&&deviceProc,"test provider exports genuine getProc-shaped entry points");
        const char *names[]{"vkGetInstanceProcAddr","vkGetDeviceProcAddr","vkCreateInstance","vkDestroyInstance",
            "vkEnumeratePhysicalDevices","vkCreateWin32SurfaceKHR","vkDestroySurfaceKHR","vkCreateDevice",
            "vkDestroyDevice","vkGetDeviceQueue","vkCreateSwapchainKHR","vkDestroySwapchainKHR",
            "vkAcquireNextImageKHR","vkQueuePresentKHR"};
        std::set<FARPROC> entries;
        for(const auto *name:names){
            const auto entry=GetProcAddress(module,name);
            require(entry&&entries.insert(entry).second,"fixture exports must have distinct, detourable dispatch addresses (no ICF-folded aliases)");
        }
    }
    void create(){
        VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        okay(global<PFN_vkCreateInstance>("vkCreateInstance")(&info,nullptr,&instance),"dispatch instance creation to the right provider");
        std::uint32_t count=1;VkPhysicalDevice physical=VK_NULL_HANDLE;
        okay(global<PFN_vkEnumeratePhysicalDevices>("vkEnumeratePhysicalDevices")(instance,&count,&physical),"capture provider-specific physical device");
        const float priority=1;VkDeviceQueueCreateInfo q{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};q.queueCount=1;q.pQueuePriorities=&priority;
        VkDeviceCreateInfo d{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};d.queueCreateInfoCount=1;d.pQueueCreateInfos=&q;
        okay(global<PFN_vkCreateDevice>("vkCreateDevice")(physical,&d,nullptr,&device),"dispatch device creation without crossing providers");
        // Exercise the proc-only v2 getter, not just directly exported functions.
        auto get=function<PFN_vkGetDeviceQueue2>("vkGetDeviceQueue2");require(get!=nullptr,"capture proc-only queue getter");
        VkDeviceQueueInfo2 queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_INFO_2};get(device,&queueInfo,&queue);
        require(queue!=VK_NULL_HANDLE,"track the actual queue returned by this provider");
    }
    VkSwapchainKHR chain(HWND window){
        VkWin32SurfaceCreateInfoKHR surfaceInfo{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};surfaceInfo.hinstance=GetModuleHandleW(nullptr);surfaceInfo.hwnd=window;
        VkSurfaceKHR surface=VK_NULL_HANDLE;
        okay(global<PFN_vkCreateWin32SurfaceKHR>("vkCreateWin32SurfaceKHR")(instance,&surfaceInfo,nullptr,&surface),"capture exact Win32 surface owner");surfaces.push_back(surface);
        VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};info.surface=surface;info.imageExtent={640,360};info.imageArrayLayers=1;info.imageFormat=VK_FORMAT_R8G8B8A8_UNORM;info.imageColorSpace=VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        VkSwapchainKHR result=VK_NULL_HANDLE;
        okay(function<PFN_vkCreateSwapchainKHR>("vkCreateSwapchainKHR")(device,&info,nullptr,&result),"capture only this provider's swapchain");chains.push_back(result);return result;
    }
    void present(VkSwapchainKHR chain){
        std::uint32_t image=0;VkAcquireNextImageInfoKHR acquire{VK_STRUCTURE_TYPE_ACQUIRE_NEXT_IMAGE_INFO_KHR};acquire.swapchain=chain;acquire.deviceMask=1;
        okay(function<PFN_vkAcquireNextImage2KHR>("vkAcquireNextImage2KHR")(device,&acquire,&image),"capture proc-only acquire2 without export recursion");
        VkPresentInfoKHR info{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};info.swapchainCount=1;info.pSwapchains=&chain;info.pImageIndices=&image;
        okay(function<PFN_vkQueuePresentKHR>("vkQueuePresentKHR")(queue,&info),"call the correct provider's original present exactly once");
    }
    void remove(VkSwapchainKHR chain){
        function<PFN_vkDestroySwapchainKHR>("vkDestroySwapchainKHR")(device,chain,nullptr);
        chains.erase(std::remove(chains.begin(),chains.end(),chain),chains.end());
    }
    LONG calls(){return reinterpret_cast<LONG(*)()>(reinterpret_cast<void*>(GetProcAddress(module,"bsiFixturePresentCalls")))();}
    void close(){
        for(auto c:chains)function<PFN_vkDestroySwapchainKHR>("vkDestroySwapchainKHR")(device,c,nullptr);
        for(auto s:surfaces)global<PFN_vkDestroySurfaceKHR>("vkDestroySurfaceKHR")(instance,s,nullptr);
        function<PFN_vkDestroyDevice>("vkDestroyDevice")(device,nullptr);
        global<PFN_vkDestroyInstance>("vkDestroyInstance")(instance,nullptr);
    }
};
}
int main(int argc,char **argv){
    require(argc==3,"pass two separately built provider DLL paths");
    HWND root=CreateWindowExW(0,L"STATIC",L"test-owned player root",WS_OVERLAPPEDWINDOW,0,0,900,600,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    HWND game=CreateWindowExW(0,L"STATIC",L"test-owned game",WS_CHILD,0,0,640,360,root,nullptr,GetModuleHandleW(nullptr),nullptr);
    WNDCLASSW cls{};cls.lpfnWndProc=windowProc;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"QtVulkanTestExcluded";
    require(RegisterClassW(&cls)!=0,"register a real explicitly Qt-classified UI HWND");
    HWND ui=CreateWindowExW(0,cls.lpszClassName,L"test-owned excluded UI",WS_CHILD,0,0,640,360,root,nullptr,cls.hInstance,nullptr);
    require(root&&game&&ui,"create owned native windows");
    Fixture a,b;a.open(argv[1]);
    auto &runtime=Runtime::instance();runtime.configureRenderer(RendererFamily::OpenGL);runtime.start(reinterpret_cast<std::uintptr_t>(root));
    const auto initial=runtime.metrics().vulkan.loaders;
    require(initial>=1,"scan hooks a complete loader whose filename is NOT vulkan-1.dll");
    b.open(argv[2]);
    for(int i=0;i<100&&runtime.metrics().vulkan.loaders==initial;++i)std::this_thread::sleep_for(std::chrono::milliseconds(50));
    require(runtime.metrics().vulkan.loaders>initial,"a second late-loaded provider is independently captured");
    a.create();b.create();
    require(a.instance!=b.instance&&a.device!=b.device&&a.queue!=b.queue,"independent provider doubles have distinct handles");
    const auto uiChain=a.chain(ui);a.present(uiChain);
    auto m=runtime.metrics();require(!m.observed&&!m.active&&m.processed==0&&m.vulkan.rejectedWindows==1,"Qt Vulkan presentation is lifecycle evidence, never the game");
    const auto gameB=b.chain(game);b.present(gameB);
    m=runtime.metrics();require(m.observed&&m.renderer==Renderer::Vulkan&&!m.active&&m.processed==0,"identified neutral game presentation has no fabricated processed-frame success");
    const auto gameA=a.chain(game);a.present(gameA);b.present(gameB);
    require(a.calls()==2&&b.calls()==2,"interleaved providers each reach only their own original present");
    require(runtime.metrics().vulkan.devices==2&&runtime.metrics().vulkan.swapchains==3,"every captured lifecycle belongs to the correct provider registry");
    runtime.configureRenderer(RendererFamily::DirectX);a.present(gameA);
    require(runtime.metrics().observed&&runtime.metrics().renderer==Renderer::Vulkan&&runtime.metrics().configured==RendererFamily::DirectX
            &&runtime.metrics().requested==RendererFamily::Automatic&&runtime.metrics().processed==0,
            "actual Vulkan presentation stays eligible under DirectX/OpenGL config hints; it is never relabeled or counted as an effect pass");
    b.remove(gameB);require(runtime.metrics().observed,"destroying another provider's chain must not revoke the selected source");
    a.remove(gameA);require(!runtime.metrics().observed,"destroying the selected swapchain re-arms automatic detection");
    const auto replacement=b.chain(game);b.present(replacement);
    require(runtime.metrics().observed&&runtime.metrics().processed==0,"a replacement native source is adopted without fake processing");
    a.close();b.close();require(!runtime.metrics().observed,"normal Vulkan context/device teardown cannot leave a stale routing claim");runtime.stop();
    DestroyWindow(ui);DestroyWindow(game);DestroyWindow(root);UnregisterClassW(cls.lpszClassName,cls.hInstance);
    std::cout<<"PASS: production detours across two differently named Vulkan-loader doubles, independent originals and handle registries, late provider scan, proc-only queue/acquire2, Qt HWND exclusion, neutral no-pass, config-independent actual Vulkan routing and selected/nonselected source-teardown re-detection\n";
    std::cout<<"LIMIT: authored loader contract only, no Vulkan ICD/game shader execution or GPU/player certification\n";
}
