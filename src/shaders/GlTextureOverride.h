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
// GL 3.3 / GLES 3.1, opaque triangle draws, ordinary sampler2D uniforms and
// complete normalized-color mip chains. The legacy policy additionally needs
// qualified depth; explicit owned-mip compatibility does NOT require depth.
// Compatibility can affect opaque mipmapped in-game HUD; it is not semantic
// scene classification. Blended draws, separate Qt surfaces, protected/foreign
// routes and unsupported depth/integer/HDR formats stay native. Normalized RGB
// data maps cannot be distinguished from albedo by format alone.
// Desktop GL uses a queried, bounded private LOD bias plus floor; GLES retains
// its floor-only policy and never queries/writes desktop bias.
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
                   TextureDrawReason *reason=nullptr,TextureDepthFailure *depthFailure=nullptr,
                   TextureDrawPolicy policy=TextureDrawPolicy::DepthQualifiedScene);
    void end()noexcept;
    // Only with the owning context CURRENT. Destruction otherwise abandons
    // names to that context's driver teardown, never deletes in another context.
    void release();
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace BsiShaders
