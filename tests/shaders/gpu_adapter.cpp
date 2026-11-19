#include "GlPipeline.h"
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
void bsi_test_release(){if(pipeline)pipeline->release();pipeline.reset();}
}
