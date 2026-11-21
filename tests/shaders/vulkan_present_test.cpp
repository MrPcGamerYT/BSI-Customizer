// Extension ownership/forwarding only, not a GPU or Win32 WSI test.
#include "VulkanPresent.h"
#include <array>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <type_traits>
using namespace BsiShaders;
namespace {
void require(bool condition,const char *message){if(!condition){std::cerr<<"FAIL: "<<message<<'\n';std::exit(1);}}
template<class T>T handle(std::uintptr_t value){
    if constexpr(std::is_pointer_v<T>)return reinterpret_cast<T>(value);
    else return static_cast<T>(value);
}
}
int main(){
    VkSwapchainKHR swaps[]{handle<VkSwapchainKHR>(1),handle<VkSwapchainKHR>(2)};
    const std::uint32_t images[]{0,1};
    const VkSemaphore wait=handle<VkSemaphore>(3);
    VkResult results[]{VK_SUCCESS,VK_SUBOPTIMAL_KHR};
    VkPresentInfoKHR source{};source.sType=VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    source.waitSemaphoreCount=1;source.pWaitSemaphores=&wait;
    source.swapchainCount=2;source.pSwapchains=swaps;source.pImageIndices=images;source.pResults=results;
    VulkanPresent prepared;std::string error;
    require(prepared.prepare(source,&error),"plain presentation contract");
    require(prepared.info().pNext==nullptr&&prepared.info().pWaitSemaphores==&wait
        &&prepared.info().pSwapchains==swaps&&prepared.info().pImageIndices==images
        &&prepared.info().pResults==results,"preserve waits, native arrays and writable result destination");
    const VkRectLayerKHR damage{{4,8},{16,32},0};
    const VkPresentRegionKHR regions[]{{1,&damage},{1,&damage}};
    VkPresentRegionsKHR regionInfo{};regionInfo.sType=VK_STRUCTURE_TYPE_PRESENT_REGIONS_KHR;
    regionInfo.swapchainCount=2;regionInfo.pRegions=regions;
    const std::uint64_t ids[]{12,14};
    VkPresentIdKHR idInfo{};idInfo.sType=VK_STRUCTURE_TYPE_PRESENT_ID_KHR;
    idInfo.swapchainCount=2;idInfo.pPresentIds=ids;
    const VkPresentTimeGOOGLE times[]{{12,1234567},{14,1234568}};
    VkPresentTimesInfoGOOGLE timeInfo{};timeInfo.sType=VK_STRUCTURE_TYPE_PRESENT_TIMES_INFO_GOOGLE;
    timeInfo.swapchainCount=2;timeInfo.pTimes=times;
    const VkFence fences[]{handle<VkFence>(4),handle<VkFence>(5)};
    VkSwapchainPresentFenceInfoEXT fenceInfo{};fenceInfo.sType=VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_EXT;
    fenceInfo.swapchainCount=2;fenceInfo.pFences=fences;
    const VkPresentModeKHR modes[]{VK_PRESENT_MODE_FIFO_KHR,VK_PRESENT_MODE_MAILBOX_KHR};
    VkSwapchainPresentModeInfoEXT modeInfo{};modeInfo.sType=VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_MODE_INFO_EXT;
    modeInfo.swapchainCount=2;modeInfo.pPresentModes=modes;
    regionInfo.pNext=&idInfo;idInfo.pNext=&timeInfo;timeInfo.pNext=&fenceInfo;fenceInfo.pNext=&modeInfo;
    source.pNext=&regionInfo;
    // Snapshot the object representations, including padding. A C++ struct
    // copy need not copy padding identically on every Windows compiler.
    std::array<unsigned char,sizeof(source)> sourceBefore{};
    std::array<unsigned char,sizeof(regionInfo)> regionsBefore{};
    std::memcpy(sourceBefore.data(),&source,sizeof(source));
    std::memcpy(regionsBefore.data(),&regionInfo,sizeof(regionInfo));
    require(prepared.prepare(source,&error),"supported five-node metadata chain");
    prepared.processed(0,{640,480});prepared.processed(99,{100,100});
    auto copy=static_cast<const VkPresentRegionsKHR*>(prepared.info().pNext);
    require(copy!=&regionInfo&&copy->pRegions!=regions,"metadata and region list must have owned lifetime");
    require(copy->pRegions[0].rectangleCount==1,"processed image has a full damage rectangle");
    const auto &full=copy->pRegions[0].pRectangles[0];
    require(full.offset.x==0&&full.offset.y==0&&full.extent.width==640&&full.extent.height==480&&full.layer==0,
            "spatial/color effects must not retain partial presentation damage");
    require(copy->pRegions[1].pRectangles==&damage&&copy->pRegions[1].rectangleCount==1,"unprocessed images retain their original damage hints");
    auto copiedIds=static_cast<const VkPresentIdKHR*>(copy->pNext);
    auto copiedTimes=static_cast<const VkPresentTimesInfoGOOGLE*>(copiedIds->pNext);
    auto copiedFences=static_cast<const VkSwapchainPresentFenceInfoEXT*>(copiedTimes->pNext);
    auto copiedModes=static_cast<const VkSwapchainPresentModeInfoEXT*>(copiedFences->pNext);
    require(copiedIds!=&idInfo&&copiedIds->pPresentIds==ids&&copiedTimes!=&timeInfo&&copiedTimes->pTimes==times
            &&copiedFences!=&fenceInfo&&copiedFences->pFences==fences&&copiedModes!=&modeInfo
            &&copiedModes->pPresentModes==modes&&!copiedModes->pNext,"preserve metadata order/values without editing the caller's pNext");
    require(std::memcmp(&source,sourceBefore.data(),sizeof(source))==0&&std::memcmp(&regionInfo,regionsBefore.data(),sizeof(regionInfo))==0
            &&regions[0].pRectangles==&damage&&damage.offset.x==4,"native structures must remain byte-identical");
    require(prepared.prepare(source,&error),"repeated present resets owned damage");
    copy=static_cast<const VkPresentRegionsKHR*>(prepared.info().pNext);
    require(copy->pRegions[0].pRectangles==&damage,"no full-damage state leaked into a later frame");
    modeInfo.pNext=&regionInfo;
    require(!prepared.prepare(source,&error)&&error.find("Cyclic")!=std::string::npos,"cycle must fail closed");
    modeInfo.pNext=nullptr;
    VkPresentIdKHR duplicate=idInfo;duplicate.pNext=nullptr;modeInfo.pNext=&duplicate;
    require(!prepared.prepare(source,&error)&&error.find("Duplicate")!=std::string::npos,"duplicates must fail closed");
    modeInfo.pNext=nullptr;
    regionInfo.swapchainCount=1;require(!prepared.prepare(source,&error),"mismatched region counts must bypass");regionInfo.swapchainCount=2;
    idInfo.pPresentIds=nullptr;require(!prepared.prepare(source,&error),"missing ID arrays must bypass");idInfo.pPresentIds=ids;
    VkBaseInStructure unknown{VK_STRUCTURE_TYPE_DEVICE_GROUP_PRESENT_INFO_KHR,nullptr};source.pNext=&unknown;
    require(!prepared.prepare(source,&error)&&error.find("no audited")!=std::string::npos,"unknown/device-group extensions must bypass, not be stripped");
    source.pNext=nullptr;source.swapchainCount=33;require(!prepared.prepare(source,&error),"swapchain bound");
    source.swapchainCount=2;source.waitSemaphoreCount=65;require(!prepared.prepare(source,&error),"wait bound");
    source.waitSemaphoreCount=1;source.pWaitSemaphores=nullptr;require(!prepared.prepare(source,&error),"missing waits");
    source.pWaitSemaphores=&wait;source.pSwapchains=nullptr;require(!prepared.prepare(source,&error),"missing swapchain array");
    std::cout<<"PASS: bounded Vulkan present-extension copies, native metadata/result preservation, per-image full damage, repeated frames, unknown/cyclic/duplicate/malformed-chain refusal\n";
    std::cout<<"LIMIT: extension contract unit test, not actual Vulkan WSI hooks or a physical GPU/emulator\n";
}
