#pragma once
#include "TextureQuality.h"
#ifdef _WIN32
#include <d3d11.h>
#include <d3d12.h>
#include <type_traits>

namespace BsiShaders {
// D3D11, D3D12 dynamic and D3D12 static/root samplers share these LOD fields.
// A caller must separately qualify the bound color SRV, ownership and safe
// descriptor/root-signature replacement. A sampler by itself proves none of it.
template<typename Descriptor>TextureSamplerPlan<Descriptor> planD3dTextureQuality(
    TextureQuality mode,const TextureQualityProof &proof,const Descriptor &source){
    TextureSamplerPlan<Descriptor> result{source,TextureQualityDecision::UnsupportedDescriptor};
    if(mode==TextureQuality::Native){result.decision=TextureQualityDecision::Disabled;return result;}
    static_assert(std::is_same_v<Descriptor,D3D11_SAMPLER_DESC>||std::is_same_v<Descriptor,D3D12_SAMPLER_DESC>
                  ||std::is_same_v<Descriptor,D3D12_STATIC_SAMPLER_DESC>,"Unsupported DirectX sampler descriptor");
    constexpr auto api=std::is_same_v<Descriptor,D3D11_SAMPLER_DESC>?Renderer::Direct3D11:Renderer::Direct3D12;
    if(proof.renderer!=api){result.decision=TextureQualityDecision::UnidentifiedRenderer;return result;}
    // Only the eight ordinary point/linear filters. Never turn comparison,
    // minimum/maximum reduction or anisotropic sampling into ordinary filtering.
    switch(static_cast<unsigned>(source.Filter)){
    case 0x00:case 0x01:case 0x04:case 0x05:case 0x10:case 0x11:case 0x14:case 0x15:break;
    default:return result;
    }
    TextureSampling sampling;
    sampling.minimumLod=source.MinLOD;sampling.maximumLod=source.MaxLOD;sampling.lodBias=source.MipLODBias;
    sampling.mipmapped=true;
    const auto plan=planTextureQuality(mode,proof,sampling);
    result.decision=plan.decision;if(plan.changed())result.descriptor.MinLOD=plan.sampling.minimumLod;
    return result;
}
} // namespace BsiShaders
#endif
