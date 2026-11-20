// Portable experimental sampler-policy tests, NOT game hook/GPU certification.
#include "TextureQuality.h"
#include "GlTextureQuality.h"
#include "VulkanTextureQuality.h"
#ifdef _WIN32
#include "D3dTextureQuality.h"
#endif
#include <array>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <limits>

using namespace BsiShaders;
namespace {
void require(bool value,const char *message){if(!value){std::cerr<<"FAIL: "<<message<<'\n';std::exit(1);}}
TextureQualityProof qualified(Renderer renderer=Renderer::OpenGL){
    TextureQualityProof p;p.renderer=renderer;p.requested=family(renderer);p.use=TextureUse::SampledColor;
    p.gameSurfaceIdentified=p.resourceOwnershipProven=p.privateReplacementSupported=p.externalOverridesReviewed=true;
    p.visibleMipLevels=7;return p;
}
TextureSampling ordinary(){TextureSampling s;s.maximumLod=1000;s.mipmapped=true;return s;}
// Check every semantic field (including NaN payloads), never unspecified
// structure padding that a different compiler may choose not to copy.
bool sameFloat(const float &a,const float &b){return std::memcmp(&a,&b,sizeof(float))==0;}
bool sameSampling(const TextureSampling &a,const TextureSampling &b){
    return sameFloat(a.minimumLod,b.minimumLod)&&sameFloat(a.maximumLod,b.maximumLod)&&sameFloat(a.lodBias,b.lodBias)
        &&a.mipmapped==b.mipmapped&&a.comparison==b.comparison&&a.anisotropic==b.anisotropic
        &&a.customReduction==b.customReduction&&a.unnormalizedCoordinates==b.unnormalizedCoordinates;
}
bool sameGl(const GlQualitySampler &a,const GlQualitySampler &b){
    return a.minimumFilter==b.minimumFilter&&a.comparisonMode==b.comparisonMode
        &&sameFloat(a.minimumLod,b.minimumLod)&&sameFloat(a.maximumLod,b.maximumLod)
        &&sameFloat(a.lodBias,b.lodBias)&&sameFloat(a.maximumAnisotropy,b.maximumAnisotropy);
}
bool sameVulkan(const VkSamplerCreateInfo &a,const VkSamplerCreateInfo &b){
    return a.sType==b.sType&&a.pNext==b.pNext&&a.flags==b.flags&&a.magFilter==b.magFilter&&a.minFilter==b.minFilter
        &&a.mipmapMode==b.mipmapMode&&a.addressModeU==b.addressModeU&&a.addressModeV==b.addressModeV&&a.addressModeW==b.addressModeW
        &&sameFloat(a.mipLodBias,b.mipLodBias)&&a.anisotropyEnable==b.anisotropyEnable&&sameFloat(a.maxAnisotropy,b.maxAnisotropy)
        &&a.compareEnable==b.compareEnable&&a.compareOp==b.compareOp&&sameFloat(a.minLod,b.minLod)&&sameFloat(a.maxLod,b.maxLod)
        &&a.borderColor==b.borderColor&&a.unnormalizedCoordinates==b.unnormalizedCoordinates;
}
#ifdef _WIN32
template<typename T>bool sameDirectx(const T &a,const T &b){
    if(a.Filter!=b.Filter||a.AddressU!=b.AddressU||a.AddressV!=b.AddressV||a.AddressW!=b.AddressW
       ||!sameFloat(a.MipLODBias,b.MipLODBias)||a.MaxAnisotropy!=b.MaxAnisotropy||a.ComparisonFunc!=b.ComparisonFunc
       ||!sameFloat(a.MinLOD,b.MinLOD)||!sameFloat(a.MaxLOD,b.MaxLOD))return false;
    if constexpr(std::is_same_v<T,D3D12_STATIC_SAMPLER_DESC>){
        return a.BorderColor==b.BorderColor&&a.ShaderRegister==b.ShaderRegister&&a.RegisterSpace==b.RegisterSpace&&a.ShaderVisibility==b.ShaderVisibility;
    }else{
        for(unsigned i=0;i<4;++i)if(!sameFloat(a.BorderColor[i],b.BorderColor[i]))return false;
        return true;
    }
}
#endif
void policy(){
    auto source=ordinary();auto proof=qualified();
    for(const auto &api:rendererCapabilities){
        proof=qualified(api.renderer);
        for(auto mode:{TextureQuality::Light,TextureQuality::Strong}){
            const auto p=planTextureQuality(mode,proof,source);
            require(p.changed()&&p.sampling.minimumLod==(mode==TextureQuality::Light?1.5f:3.f),"same mip floor on all five identified APIs");
            require(p.sampling.maximumLod==source.maximumLod&&p.sampling.lodBias==0,"only the floor changes, never host bias/max LOD");
        }
    }
    proof=qualified();
    const auto bypass=[&](TextureQualityDecision expected){
        const auto p=planTextureQuality(TextureQuality::Strong,proof,source);
        require(!p.changed()&&p.decision==expected,textureQualityDiagnostic(expected));
        require(sameSampling(p.sampling,source),"every refusal preserves all source sampling values");
    };
    require(!planTextureQuality(TextureQuality::Native,proof,source).changed(),"native is a no-op");
    require(planTextureQuality(static_cast<TextureQuality>(99),proof,source).decision==TextureQualityDecision::InvalidMode,"reject unknown mode");
    proof.gameSurfaceIdentified=false;bypass(TextureQualityDecision::UnidentifiedRenderer);proof=qualified();
    proof.renderer=Renderer::Unknown;bypass(TextureQualityDecision::UnidentifiedRenderer);proof=qualified();
    proof.renderer=static_cast<Renderer>(99);proof.requested=RendererFamily::Automatic;bypass(TextureQualityDecision::UnidentifiedRenderer);proof=qualified();
    proof.requested=RendererFamily::Vulkan;bypass(TextureQualityDecision::UnidentifiedRenderer);proof=qualified();
    for(auto use:{TextureUse::Unknown,TextureUse::UserInterface,TextureUse::Depth,TextureUse::Integer,TextureUse::RenderTarget,TextureUse::Presentation}){
        proof.use=use;bypass(TextureQualityDecision::UnqualifiedResource);
    }
    proof=qualified();proof.resourceOwnershipProven=false;bypass(TextureQualityDecision::UnqualifiedResource);proof=qualified();
    proof.privateReplacementSupported=false;bypass(TextureQualityDecision::UnsupportedReplacement);proof=qualified();
    proof.protectedContent=true;bypass(TextureQualityDecision::ProtectedContent);proof=qualified();
    proof.externalOverridesReviewed=false;bypass(TextureQualityDecision::ExternalOverridesUnverified);proof=qualified();
    for(unsigned mips:{0u,1u,33u,std::numeric_limits<unsigned>::max()}){proof.visibleMipLevels=mips;bypass(TextureQualityDecision::NoMipChain);}
    proof=qualified();source.mipmapped=false;bypass(TextureQualityDecision::NoMipChain);source=ordinary();
    source.comparison=true;bypass(TextureQualityDecision::UnsupportedSampling);source=ordinary();
    source.anisotropic=true;bypass(TextureQualityDecision::UnsupportedSampling);source=ordinary();
    source.customReduction=true;bypass(TextureQualityDecision::UnsupportedSampling);source=ordinary();
    source.unnormalizedCoordinates=true;bypass(TextureQualityDecision::UnsupportedSampling);source=ordinary();
    for(float invalid:{-1.f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}){
        source.minimumLod=invalid;bypass(TextureQualityDecision::InvalidSampling);source=ordinary();
    }
    source.maximumLod=-1;bypass(TextureQualityDecision::InvalidSampling);source=ordinary();
    source.maximumLod=std::numeric_limits<float>::infinity();bypass(TextureQualityDecision::InvalidSampling);source=ordinary();
    source.lodBias=std::numeric_limits<float>::quiet_NaN();bypass(TextureQualityDecision::InvalidSampling);source=ordinary();
    for(float bias:{-2.f,.5f,3.f}){source.lodBias=bias;bypass(TextureQualityDecision::ExistingLodOverride);}
    source=ordinary();source.minimumLod=.5f;bypass(TextureQualityDecision::ExistingLodOverride);source=ordinary();
    source.maximumLod=0;bypass(TextureQualityDecision::NoCoarserLevel);source=ordinary();
    source.maximumLod=.75f;require(planTextureQuality(TextureQuality::Strong,proof,source).sampling.minimumLod==.75f,"respect sampler's existing max LOD");source=ordinary();
    proof.visibleMipLevels=2;require(planTextureQuality(TextureQuality::Strong,proof,source).sampling.minimumLod==1,"respect view-relative mip bounds");
    TextureQualityProof defaults;require(!planTextureQuality(TextureQuality::Strong,defaults,source).changed(),"default proof never opts a game into modification");
    for(unsigned i=0;i<=static_cast<unsigned>(TextureQualityDecision::UnsupportedDescriptor);++i)
        require(std::strlen(textureQualityDiagnostic(static_cast<TextureQualityDecision>(i)))>12,"every decision has an honest diagnostic");
}
void gl(){
    auto proof=qualified();GlQualitySampler source;source.minimumFilter=0x2703;source.maximumLod=1000;
    const auto before=source;
    const auto p=planGlTextureQuality(TextureQuality::Light,proof,source);
    require(p.changed()&&p.descriptor.minimumLod==1.5f,"GL minimum-LOD adapter");
    require(sameGl(before,source),"GL source untouched");
    auto expected=source;expected.minimumLod=1.5f;require(sameGl(p.descriptor,expected),"GL preserves remaining sampling fields");
    proof.renderer=Renderer::OpenGLES;require(planGlTextureQuality(TextureQuality::Strong,proof,source).changed(),"GLES uses min LOD, not unsupported texture bias");
    source.minimumLod=0;require(planGlTextureQuality(TextureQuality::Strong,proof,source).changed(),"GL also accepts an explicit neutral zero floor");source=before;
    for(float existing:{-2.f,.5f}){
        source.minimumLod=existing;const auto refused=planGlTextureQuality(TextureQuality::Strong,proof,source);
        require(!refused.changed()&&refused.descriptor.minimumLod==existing,"nondefault GL floors stay untouched");
    }
    source=before;
    proof.renderer=Renderer::Vulkan;proof.requested=RendererFamily::Vulkan;require(!planGlTextureQuality(TextureQuality::Strong,proof,source).changed(),"GL adapter cannot accept Vulkan proof");
    proof=qualified();proof.privateReplacementSupported=false;require(!planGlTextureQuality(TextureQuality::Strong,proof,source).changed(),"GL/GLES without sampler objects safely bypass");proof=qualified();
    source.minimumFilter=0x2601;require(!planGlTextureQuality(TextureQuality::Strong,proof,source).changed(),"nonmip filter bypass");source=before;
    source.minimumFilter=0xdead;require(!planGlTextureQuality(TextureQuality::Strong,proof,source).changed(),"unknown filter bypass");source=before;
    source.comparisonMode=0x884e;require(!planGlTextureQuality(TextureQuality::Strong,proof,source).changed(),"comparison sampler bypass");source=before;
    source.maximumAnisotropy=16;require(!planGlTextureQuality(TextureQuality::Strong,proof,source).changed(),"existing anisotropy preserved");
}
void vulkan(){
    auto proof=qualified(Renderer::Vulkan);
    VkSamplerCreateInfo source{};source.sType=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;source.maxLod=VK_LOD_CLAMP_NONE;
    source.minFilter=VK_FILTER_LINEAR;source.magFilter=VK_FILTER_NEAREST;source.mipmapMode=VK_SAMPLER_MIPMAP_MODE_LINEAR;
    source.addressModeU=VK_SAMPLER_ADDRESS_MODE_REPEAT;source.addressModeV=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    source.borderColor=VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    const auto before=source;
    const auto p=planVulkanTextureQuality(TextureQuality::Strong,proof,source);
    require(p.changed()&&p.descriptor.minLod==3,"Vulkan min-LOD adapter");
    auto expected=source;expected.minLod=3;
    require(sameVulkan(p.descriptor,expected)&&sameVulkan(before,source),"Vulkan preserves input/all non-LOD descriptor fields");
    proof.renderer=Renderer::OpenGL;proof.requested=RendererFamily::OpenGL;require(!planVulkanTextureQuality(TextureQuality::Strong,proof,source).changed(),"Vulkan adapter cannot accept GL proof");proof=qualified(Renderer::Vulkan);
    const auto reject=[&]{const auto r=planVulkanTextureQuality(TextureQuality::Strong,proof,source);require(!r.changed()&&sameVulkan(r.descriptor,source),"Vulkan refusal preserves original descriptor");};
    VkBaseInStructure extension{};source.pNext=&extension;reject();source=before;
    source.flags=1;reject();source=before;
    source.unnormalizedCoordinates=VK_TRUE;reject();source=before;
    source.compareEnable=VK_TRUE;reject();source=before;
    source.anisotropyEnable=VK_TRUE;reject();source=before;
    source.minFilter=static_cast<VkFilter>(0x7fffffff);reject();source=before;
    source.mipmapMode=static_cast<VkSamplerMipmapMode>(0x7fffffff);reject();
}
#ifdef _WIN32
void directx(){
    auto proof=qualified(Renderer::Direct3D11);
    D3D11_SAMPLER_DESC d11{};d11.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;d11.MaxLOD=D3D11_FLOAT32_MAX;d11.AddressU=D3D11_TEXTURE_ADDRESS_CLAMP;
    d11.AddressV=D3D11_TEXTURE_ADDRESS_MIRROR;d11.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;d11.MaxAnisotropy=1;d11.ComparisonFunc=D3D11_COMPARISON_ALWAYS;
    d11.BorderColor[0]=.25f;d11.BorderColor[1]=.5f;d11.BorderColor[2]=.75f;d11.BorderColor[3]=1;
    const auto before11=d11;auto expected11=d11;expected11.MinLOD=1.5f;
    const auto p11=planD3dTextureQuality(TextureQuality::Light,proof,d11);
    require(p11.changed()&&sameDirectx(p11.descriptor,expected11)&&sameDirectx(d11,before11),"D3D11 private descriptor changes only minimum LOD");
    D3D12_SAMPLER_DESC d12{};d12.Filter=D3D12_FILTER_MIN_MAG_MIP_LINEAR;d12.MaxLOD=D3D12_FLOAT32_MAX;
    d12.AddressU=D3D12_TEXTURE_ADDRESS_MODE_CLAMP;d12.AddressV=D3D12_TEXTURE_ADDRESS_MODE_MIRROR;d12.AddressW=D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    d12.MaxAnisotropy=1;d12.ComparisonFunc=D3D12_COMPARISON_FUNC_ALWAYS;d12.BorderColor[0]=.25f;d12.BorderColor[1]=.5f;d12.BorderColor[2]=.75f;d12.BorderColor[3]=1;
    const auto before12=d12;auto expected12=d12;expected12.MinLOD=3;
    require(!planD3dTextureQuality(TextureQuality::Strong,proof,d12).changed(),"D3D12 descriptor cannot accept D3D11 proof in the same renderer family");
    proof=qualified(Renderer::Direct3D12);
    require(!planD3dTextureQuality(TextureQuality::Strong,proof,d11).changed(),"D3D11 descriptor cannot accept D3D12 proof in the same renderer family");
    const auto p12=planD3dTextureQuality(TextureQuality::Strong,proof,d12);
    require(p12.changed()&&sameDirectx(p12.descriptor,expected12)&&sameDirectx(d12,before12),"D3D12 dynamic descriptor changes only minimum LOD");
    D3D12_STATIC_SAMPLER_DESC stat{};stat.Filter=D3D12_FILTER_MIN_MAG_MIP_POINT;stat.MaxLOD=D3D12_FLOAT32_MAX;stat.ShaderRegister=3;stat.RegisterSpace=2;stat.ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
    const auto ps=planD3dTextureQuality(TextureQuality::Strong,proof,stat);auto expected=stat;expected.MinLOD=3;
    require(ps.changed()&&sameDirectx(ps.descriptor,expected),"D3D12 static sampler clone preserves binding/register fields");
    for(auto filter:{D3D12_FILTER_ANISOTROPIC,D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR,D3D12_FILTER_MINIMUM_MIN_MAG_MIP_POINT,D3D12_FILTER_MAXIMUM_MIN_MAG_MIP_LINEAR}){
        d12.Filter=filter;const auto p=planD3dTextureQuality(TextureQuality::Strong,proof,d12);
        require(!p.changed()&&sameDirectx(p.descriptor,d12),"D3D comparison/reduction/aniso descriptor preserved");
    }
    proof=qualified(Renderer::Vulkan);require(!planD3dTextureQuality(TextureQuality::Strong,proof,stat).changed(),"D3D adapter cannot accept Vulkan proof");
}
#endif
}
int main(){
    policy();gl();vulkan();
#ifdef _WIN32
    directx();
#endif
    std::cout<<"PASS: bounded vendor-neutral texture-quality policy, all five API routes, typed sampler plans, unchanged inputs, view mip bounds, existing overrides and conservative refusals\n";
    std::cout<<"LIMIT: descriptor policy only, not game-resource interception, physical GPU, NVIDIA profile, performance or installed-player validation\n";
}
