#pragma once
#include "RendererCapabilities.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

// Independently authored sampling policy, also used by the conservative GL
// draw-scoped private-sampler path. This is NOT a presentation filter. Callers
// must prove the resource contract and create a PRIVATE sampler replacement;
// never mutate a game's sampler or infer ownership from a loaded graphics DLL.
namespace BsiShaders {
enum class TextureQuality { Native, Light, Strong };
struct TextureConfiguration {
    bool enabled=false,externalOverridesReviewed=false;
    TextureQuality quality()const{return enabled?TextureQuality::Strong:TextureQuality::Native;}
};
enum class TextureUse { Unknown, SampledColor, UserInterface, Depth, Integer, RenderTarget, Presentation };
struct TextureQualityProof {
    Renderer renderer=Renderer::Unknown;
    RendererFamily requested=RendererFamily::Automatic;
    TextureUse use=TextureUse::Unknown;
    bool gameSurfaceIdentified=false;
    bool resourceOwnershipProven=false;
    bool privateReplacementSupported=false;
    bool protectedContent=false;
    // GL queries cannot prove that all external driver-profile overrides are
    // absent. Unknown is a bypass, NOT permission to reset or guess a profile.
    bool externalOverridesReviewed=false;
    std::uint32_t visibleMipLevels=0;
};
struct TextureSampling {
    float minimumLod=0,maximumLod=0,lodBias=0;
    bool mipmapped=false,comparison=false,anisotropic=false,customReduction=false;
    bool unnormalizedCoordinates=false;
};
enum class TextureQualityDecision {
    Applied, Disabled, InvalidMode, UnidentifiedRenderer, UnqualifiedResource,
    UnsupportedReplacement, ProtectedContent, ExternalOverridesUnverified,
    NoMipChain, UnsupportedSampling, InvalidSampling, ExistingLodOverride,
    NoCoarserLevel, UnsupportedDescriptor
};
struct TextureQualityPlan {
    TextureSampling sampling;
    TextureQualityDecision decision;
    bool changed()const{return decision==TextureQualityDecision::Applied;}
};
inline TextureQualityPlan planTextureQuality(TextureQuality mode,const TextureQualityProof &proof,
                                             const TextureSampling &source){
    TextureQualityPlan plan{source,TextureQualityDecision::Disabled};
    const auto refuse=[&](TextureQualityDecision why){plan.decision=why;return plan;};
    if(mode==TextureQuality::Native)return plan;
    if(mode!=TextureQuality::Light&&mode!=TextureQuality::Strong)return refuse(TextureQualityDecision::InvalidMode);
    if(!canRoute(proof.renderer,proof.gameSurfaceIdentified,true,proof.requested))return refuse(TextureQualityDecision::UnidentifiedRenderer);
    if(proof.use!=TextureUse::SampledColor||!proof.resourceOwnershipProven)return refuse(TextureQualityDecision::UnqualifiedResource);
    if(proof.protectedContent)return refuse(TextureQualityDecision::ProtectedContent);
    if(!proof.privateReplacementSupported)return refuse(TextureQualityDecision::UnsupportedReplacement);
    if(!proof.externalOverridesReviewed)return refuse(TextureQualityDecision::ExternalOverridesUnverified);
    if(proof.visibleMipLevels<2||proof.visibleMipLevels>32||!source.mipmapped)return refuse(TextureQualityDecision::NoMipChain);
    if(source.comparison||source.anisotropic||source.customReduction||source.unnormalizedCoordinates)
        return refuse(TextureQualityDecision::UnsupportedSampling);
    if(!std::isfinite(source.minimumLod)||!std::isfinite(source.maximumLod)||!std::isfinite(source.lodBias)
       ||source.minimumLod<0||source.maximumLod<source.minimumLod)
        return refuse(TextureQualityDecision::InvalidSampling);
    // Do not add our bias to an application's/profile's existing LOD policy.
    // A minimum-LOD floor also works in GLES 3, where texture LOD bias does not.
    if(source.minimumLod!=0||source.lodBias!=0)return refuse(TextureQualityDecision::ExistingLodOverride);
    const float last=static_cast<float>(proof.visibleMipLevels-1);
    const float ceiling=(std::min)(last,source.maximumLod);
    if(ceiling<=0)return refuse(TextureQualityDecision::NoCoarserLevel);
    plan.sampling.minimumLod=(std::min)(mode==TextureQuality::Light?1.5f:3.f,ceiling);
    plan.decision=TextureQualityDecision::Applied;
    return plan;
}
inline const char *textureQualityDiagnostic(TextureQualityDecision decision){
    switch(decision){
    case TextureQualityDecision::Applied:return "Private sampler minimum-LOD plan prepared; game integration and performance are not confirmed.";
    case TextureQualityDecision::Disabled:return "Native sampling retained.";
    case TextureQualityDecision::InvalidMode:return "Unknown texture-quality mode; native sampling retained.";
    case TextureQualityDecision::UnidentifiedRenderer:return "Game surface/API is unidentified or does not match the selected renderer.";
    case TextureQualityDecision::UnqualifiedResource:return "Only an ownership-proven sampled game-color resource qualifies.";
    case TextureQualityDecision::UnsupportedReplacement:return "A safe private sampler replacement is unavailable for this resource/API.";
    case TextureQualityDecision::ProtectedContent:return "Protected content is never modified.";
    case TextureQualityDecision::ExternalOverridesUnverified:return "External driver/profile overrides are unverified; native sampling retained.";
    case TextureQualityDecision::NoMipChain:return "No qualified mipmapped color view is available.";
    case TextureQualityDecision::UnsupportedSampling:return "Comparison, anisotropic, reduction or unnormalized sampling is not qualified.";
    case TextureQualityDecision::InvalidSampling:return "Invalid or nonfinite source LOD limits; native sampling retained.";
    case TextureQualityDecision::ExistingLodOverride:return "Existing minimum-LOD/bias overrides are preserved, not stacked or reset.";
    case TextureQualityDecision::NoCoarserLevel:return "The source sampler/view exposes no coarser LOD.";
    case TextureQualityDecision::UnsupportedDescriptor:return "Extended or unsupported sampler descriptor; native sampling retained.";
    }
    return "Unknown texture-quality decision; native sampling retained.";
}
template<typename Descriptor>struct TextureSamplerPlan {
    Descriptor descriptor;
    TextureQualityDecision decision;
    bool changed()const{return decision==TextureQualityDecision::Applied;}
};
} // namespace BsiShaders
