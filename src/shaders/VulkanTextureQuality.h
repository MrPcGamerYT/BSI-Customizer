#pragma once
#include "TextureQuality.h"
#include <vulkan/vulkan_core.h>

namespace BsiShaders {
// Prepare data for a NEW sampler. This never modifies an existing VkSampler,
// updates a descriptor, changes enabled device features or touches a pNext chain.
inline TextureSamplerPlan<VkSamplerCreateInfo> planVulkanTextureQuality(
    TextureQuality mode,const TextureQualityProof &proof,const VkSamplerCreateInfo &source){
    TextureSamplerPlan<VkSamplerCreateInfo> result{source,TextureQualityDecision::UnsupportedDescriptor};
    if(mode==TextureQuality::Native){result.decision=TextureQualityDecision::Disabled;return result;}
    if(proof.renderer!=Renderer::Vulkan){result.decision=TextureQualityDecision::UnidentifiedRenderer;return result;}
    if(source.sType!=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO||source.pNext||source.flags
       ||(source.minFilter!=VK_FILTER_NEAREST&&source.minFilter!=VK_FILTER_LINEAR)
       ||(source.magFilter!=VK_FILTER_NEAREST&&source.magFilter!=VK_FILTER_LINEAR)
       ||(source.mipmapMode!=VK_SAMPLER_MIPMAP_MODE_NEAREST&&source.mipmapMode!=VK_SAMPLER_MIPMAP_MODE_LINEAR))return result;
    TextureSampling sampling;
    sampling.minimumLod=source.minLod;sampling.maximumLod=source.maxLod;sampling.lodBias=source.mipLodBias;
    sampling.mipmapped=true;sampling.comparison=source.compareEnable!=VK_FALSE;
    sampling.anisotropic=source.anisotropyEnable!=VK_FALSE;
    sampling.unnormalizedCoordinates=source.unnormalizedCoordinates!=VK_FALSE;
    const auto plan=planTextureQuality(mode,proof,sampling);
    result.decision=plan.decision;if(plan.changed())result.descriptor.minLod=plan.sampling.minimumLod;
    return result;
}
} // namespace BsiShaders
