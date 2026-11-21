#pragma once
#include "GlPipeline.h"
#include "TextureQuality.h"
#include <memory>

namespace BsiShaders {
// Draw-scoped PRIVATE sampler substitution, not a final-image blur. The caller
// must first prove the CURRENT drawable belongs to the selected owned game
// presentation. A separate scene context on that EXACT drawable can qualify;
// shared/offscreen/pbuffer/foreign contexts are never inferred from a loaded
// provider or a share group. No original sampler/texture/profile is edited.
// Conservative scope: GL 3.3 / GLES 3.1, depth-tested/depth-writing triangle
// draws, ordinary sampler2D uniforms and complete, normalized-color mip chains.
class GlTextureOverride {
public:
    GlTextureOverride();
    ~GlTextureOverride();
    GlTextureOverride(const GlTextureOverride &) = delete;
    GlTextureOverride &operator=(const GlTextureOverride &) = delete;
    bool initialize(ResolveGl resolve,void *user,std::string *error,bool externalProtectionPossible=false);
    // Returns the number of bindings temporarily replaced for this draw.
    // Call end() AFTER the original draw; begin() already restores ACTIVE_TEXTURE
    // and rolls back every binding before propagating an inspection exception.
    unsigned begin(TextureQuality mode,bool externalOverridesReviewed,unsigned primitive,std::string *detail,
                   TextureDrawReason *reason=nullptr,TextureDepthFailure *depthFailure=nullptr);
    void end()noexcept;
    // Only with the owning context CURRENT. Destruction otherwise abandons
    // names to that context's driver teardown, never deletes in another context.
    void release();
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace BsiShaders
