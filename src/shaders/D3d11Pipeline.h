#pragma once
#include "Parameters.h"
#include <memory>
#include <string>
#ifdef _WIN32
#include <d3d11_1.h>

namespace BsiShaders {
// Used by native D3D11 and by D3D11On12 wrapped game backbuffers. Resource and
// feature checks, not the GPU vendor ID, determine eligibility.
class D3d11Pipeline {
public:
    D3d11Pipeline();
    ~D3d11Pipeline();
    D3d11Pipeline(const D3d11Pipeline&)=delete;
    D3d11Pipeline &operator=(const D3d11Pipeline&)=delete;
    bool initialize(ID3D11Device *device,std::string *error);
    bool render(ID3D11Texture2D *gameFrame,const Parameters &settings,std::string *error);
    void release();
    static bool supports(DXGI_FORMAT format);
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace BsiShaders
#endif
