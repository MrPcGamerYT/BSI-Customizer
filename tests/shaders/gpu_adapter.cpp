#include "GlPipeline.h"
#include "GlTextureQuality.h"
#include <cstring>
#include <memory>

// Test-only C bridge to the production pipeline, not a reimplementation.
namespace { std::unique_ptr<BsiShaders::GlPipeline> pipeline; }
extern "C" {
int bsi_test_initialize(BsiShaders::ResolveGl resolve,void *user,char *message,int capacity){
    pipeline=std::make_unique<BsiShaders::GlPipeline>();std::string error;
    const bool okay=pipeline->initialize(resolve,user,&error);
    if(capacity>0){std::strncpy(message,error.c_str(),capacity-1);message[capacity-1]=0;}return okay;
}
int bsi_test_render(int width,int height,int enabled,const int *values,char *message,int capacity){
    BsiShaders::Parameters p;p.enabled=enabled!=0;
    for(std::size_t i=0;i<p.values.size();++i)p.values[i]=values[i];
    std::string error;const bool okay=pipeline&&pipeline->render(width,height,p,&error);
    if(capacity>0){std::strncpy(message,error.c_str(),capacity-1);message[capacity-1]=0;}return okay;
}
void bsi_test_reference(const float *input,const int *values,float *output){
    BsiShaders::Parameters p;p.enabled=true;
    for(std::size_t i=0;i<p.values.size();++i)p.values[i]=values[i];
    const auto result=BsiShaders::grade({input[0],input[1],input[2]},p);
    for(int i=0;i<3;++i)output[i]=result[i];
}
// Test-owned mip resources supply explicit qualification. This bridge is not
// linked into the plugin and cannot assert ownership/profile safety in a game.
int bsi_test_quality_minimum(int mode,int es,int supported,unsigned mips,float minimum,float maximum,float bias,int reviewed,float *out){
    using namespace BsiShaders;
    TextureQualityProof proof;proof.renderer=es?Renderer::OpenGLES:Renderer::OpenGL;proof.requested=RendererFamily::OpenGL;
    proof.use=TextureUse::SampledColor;proof.gameSurfaceIdentified=proof.resourceOwnershipProven=true;
    proof.privateReplacementSupported=supported!=0;proof.externalOverridesReviewed=reviewed!=0;proof.visibleMipLevels=mips;
    GlQualitySampler source;source.minimumFilter=0x2703;source.minimumLod=minimum;source.maximumLod=maximum;source.lodBias=bias;
    const auto plan=planGlTextureQuality(static_cast<TextureQuality>(mode),proof,source);
    *out=plan.descriptor.minimumLod;return plan.changed();
}
void bsi_test_release(){if(pipeline)pipeline->release();pipeline.reset();}
}
