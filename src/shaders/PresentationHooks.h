#pragma once
#ifdef _WIN32
namespace BsiShaders {
using InstallPresentationHook=bool(*)(void *target,void *replacement,void **original);
// Thread-safe registry; vtable detours can have several runtime implementations.
// Callbacks recover the trampoline for the actual object's method address,
// rather than assuming every DXGI implementation uses one global function.
void *originalPresentationFunction(void *target);
bool scanDxgiHooks(InstallPresentationHook install);
bool scanVulkanHooks(InstallPresentationHook install);
void stopDxgiHooks();
void stopVulkanHooks();
}
#endif
