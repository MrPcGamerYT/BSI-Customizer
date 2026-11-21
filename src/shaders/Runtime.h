#pragma once
#include "Parameters.h"
#include "RendererCapabilities.h"
#include "VulkanCapture.h"
#include "TextureQuality.h"
#include <array>
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
    // Since the latest texture setting/presentation-source change, not lifetime
    // success carried over from a different game context or an earlier toggle.
    std::uint64_t textureDraws=0,textureBindings=0,textureInspected=0,textureSceneDraws=0;
    std::uint64_t textureDepthBypasses=0,textureBlendBypasses=0,textureSamplerBypasses=0,textureOtherBypasses=0;
    std::array<std::uint64_t,static_cast<std::size_t>(TextureDepthFailure::Count)> textureDepthReasons{};
    std::uint64_t textureNestedInspected=0,textureNestedDraws=0;
    std::string textureLastBypass;
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
    // A draw hook may use an already selected source, NEVER claim a route from
    // a draw/context alone. Also authorizes a scene context on its exact drawable.
    bool isPresentation(Renderer renderer,std::uintptr_t object,std::uintptr_t window);
    void releasePresentation(Renderer renderer,std::uintptr_t object);
    Parameters settings();
    void setTexture(const TextureConfiguration &settings);
    TextureConfiguration texture();
    void textureReport(unsigned bindings,const std::string &detail,bool supported,
                       TextureDrawReason reason=TextureDrawReason::NotInspected,
                       TextureDepthFailure depthFailure=TextureDepthFailure::None,bool nested=false);
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
    void resetTextureEvidence(); // caller holds mutex
    std::mutex mutex;
    Parameters current;
    TextureConfiguration currentTexture;
    Metrics state;
    std::uintptr_t host=0,presentationObject=0,presentationWindow=0;
};
void startPresentationHooks();
void stopPresentationHooks();
} // namespace BsiShaders
