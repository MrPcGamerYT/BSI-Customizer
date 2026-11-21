#pragma once
#include <array>
#include <cstdint>

// One signature/alias inventory for the Windows draw detours and their dispatch
// tests. A translator can use base-vertex, multi-draw or indirect entry points
// for the scene while using plain DrawArrays only for its final UI/compositor.
// Every alias in a row has the SAME ABI. No command/index buffers are read.
#define BSI_GL_DRAW_CALLS(X) \
    X(Arrays, (unsigned mode,int first,int count), (mode,first,count), "glDrawArrays") \
    X(Elements, (unsigned mode,int count,unsigned type,const void *indices), (mode,count,type,indices), "glDrawElements") \
    X(ArraysInstanced, (unsigned mode,int first,int count,int instances), (mode,first,count,instances), "glDrawArraysInstanced","glDrawArraysInstancedARB","glDrawArraysInstancedANGLE","glDrawArraysInstancedEXT","glDrawArraysInstancedNV") \
    X(ElementsInstanced, (unsigned mode,int count,unsigned type,const void *indices,int instances), (mode,count,type,indices,instances), "glDrawElementsInstanced","glDrawElementsInstancedARB","glDrawElementsInstancedANGLE","glDrawElementsInstancedEXT","glDrawElementsInstancedNV") \
    X(RangeElements, (unsigned mode,unsigned start,unsigned end,int count,unsigned type,const void *indices), (mode,start,end,count,type,indices), "glDrawRangeElements","glDrawRangeElementsEXT") \
    X(ElementsBaseVertex, (unsigned mode,int count,unsigned type,const void *indices,int baseVertex), (mode,count,type,indices,baseVertex), "glDrawElementsBaseVertex","glDrawElementsBaseVertexEXT","glDrawElementsBaseVertexOES") \
    X(RangeElementsBaseVertex, (unsigned mode,unsigned start,unsigned end,int count,unsigned type,const void *indices,int baseVertex), (mode,start,end,count,type,indices,baseVertex), "glDrawRangeElementsBaseVertex","glDrawRangeElementsBaseVertexEXT","glDrawRangeElementsBaseVertexOES") \
    X(ElementsInstancedBaseVertex, (unsigned mode,int count,unsigned type,const void *indices,int instances,int baseVertex), (mode,count,type,indices,instances,baseVertex), "glDrawElementsInstancedBaseVertex","glDrawElementsInstancedBaseVertexEXT","glDrawElementsInstancedBaseVertexOES") \
    X(ArraysBaseInstance, (unsigned mode,int first,int count,int instances,unsigned baseInstance), (mode,first,count,instances,baseInstance), "glDrawArraysInstancedBaseInstance","glDrawArraysInstancedBaseInstanceEXT") \
    X(ElementsBaseInstance, (unsigned mode,int count,unsigned type,const void *indices,int instances,unsigned baseInstance), (mode,count,type,indices,instances,baseInstance), "glDrawElementsInstancedBaseInstance","glDrawElementsInstancedBaseInstanceEXT") \
    X(ElementsBaseVertexBaseInstance, (unsigned mode,int count,unsigned type,const void *indices,int instances,int baseVertex,unsigned baseInstance), (mode,count,type,indices,instances,baseVertex,baseInstance), "glDrawElementsInstancedBaseVertexBaseInstance","glDrawElementsInstancedBaseVertexBaseInstanceEXT") \
    X(MultiArrays, (unsigned mode,const int *firsts,const int *counts,int drawCount), (mode,firsts,counts,drawCount), "glMultiDrawArrays","glMultiDrawArraysEXT") \
    X(MultiElements, (unsigned mode,const int *counts,unsigned type,const void *const *offsets,int drawCount), (mode,counts,type,offsets,drawCount), "glMultiDrawElements","glMultiDrawElementsEXT") \
    X(MultiElementsBaseVertex, (unsigned mode,const int *counts,unsigned type,const void *const *offsets,int drawCount,const int *baseVertices), (mode,counts,type,offsets,drawCount,baseVertices), "glMultiDrawElementsBaseVertex","glMultiDrawElementsBaseVertexEXT","glMultiDrawElementsBaseVertexOES") \
    X(ArraysIndirect, (unsigned mode,const void *indirect), (mode,indirect), "glDrawArraysIndirect") \
    X(ElementsIndirect, (unsigned mode,unsigned type,const void *indirect), (mode,type,indirect), "glDrawElementsIndirect") \
    X(MultiArraysIndirect, (unsigned mode,const void *indirect,int drawCount,int stride), (mode,indirect,drawCount,stride), "glMultiDrawArraysIndirect","glMultiDrawArraysIndirectEXT") \
    X(MultiElementsIndirect, (unsigned mode,unsigned type,const void *indirect,int drawCount,int stride), (mode,type,indirect,drawCount,stride), "glMultiDrawElementsIndirect","glMultiDrawElementsIndirectEXT") \
    X(MultiArraysIndirectCount, (unsigned mode,const void *indirect,std::intptr_t countOffset,int maxDrawCount,int stride), (mode,indirect,countOffset,maxDrawCount,stride), "glMultiDrawArraysIndirectCount","glMultiDrawArraysIndirectCountARB") \
    X(MultiElementsIndirectCount, (unsigned mode,unsigned type,const void *indirect,std::intptr_t countOffset,int maxDrawCount,int stride), (mode,type,indirect,countOffset,maxDrawCount,stride), "glMultiDrawElementsIndirectCount","glMultiDrawElementsIndirectCountARB")

namespace BsiShaders {
enum class GlDrawKind {
#define BSI_DRAW_KIND(id,params,args,...) id,
    BSI_GL_DRAW_CALLS(BSI_DRAW_KIND)
#undef BSI_DRAW_KIND
    Count
};
inline constexpr unsigned glDrawKinds=static_cast<unsigned>(GlDrawKind::Count);
inline constexpr unsigned glDrawAliases=5;
inline constexpr std::array<std::array<const char*,glDrawAliases>,glDrawKinds> glDrawNames{{
#define BSI_DRAW_NAMES(id,params,args,...) {{__VA_ARGS__}},
    BSI_GL_DRAW_CALLS(BSI_DRAW_NAMES)
#undef BSI_DRAW_NAMES
}};
} // namespace BsiShaders
