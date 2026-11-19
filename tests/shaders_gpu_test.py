"""Exercise the production C++ pipeline on real offscreen EGL pixels.

This is a software/API conformance test, NOT certification of Intel/AMD/NVIDIA
hardware, Windows presentation hooks, the emulator, DirectX or Vulkan adapters.
A missing graphics context fails the test rather than reporting a skipped pass.
Use Mesa surfaceless EGL by default, or set BSI_SHADERS_EGL_LIBRARY to an ANGLE
libEGL and BSI_SHADERS_EGL_PLATFORM=angle-swiftshader for local software testing.
"""
import ctypes as C
import ctypes.util
import math
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
I, U, P, F = C.c_int, C.c_uint, C.c_void_p, C.c_float
EGL_NONE = 0x3038
SIZE = 64
RESOLVE = C.CFUNCTYPE(P, C.c_char_p, P)


def main():
    name = os.environ.get("BSI_SHADERS_EGL_LIBRARY") or ctypes.util.find_library("EGL")
    assert name, "EGL is required: install a software driver or provide an explicit ANGLE runtime"
    egl = C.CDLL(name)

    def native(name, result, *args):
        fn = getattr(egl, name)
        fn.restype, fn.argtypes = result, list(args)
        return fn

    get_proc = native("eglGetProcAddress", P, C.c_char_p)
    platform = C.CFUNCTYPE(P, U, P, C.POINTER(I))(get_proc(b"eglGetPlatformDisplayEXT"))
    if os.environ.get("BSI_SHADERS_EGL_PLATFORM") == "angle-swiftshader":
        attrs = (I * 5)(0x3203, 0x3450, 0x3209, 0x3487, EGL_NONE)
        display = platform(0x3202, None, attrs)
    else:
        display = platform(0x31DD, None, (I * 1)(EGL_NONE))
    major, minor = I(), I()
    assert native("eglInitialize", U, P, C.POINTER(I), C.POINTER(I))(display, C.byref(major), C.byref(minor)), "Cannot initialize a real EGL display"
    assert native("eglBindAPI", U, U)(0x30A0)
    attrs = (I * 17)(0x3033, 1, 0x3040, 4, 0x3024, 8, 0x3023, 8, 0x3022, 8, 0x3021, 8, 0x3031, 0, 0x3032, 0, EGL_NONE)
    config, count = P(), I()
    assert native("eglChooseConfig", U, P, C.POINTER(I), C.POINTER(P), I, C.POINTER(I))(display, attrs, C.byref(config), 1, C.byref(count)) and count.value
    context = native("eglCreateContext", P, P, P, P, C.POINTER(I))(display, config, None, (I * 3)(0x3098, 2, EGL_NONE))
    surface = native("eglCreatePbufferSurface", P, P, P, C.POINTER(I))(display, config, (I * 5)(0x3057, SIZE, 0x3056, SIZE, EGL_NONE))
    assert context and surface
    make_current = native("eglMakeCurrent", U, P, P, P, P)
    assert make_current(display, surface, surface, context)

    def gl(name, result, *args):
        address = get_proc(name.encode())
        assert address, name
        return C.CFUNCTYPE(result, *args)(address)

    get_error = gl("glGetError", U)
    get_string = gl("glGetString", C.c_char_p, U)
    print("Real EGL test renderer:", get_string(0x1F00).decode(), get_string(0x1F01).decode(), get_string(0x1F02).decode())
    clear_color = gl("glClearColor", None, F, F, F, F)
    clear = gl("glClear", None, U)
    enable, disable = gl("glEnable", None, U), gl("glDisable", None, U)
    scissor = gl("glScissor", None, I, I, I, I)
    viewport = gl("glViewport", None, I, I, I, I)
    color_mask = gl("glColorMask", None, C.c_ubyte, C.c_ubyte, C.c_ubyte, C.c_ubyte)
    get_int = gl("glGetIntegerv", None, U, C.POINTER(I))
    get_bool = gl("glGetBooleanv", None, U, C.POINTER(C.c_ubyte))
    is_enabled = gl("glIsEnabled", C.c_ubyte, U)
    read = gl("glReadPixels", None, I, I, I, I, U, U, P)
    active_texture = gl("glActiveTexture", None, U)

    def pixels():
        result = (C.c_ubyte * (SIZE * SIZE * 4))()
        read(0, 0, SIZE, SIZE, 0x1908, 0x1401, result)
        assert get_error() == 0, "production rendering left a GL error"
        return bytes(result)

    def flat(rgb=(.8, .4, .2)):
        disable(0x0C11)
        color_mask(1, 1, 1, 1)
        clear_color(*rgb, 1)
        clear(0x4000)

    def pattern():
        flat((.08, .08, .08))
        enable(0x0C11)
        for y in range(SIZE):
            # A soft diagonal plus a small bright region exercises all spatial passes.
            for x in range(SIZE):
                value = .15 + .65 / (1 + math.exp(max(-50, min(50, (y - x) * .8))))
                if 27 < x < 35 and 27 < y < 35:
                    value = 1
                scissor(x, y, 1, 1)
                clear_color(value, value * .8, value * .6, 1)
                clear(0x4000)
        disable(0x0C11)

    def snapshot():
        result = []
        for enum, length in [(0x8B8D, 1), (0x84E0, 1), (0x8894, 1), (0x0BA2, 4), (0x0C10, 4)]:
            value = (I * length)()
            get_int(enum, value)
            result.append(tuple(value))
        masks = (C.c_ubyte * 4)()
        get_bool(0x0C23, masks)
        result.append(tuple(masks))
        result.append(tuple(is_enabled(e) for e in [0x0BE2, 0x0B71, 0x0B90, 0x0B44, 0x0C11, 0x0BD0]))
        return result

    with tempfile.TemporaryDirectory(prefix="bsi-shader-gpu-") as temp:
        library = Path(temp) / "pipeline.so"
        subprocess.run([os.environ.get("CXX", "g++"), "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror", "-shared", "-fPIC",
                        "-Isrc/shaders", "src/shaders/GlPipeline.cpp", "tests/shaders/gpu_adapter.cpp", "-o", str(library)], cwd=ROOT, check=True)
        core = C.CDLL(str(library))
        core.bsi_test_initialize.argtypes = [RESOLVE, P, C.c_char_p, I]
        core.bsi_test_render.argtypes = [I, I, I, C.POINTER(I), C.c_char_p, I]
        core.bsi_test_reference.argtypes = [C.POINTER(F), C.POINTER(I), C.POINTER(F)]
        resolver = RESOLVE(lambda name, _user: get_proc(name))
        message = C.create_string_buffer(2048)
        assert core.bsi_test_initialize(resolver, None, message, len(message)), message.value.decode()
        assert get_error() == 0

        def render(values=None, enabled=True, width=SIZE, height=SIZE):
            values = values or [0] * 10
            assert core.bsi_test_render(width, height, enabled, (I * 10)(*values), message, len(message)), message.value.decode()

        flat()
        original = pixels()
        render()
        assert pixels() == original, "enabled + all-zero settings must be byte-exact neutral bypass"
        render([40] * 10, enabled=False)
        assert pixels() == original, "disabled effects must not touch a frame"
        for index, value in [(0, 60), (0, -80), (1, 30), (1, -45), (2, 50), (2, -70), (3, 75), (4, 60), (5, 80)]:
            flat()
            values = [0] * 10
            values[index] = value
            expected = (F * 3)()
            # Compare against the quantized input actually captured from the surface.
            core.bsi_test_reference((F * 3)(*(c / 255 for c in original[:3])), (I * 10)(*values), expected)
            render(values)
            observed = pixels()[:3]
            assert all(abs(observed[i] - round(expected[i] * 255)) <= 1 for i in range(3)), (index, observed, list(expected))
        for index in (6, 7, 8, 9):
            pattern()
            before = pixels()
            values = [0] * 10
            values[index] = 85
            render(values)
            after = pixels()
            assert after != before, f"spatial shader {index} did not change real framebuffer pixels"
            assert all(after[i] == before[i] for i in range(3, len(after), 4)), "shader must preserve source alpha"
        flat()
        viewport(2, 3, 47, 49)
        scissor(4, 5, 11, 13)
        for enum in [0x0BE2, 0x0B71, 0x0B90, 0x0B44, 0x0C11]:
            enable(enum)
        color_mask(0, 1, 0, 1)
        active_texture(0x84C3)
        before = snapshot()
        render([20, 10, 5, 0, 0, 0, 0, 0, 0, 0])
        assert snapshot() == before, "production pipeline failed to restore native host state"
        assert get_error() == 0
        enable(0xDEADBEEF)
        render([20] + [0] * 9)
        assert get_error() == 0x0500, "pipeline must not consume the host's pending GL error"
        assert get_error() == 0
        invalid = (I * 10)(101, *([0] * 9))
        assert not core.bsi_test_render(SIZE, SIZE, True, invalid, message, len(message))
        assert not core.bsi_test_render(0, SIZE, True, (I * 10)(20, *([0] * 9)), message, len(message))
        core.bsi_test_release()
        assert get_error() == 0
    make_current(display, None, None, None)
    native("eglDestroySurface", U, P, P)(display, surface)
    native("eglDestroyContext", U, P, P)(display, context)
    native("eglTerminate", U, P)(display)
    print("PASS: production C++ GLES pipeline changes real pixels for all 10 effects; neutral byte identity, reference color math, source alpha, host-state restoration, pending error preservation and invalid input guards")
    print("LIMIT: software/API test only; Windows hooks, native Settings integration, DirectX/Vulkan and physical GPU-vendor coverage remain unverified")


if __name__ == "__main__":
    main()
