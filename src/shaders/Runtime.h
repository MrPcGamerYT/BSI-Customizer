#pragma once
#include "Parameters.h"
#include "RendererCapabilities.h"
#include "VulkanCapture.h"
#include <cstdint>
#include <mutex>
#include <string>

namespace BsiShaders {
struct Metrics {
    bool hooks=false, observed=false, active=false, deviceReady=false;
    Renderer renderer=Renderer::Unknown;
    RendererFamily requested=RendererFamily::Automatic;
    std::uint64_t processed=0;
    VulkanCapture vulkan;
    std::string status="Waiting for the game's presentation API. Multi-renderer support is not complete.";
};
// One state per HD-Player process; Qt objects never cross the render-thread
// boundary. The settings bridge copies bounded values through this mutex.
class Runtime {
public:
    static Runtime &instance();
    void start(std::uintptr_t playerWindow);
    void setWindow(std::uintptr_t playerWindow);
    void stop();
    void set(const Parameters &settings);
    void selectRenderer(RendererFamily requested);
    bool accepts(Renderer observed);
    Parameters settings();
    Metrics metrics();
    void waiting(bool hooks,const std::string &status);
    void vulkanEvent(VulkanEvent event,const std::string &detail={});
    std::string gameWindowDiagnostic(std::uintptr_t surface);
    void report(bool hooks,bool observed,bool active,const std::string &status,bool processed=false,
                Renderer renderer=Renderer::Unknown,bool deviceRequirementsMet=false);
    // Identity without a size gate: a game child may still be 1x1 while its
    // renderer creates a full-size swapchain, before Qt finishes layout.
    bool ownsGameWindow(std::uintptr_t surface);
    bool eligible(std::uintptr_t surface);
private:
    std::mutex mutex;
    Parameters current;
    Metrics state;
    std::uintptr_t host=0;
};
void startPresentationHooks();
void stopPresentationHooks();
} // namespace BsiShaders
