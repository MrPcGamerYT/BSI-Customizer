#pragma once
#include <array>

namespace BsiShaders {
// These describe the GAME's observed presentation API, not the renderer used
// by Qt to draw Settings, and not merely a DLL found in the process.
enum class Renderer { Unknown, OpenGL, OpenGLES, Direct3D11, Direct3D12, Vulkan };
enum class RendererFamily { Automatic, OpenGL, DirectX, Vulkan };
inline constexpr RendererFamily family(Renderer renderer) {
    switch(renderer) {
    case Renderer::OpenGL: case Renderer::OpenGLES: return RendererFamily::OpenGL;
    case Renderer::Direct3D11: case Renderer::Direct3D12: return RendererFamily::DirectX;
    case Renderer::Vulkan: return RendererFamily::Vulkan;
    default: return RendererFamily::Automatic;
    }
}
inline constexpr bool matches(RendererFamily requested, Renderer observed) {
    return observed != Renderer::Unknown
        && (requested == RendererFamily::Automatic || requested == family(observed));
}
struct RendererCapability {
    Renderer renderer;
    const char *id;
    const char *label;
    bool pipelineImplemented;
};
inline constexpr std::array<RendererCapability, 5> rendererCapabilities {{
    {Renderer::OpenGL, "opengl", "OpenGL", true},
    {Renderer::OpenGLES, "opengles", "OpenGL ES / EGL", true},
    {Renderer::Direct3D11, "d3d11", "DirectX 11", true},
    {Renderer::Direct3D12, "d3d12", "DirectX 12", true},
    {Renderer::Vulkan, "vulkan", "Vulkan", true}
}};
inline constexpr RendererCapability capability(Renderer renderer) {
    for (const auto &entry : rendererCapabilities)
        if (entry.renderer == renderer) return entry;
    return {Renderer::Unknown, "unknown", "Not detected", false};
}
// No GPU-vendor allowlist: an adapter is eligible only because the actual game
// surface/API was identified and its required device capabilities are present.
// Implemented != validated on a real Intel/AMD/NVIDIA emulator. In particular,
// listing a planned backend must never silently make it usable.
inline constexpr bool canRoute(Renderer observedGameApi, bool gameSurfaceIdentified,
                               bool deviceRequirementsMet,
                               RendererFamily requested = RendererFamily::Automatic) {
    return gameSurfaceIdentified && deviceRequirementsMet && matches(requested, observedGameApi)
        && capability(observedGameApi).pipelineImplemented;
}
} // namespace BsiShaders
