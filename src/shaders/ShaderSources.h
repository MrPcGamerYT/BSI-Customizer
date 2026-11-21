#pragma once
#include <string>

// Independently authored MIT-licensed GLSL. No recovered/uploaded shader code.
namespace BsiShaders {
inline std::string vertexSource(bool es, bool core) {
    const std::string prefix = es ? "#version 100\n" : core ? "#version 150\n" : "#version 120\n";
    return prefix + (core ? "in vec2 position;out vec2 uv;\n" : "attribute vec2 position;varying vec2 uv;\n")
        + "void main(){uv=position*.5+.5;gl_Position=vec4(position,0.,1.);}\n";
}
// One independently authored effect body for desktop GL, GLES and Vulkan.
// This prevents renderer choices from silently changing the meaning of sliders.
inline constexpr const char effectBody[] = R"GLSL(
vec3 pixel(vec2 p){return SAMPLE(frame,clamp(p,texel*.5,vec2(1.)-texel*.5)).rgb;}
float luminance(vec3 p){return dot(p,vec3(.2126,.7152,.0722));}
void main(){
    vec4 original=SAMPLE(frame,uv);
    vec3 c=original.rgb;
    vec3 n=pixel(uv+vec2(0.,texel.y)),s=pixel(uv-vec2(0.,texel.y));
    vec3 e=pixel(uv+vec2(texel.x,0.)),w=pixel(uv-vec2(texel.x,0.));
    if(weights[7]>0.){
        float lo=min(luminance(c),min(min(luminance(n),luminance(s)),min(luminance(e),luminance(w))));
        float hi=max(luminance(c),max(max(luminance(n),luminance(s)),max(luminance(e),luminance(w))));
        float edge=smoothstep(.04,.25,hi-lo);
        float horizontal=abs(luminance(n)-luminance(s));
        float vertical=abs(luminance(e)-luminance(w));
        vec2 along=horizontal>vertical?vec2(texel.x*.5,0.):vec2(0.,texel.y*.5);
        c=mix(c,(pixel(uv+along)+pixel(uv-along))*.5,weights[7]*edge*.75);
    }
    if(weights[6]>0.){
        vec3 laplacian=c*4.-n-s-e-w;
        c=clamp(c+laplacian*weights[6]*.2,min(c,min(min(n,s),min(e,w))),max(c,max(max(n,s),max(e,w))));
    }
    if(weights[8]>0.){
        vec3 glow=vec3(0.);
        for(int y=-2;y<=2;++y)for(int x=-2;x<=2;++x){
            vec3 tap=pixel(uv+vec2(float(x),float(y))*texel*3.);
            float weight=(3.-abs(float(x)))*(3.-abs(float(y)))/81.;
            glow+=max(tap-vec3(.65),vec3(0.))*weight;
        }
        c+=glow*weights[8]*.8;
    }
    c=mix(vec3(luminance(c)),c,1.+weights[0]);
    c=(c-.5)*(1.+weights[1])+.5;
    c+=vec3(.08,0.,-.08)*weights[2];
    c+=weights[3]*.18*pow(vec3(1.)-clamp(c,0.,1.),vec3(2.));
    c+=weights[4]*.55*(c-.5)*(vec3(1.)-abs(2.*c-1.));
    c+=weights[5]*.08;
    if(weights[9]>0.){
        vec2 p=(uv-.5)*1.41421356;
        c*=1.-weights[9]*.65*smoothstep(.2,1.,dot(p,p));
    }
    OUTPUT=vec4(clamp(c,0.,1.),original.a);
}
)GLSL";
inline std::string fragmentSource(bool es, bool core) {
    std::string prefix = es ? "#version 100\nprecision highp float;\n" : core ? "#version 150\n" : "#version 120\n";
    prefix += core ? "in vec2 uv;out vec4 outputColor;\n#define SAMPLE texture\n#define OUTPUT outputColor\n"
                   : "varying vec2 uv;\n#define SAMPLE texture2D\n#define OUTPUT gl_FragColor\n";
    return prefix + "uniform sampler2D frame;uniform vec2 texel;uniform float weights[10];\n" + effectBody;
}
inline std::string vulkanVertexSource() {
    return "#version 450\nvoid main(){vec2 p=vec2((gl_VertexIndex<<1)&2,gl_VertexIndex&2);gl_Position=vec4(p*2.-1.,0.,1.);}\n";
}
inline std::string vulkanFragmentSource() {
    return std::string("#version 450\nlayout(location=0) out vec4 outputColor;\n"
        "layout(set=0,binding=0) uniform sampler2D frame;\n"
        "layout(push_constant,std430) uniform Parameters {float weights[10];vec2 texel;};\n"
        "#define SAMPLE texture\n#define OUTPUT outputColor\n#define uv (gl_FragCoord.xy*texel)\n") + effectBody;
}
} // namespace BsiShaders
