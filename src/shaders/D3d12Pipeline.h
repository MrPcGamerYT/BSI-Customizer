#pragma once
#include "Parameters.h"
#ifdef _WIN32
#include <d3d12.h>
#include <memory>
#include <string>
namespace BsiShaders {
// Microsoft's D3D11On12 device translates our shared original D3D11 shaders
// onto the game's ACTUAL captured D3D12 direct command queue. This is not a
// fallback to a different game renderer and is not tied to any GPU vendor.
class D3d12Pipeline {
public:
    D3d12Pipeline();
    ~D3d12Pipeline();
    D3d12Pipeline(const D3d12Pipeline&)=delete;
    D3d12Pipeline &operator=(const D3d12Pipeline&)=delete;
    bool initialize(ID3D12Device *device,ID3D12CommandQueue *observedQueue,std::string *error);
    bool render(ID3D12Resource *gameFrame,const Parameters &settings,std::string *error);
    void release();
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace BsiShaders
#endif
