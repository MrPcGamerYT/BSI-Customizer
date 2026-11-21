#pragma once
#include "Parameters.h"
#include "RendererCapabilities.h"
#include "VulkanCapture.h"
#include "TextureQuality.h"
#include <cstdint>
#include <mutex>
#include <string>

namespace BsiShaders {
struct Metrics {
    bool hooks=false, observed=false, active=false, deviceReady=false;
    Renderer renderer=Renderer::Unknown;
    RendererFamily requested=RendererFamily::Automatic;
    // Configuration is a hint, not proof of the host's game presentation API.
    RendererFamily configured=RendererFamily::Automatic;
    std::uint64_t processed=0;
    VulkanCapture vulkan;
    bool textureSupported=false;
    std::uint64_t textureDraws=0,textureBindings=0;
    std::string textureStatus="Potato graphic is off; native texture sampling retained.";
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
    // Explicit family selection is retained for isolated backend qualification.
    void selectRenderer(RendererFamily requested);
    // The player always uses automatic, presentation-proven routing. Its
    // graphics setting is retained separately for diagnostics, never enforced.
    void configureRenderer(RendererFamily configured);
    bool accepts(Renderer observed);
    // Call only inside an ownership-checked real presentation callback, BEFORE
    // touching pixels. Atomic arbitration prevents concurrent/nested providers
    // (including translation layers in the same API family) grading twice.
    bool claimPresentation(Renderer renderer,std::uintptr_t object,std::uintptr_t window);
    void releasePresentation(Renderer renderer,std::uintptr_t object);
    Parameters settings();
    void setTexture(const TextureConfiguration &settings);
    TextureConfiguration texture();
    void textureReport(unsigned bindings,const std::string &detail,bool supported);
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
    TextureConfiguration currentTexture;
    Metrics state;
    std::uintptr_t host=0,presentationObject=0,presentationWindow=0;
};
void startPresentationHooks();
void stopPresentationHooks();
} // namespace BsiShaders
