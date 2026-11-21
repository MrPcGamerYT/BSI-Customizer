#pragma once
#include "Parameters.h"
#include "ShaderSources.h"
#include <string>

namespace BsiShaders {
inline std::string hlslVertexSource(){
    return "float4 main(uint id:SV_VertexID):SV_Position{float2 p=float2((id<<1)&2,id&2);return float4(p*2.-1.,0.,1.);}";
}
inline std::string hlslFragmentSource(){
    std::string body=effectBody;
    const auto replace=[&](const std::string &from,const std::string &to){
        for(std::size_t i=0;(i=body.find(from,i))!=std::string::npos;i+=to.size())body.replace(i,from.size(),to);
    };
    // HLSL constant-buffer arrays have different packing from GLSL push
    // constants. Explicit packed float4s preserve the same 12-float wire data.
    for(std::size_t i=0;i<controls.size();++i)replace("weights["+std::to_string(i)+"]","packed["+std::to_string(i/4)+"]["+std::to_string(i%4)+"]");
    replace("void main()","float4 main(float4 position:SV_Position):SV_Target");
    replace("OUTPUT=","return ");
    replace("vec3(luminance(c))","float3(luminance(c),luminance(c),luminance(c))");
    for(const std::string scalar:{"0.","1.","2.",".65"}){
        replace("vec3("+scalar+")","float3("+scalar+","+scalar+","+scalar+")");
        replace("vec2("+scalar+")","float2("+scalar+","+scalar+")");
    }
    return std::string("Texture2D frame:register(t0);SamplerState sourceSampler:register(s0);\n"
        "cbuffer Parameters:register(b0){float4 packed[3];};\n"
        "#define texel packed[2].zw\n#define uv (position.xy*texel)\n"
        "#define vec2 float2\n#define vec3 float3\n#define vec4 float4\n#define mix lerp\n"
        "#define SAMPLE(t,p) t.Sample(sourceSampler,p)\n")+body;
}
} // namespace BsiShaders
