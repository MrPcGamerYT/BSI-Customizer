#pragma once
#include "Parameters.h"
#include <cstdint>
#include <memory>
#include <string>

namespace BsiShaders {
// No windowing, Qt, DLL loading, FPS control or system changes. The caller must
// own the current GL context and identify an eligible native game surface.
using ResolveGl = void *(*)(const char *name, void *user);
class GlPipeline {
public:
    GlPipeline();
    ~GlPipeline();
    GlPipeline(const GlPipeline &) = delete;
    GlPipeline &operator=(const GlPipeline &) = delete;
    bool initialize(ResolveGl resolve, void *user, std::string *error);
    bool render(int width, int height, const Parameters &settings, std::string *error);
    // Call only with this pipeline's context current; never from DllMain.
    void release();
    bool initialized() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace BsiShaders
