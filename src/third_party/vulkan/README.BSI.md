# Vulkan-Headers subset

Official Khronos Vulkan-Headers v1.3.290, commit
`b379292b2ab6df5771ba9870d53cf8b2c9295daf`.
Source: https://github.com/KhronosGroup/Vulkan-Headers/tree/b379292b2ab6df5771ba9870d53cf8b2c9295daf

Unmodified core/platform/Win32 headers and their required `vk_video` includes
only; no loader, driver, executable or proxy DLL is shipped here. These header
files are used to dynamically resolve the actual application's Vulkan dispatch.
See upstream `LICENSE.md`, `LICENSES` and each file's SPDX notice.

No GPU-vendor-specific code or device allowlist. Including these declarations
is not evidence of runtime support, hardware testing, or emulator compatibility.
