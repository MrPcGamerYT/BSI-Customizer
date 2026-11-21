#pragma once
#include <algorithm>
#include <cstdint>

namespace BsiShaders {
// A native framebuffer-size profile, NOT a postprocess or game-texture sampler
// override. These are the public Display page's known custom-resolution bounds.
// No FPS, driver, root, DPI, graphics-API or guest/game-file setting is changed.
// Keep the historical internal name for v2 preference/API compatibility. The
// visible option is Render resolution, not the requested texture-LOD Potato.
enum class PotatoMode { Native=0, Light=1, Strong=2 };
struct DisplayResolution {
    int width=0,height=0;
    bool custom=false;
    bool valid() const {
        // Existing predefined resolutions can exceed the custom editor's 3840
        // limit (e.g. 5120x2160); keep an exact, bounded restore snapshot.
        return width>=540&&height>=540&&width<=8192&&height<=8192
            &&std::int64_t(width)*height<=33554432
            &&(!custom||(width<=3840&&height<=3840));
    }
    bool operator==(const DisplayResolution &other)const{return width==other.width&&height==other.height&&custom==other.custom;}
    bool operator!=(const DisplayResolution &other)const{return !(*this==other);}
};
struct PotatoResolutionPlan {
    DisplayResolution target;
    bool supported=false,reduced=false;
    const char *diagnostic="Native display resolution retained.";
};
inline PotatoResolutionPlan planPotatoResolution(PotatoMode mode,const DisplayResolution &original){
    PotatoResolutionPlan plan;plan.target=original;
    if(!original.valid()){plan.diagnostic="The native framebuffer size is outside the supported capture bounds; no display change is allowed.";return plan;}
    if(mode==PotatoMode::Native){plan.supported=true;return plan;}
    if(mode!=PotatoMode::Light&&mode!=PotatoMode::Strong){plan.diagnostic="Unknown resolution mode; native resolution retained.";return plan;}
    const int cap=mode==PotatoMode::Light?720:540;
    const int shorter=(std::min)(original.width,original.height);
    // An exact-original profile is supported, not a performance claim. Preserve
    // both original dimensions/flag; the caller compares CURRENT to detect a no-op.
    if(shorter==cap){plan.supported=true;plan.diagnostic="This profile matches the captured original size without reducing it. A different currently configured size still needs Save and restart.";return plan;}
    // These remain optional REDUCTION profiles. Never upscale a smaller native
    // framebuffer merely to make a previously rejected selection appear to work.
    if(shorter<cap){plan.diagnostic="This profile would increase the original Display size. Choose a smaller available profile or keep the current size; no upscaling is performed.";return plan;}
    // Preserve orientation/aspect to the nearest TWO pixels. Both dimensions
    // must be legal in the native custom-resolution editor; never force an
    // unsupported 360p/270p framebuffer or silently change DPI/guest density.
    const auto scaled=[&](int value){return int(2*((std::int64_t(value)*cap+shorter)/(2*shorter)));};
    const DisplayResolution target{scaled(original.width),scaled(original.height),true};
    if(target.width<540||target.height<540||target.width>3840||target.height>3840){
        plan.diagnostic="The aspect-preserving target does not fit the native 540-3840 pixel custom-resolution limits; original resolution retained.";return plan;
    }
    plan.target=target;plan.supported=true;
    plan.reduced=std::int64_t(target.width)*target.height<std::int64_t(original.width)*original.height;
    plan.diagnostic="Native Display size selected; save and restart the player to apply it. This is not texture LOD or a measured game-resolution/FPS change.";
    return plan;
}
} // namespace BsiShaders
