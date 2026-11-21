#pragma once
#include "TextureQuality.h"

namespace BsiShaders {
// Relevant fields from a caller-owned sampler, not arbitrary texture state.
// A private replacement must also copy the caller's full wrap/border/other state.
// GL >= 3.3 / GLES >= 3 supplies sampler objects and MIN_LOD. GL 2 / GLES 2
// must pass privateReplacementSupported=false and leave their state untouched.
struct GlQualitySampler {
    int minimumFilter=0x2601; // GL_LINEAR (no mip chain by default)
    int comparisonMode=0;   // GL_NONE
    float minimumLod=-1000,maximumLod=1000,lodBias=0,maximumAnisotropy=1;
};
struct GlQualityCapabilities {
    // Only a queried desktop GL_MAX_TEXTURE_LOD_BIAS enables additive bias.
    // Zero keeps the original portable floor-only plan. GLES must use zero:
    // it has no sampler TEXTURE_LOD_BIAS state, even when entry points exist.
    float maximumLodBias=0;
};
inline TextureSamplerPlan<GlQualitySampler> planGlTextureQuality(
    TextureQuality mode,const TextureQualityProof &proof,const GlQualitySampler &source,
    const GlQualityCapabilities &capabilities={}){
    TextureSamplerPlan<GlQualitySampler> result{source,TextureQualityDecision::UnsupportedDescriptor};
    if(mode==TextureQuality::Native){result.decision=TextureQualityDecision::Disabled;return result;}
    if(proof.renderer!=Renderer::OpenGL&&proof.renderer!=Renderer::OpenGLES){result.decision=TextureQualityDecision::UnidentifiedRenderer;return result;}
    if(!std::isfinite(capabilities.maximumLodBias)||capabilities.maximumLodBias<0
       ||(proof.renderer==Renderer::OpenGLES&&capabilities.maximumLodBias!=0))return result;
    // Keep unrecognized filters, comparison modes and invalid AF unchanged.
    if((source.minimumFilter!=0x2600&&source.minimumFilter!=0x2601
        &&(source.minimumFilter<0x2700||source.minimumFilter>0x2703))
       ||!std::isfinite(source.maximumAnisotropy)||source.maximumAnisotropy<1)return result;
    if(source.minimumLod<0&&source.minimumLod!=-1000.f&&std::isfinite(source.minimumLod)){
        result.decision=TextureQualityDecision::ExistingLodOverride;return result;
    }
    TextureSampling sampling;
    // GL/GLES sampler objects start at MIN_LOD=-1000, not the zero used by
    // Vulkan/DirectX. Treat only that exact API default as an unrestricted
    // floor for planning. Other nonzero floors are preserved overrides.
    // The source/default is still returned byte-for-byte on every bypass.
    sampling.minimumLod=source.minimumLod==-1000.f?0.f:source.minimumLod;
    sampling.maximumLod=source.maximumLod;sampling.lodBias=source.lodBias;
    sampling.mipmapped=source.minimumFilter>=0x2700&&source.minimumFilter<=0x2703;
    sampling.comparison=source.comparisonMode!=0;sampling.anisotropic=source.maximumAnisotropy>1;
    const auto plan=planTextureQuality(mode,proof,sampling);
    result.decision=plan.decision;
    if(plan.changed()){
        result.descriptor.minimumLod=plan.sampling.minimumLod;
        // A minimum floor alone does nothing when native lambda is already
        // above it. The independently tested desktop path also shifts lambda,
        // in a PRIVATE sampler only, bounded by view/max LOD and the queried
        // device limit. The shared proof/refusals ran first: never stack an
        // existing source bias/floor or authorize a resource by capability.
        if(proof.renderer==Renderer::OpenGL&&capabilities.maximumLodBias>0)
            result.descriptor.lodBias=(std::min)(plan.sampling.minimumLod,capabilities.maximumLodBias);
    }
    return result;
}
} // namespace BsiShaders
