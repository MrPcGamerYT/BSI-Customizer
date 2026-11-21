"""Real GL/GLES pixels through production private-sampler code on owned resources.

The Windows detour/HWND contracts are separate; this is not emulator evidence.
"""
import ctypes as C
import math
import re

I, U, F, P = C.c_int, C.c_uint, C.c_float, C.c_void_p


def exercise(gl, policy, desktop, size, resolver):
    version = gl("glGetString", C.c_char_p, U)(0x1F02).decode()
    match = re.search(r"(\d+)\.(\d+)", version)
    assert match, version
    actual = tuple(map(int, match.groups()))
    assert actual >= ((3, 3) if desktop else (3, 1)), "The production quality fixture requires GL 3.3/GLES 3.1 texture-level queries and private sampler objects; never skip-pass"
    policy.bsi_test_quality_minimum.argtypes = [I, I, I, U, F, F, F, I, C.POINTER(F)]
    policy.bsi_test_quality_minimum.restype = I

    def plan(mode, *, supported=1, mips=7, minimum=-1000, maximum=6, bias=0, reviewed=1):
        out = F(-99)
        changed = policy.bsi_test_quality_minimum(mode, not desktop, supported, mips, minimum, maximum, bias, reviewed, C.byref(out))
        return bool(changed), out.value

    assert plan(0) == (False, -1000)
    assert plan(1) == (True, 1.5) and plan(2) == (True, 3)
    assert plan(2, minimum=0) == (True, 3)
    assert plan(2, mips=2) == (True, 1)
    assert plan(2, mips=1) == (False, -1000)
    assert plan(2, supported=0) == (False, -1000), "GL/GLES without a private sampler path must bypass"
    assert plan(2, reviewed=0) == (False, -1000), "Unknown external profile is not qualified by GL queries"
    assert plan(2, minimum=.5) == (False, .5)
    assert plan(2, minimum=-2) == (False, -2)
    assert plan(2, bias=2) == (False, -1000), "Never stack a quality bias on an existing override"

    get_error = gl("glGetError", U)
    create_shader = gl("glCreateShader", U, U)
    shader_source = gl("glShaderSource", None, U, I, C.POINTER(C.c_char_p), C.POINTER(I))
    compile_shader = gl("glCompileShader", None, U)
    get_shader = gl("glGetShaderiv", None, U, U, C.POINTER(I))
    shader_log = gl("glGetShaderInfoLog", None, U, I, C.POINTER(I), P)
    create_program = gl("glCreateProgram", U)
    attach = gl("glAttachShader", None, U, U)
    link = gl("glLinkProgram", None, U)
    get_program = gl("glGetProgramiv", None, U, U, C.POINTER(I))
    delete_shader = gl("glDeleteShader", None, U)
    prefix = "#version 330 core\n" if desktop else "#version 300 es\nprecision highp float;\n"

    def shader(kind, body):
        obj = create_shader(kind)
        text = C.c_char_p((prefix + body).encode())
        shader_source(obj, 1, C.byref(text), None)
        compile_shader(obj)
        status = I()
        get_shader(obj, 0x8B81, C.byref(status))
        log = C.create_string_buffer(4096)
        shader_log(obj, len(log), None, log)
        assert status.value, log.value.decode()
        return obj

    vertex = shader(0x8B31, "out vec2 uv;uniform float sampleScale;void main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);uv=p*sampleScale;gl_Position=vec4(p*2.-1.,0.,1.);}")
    fragment = shader(0x8B30, "in vec2 uv;uniform sampler2D sourceTexture;out vec4 color;void main(){color=texture(sourceTexture,uv);}")
    program = create_program()
    attach(program, vertex)
    attach(program, fragment)
    link(program)
    status = I()
    get_program(program, 0x8B82, C.byref(status))
    assert status.value, "mip-selection test program did not link"
    delete_shader(vertex)
    delete_shader(fragment)
    gl("glUseProgram", None, U)(program)
    location = gl("glGetUniformLocation", I, U, C.c_char_p)(program, b"sourceTexture")
    assert location >= 0
    gl("glUniform1i", None, I, I)(location, 0)
    scale_location = gl("glGetUniformLocation", I, U, C.c_char_p)(program, b"sampleScale")
    assert scale_location >= 0
    sample_scale = gl("glUniform1f", None, I, F)
    sample_scale(scale_location, 1)
    maximum_bias = F(0)
    if desktop:
        gl("glGetFloatv", None, U, C.POINTER(F))(0x84FD, C.byref(maximum_bias))
        assert math.isfinite(maximum_bias.value) and maximum_bias.value > 0, "desktop bias limit must be real, not assumed"
    vao = U()
    gl("glGenVertexArrays", None, I, C.POINTER(U))(1, C.byref(vao))
    gl("glBindVertexArray", None, U)(vao)
    gl("glActiveTexture", None, U)(0x84C0)
    texture = U()
    gl("glGenTextures", None, I, C.POINTER(U))(1, C.byref(texture))
    gl("glBindTexture", None, U, U)(0x0DE1, texture)
    upload = gl("glTexImage2D", None, U, I, I, I, I, I, U, U, P)
    colors = [(255, 0, 0, 173), (0, 255, 0, 173), (0, 0, 255, 173), (255, 255, 0, 173),
              (255, 0, 255, 173), (0, 255, 255, 173), (255, 255, 255, 173)]
    for level, rgba in enumerate(colors):
        extent = 64 >> level
        data = (C.c_ubyte * (extent * extent * 4)).from_buffer_copy(bytes(rgba) * (extent * extent))
        upload(0x0DE1, level, 0x8058, extent, extent, 0, 0x1908, 0x1401, data)
    gen_sampler = gl("glGenSamplers", None, I, C.POINTER(U))
    sampler_i = gl("glSamplerParameteri", None, U, U, I)
    sampler_f = gl("glSamplerParameterf", None, U, U, F)
    get_sampler = gl("glGetSamplerParameterfv", None, U, U, C.POINTER(F))
    bind_sampler = gl("glBindSampler", None, U, U)
    samplers = (U * 3)()
    gen_sampler(3, samplers)
    default_min = F()
    get_sampler(samplers[0], 0x813A, C.byref(default_min))
    assert default_min.value == -1000, "exercise the actual GL/GLES sampler default"
    for mode, sampler in enumerate(samplers):
        sampler_i(sampler, 0x2801, 0x2703)  # trilinear, the complete seven-level view
        sampler_i(sampler, 0x2800, 0x2600)
        sampler_i(sampler, 0x2802, 0x812F)
        sampler_i(sampler, 0x2803, 0x812F)
        sampler_f(sampler, 0x813A, plan(mode, minimum=default_min.value)[1])
        sampler_f(sampler, 0x813B, 6)

    for cap in (0x0BE2, 0x0B71, 0x0B90, 0x0B44, 0x0C11, 0x0BD0, 0x8C89):
        gl("glDisable", None, U)(cap)
    if desktop:
        gl("glDisable", None, U)(0x8DB9)
    gl("glColorMask", None, C.c_ubyte, C.c_ubyte, C.c_ubyte, C.c_ubyte)(1, 1, 1, 1)
    gl("glViewport", None, I, I, I, I)(0, 0, size, size)
    draw = gl("glDrawArrays", None, U, I, I)
    read = gl("glReadPixels", None, I, I, I, I, U, U, P)

    def render(sampler):
        bind_sampler(0, sampler)
        draw(0x0004, 0, 3)
        pixel = (C.c_ubyte * 4)()
        read(size // 2, size // 2, 1, 1, 0x1908, 0x1401, pixel)
        assert get_error() == 0, "quality proof left a GL API error"
        return tuple(pixel)

    native = render(samplers[0])
    assert native == colors[0], ("native mip", native)
    light, strong = render(samplers[1]), render(samplers[2])
    assert all(abs(a - b) <= 2 for a, b in zip(light, (0, 128, 128, 173))), ("light mip interpolation", light)
    assert strong == colors[3], ("strong mip", strong)
    assert render(samplers[0]) == native, "rebinding the original sampler must restore exact original pixels"
    original_min, original_max = F(), F()
    get_sampler(samplers[0], 0x813A, C.byref(original_min))
    get_sampler(samplers[0], 0x813B, C.byref(original_max))
    assert (original_min.value, original_max.value) == (-1000, 6), "quality plan mutated the original sampler"
    # Exercise the ACTUAL production draw-scoped implementation, not just the
    # descriptor planner or a Python replacement. Its changed sampler must be
    # bound during the real draw, then restore the original binding and pixels.
    policy.bsi_test_quality_initialize.argtypes = [type(resolver), P, C.c_char_p, I]
    policy.bsi_test_quality_initialize.restype = I
    policy.bsi_test_quality_begin.argtypes = [I, I, U, C.c_char_p, I]
    policy.bsi_test_quality_begin.restype = U
    message = C.create_string_buffer(2048)
    assert policy.bsi_test_quality_initialize(resolver, None, message, len(message)), message.value.decode()
    bind_sampler(0, samplers[0])
    gl("glEnable", None, U)(0x0B71)
    gl("glDepthMask", None, C.c_ubyte)(1)
    gl("glDepthFunc", None, U)(0x0203)  # LEQUAL: repeated real depth-tested scene draws
    if desktop:
        gl("glClearDepth", None, C.c_double)(1)
    else:
        gl("glClearDepthf", None, F)(1)
    gl("glClear", None, U)(0x0100)  # clear the test-owned depth attachment
    active = gl("glActiveTexture", None, U)
    get_int = gl("glGetIntegerv", None, U, C.POINTER(I))
    def get_binding(value):
        # GLES exposes SAMPLER_BINDING through the active unit's GetIntegerv,
        # not the desktop-only indexed query for this enum.
        active(0x84C0)
        get_int(0x8919, C.byref(value))
        active(0x84C3)
    active(0x84C3)
    native_binding = samplers[0]

    def production(mode, reviewed=1, primitive=0x0004):
        before, after = I(), I()
        get_binding(before)
        count = policy.bsi_test_quality_begin(mode, reviewed, primitive, message, len(message))
        current = I()
        get_int(0x84E0, C.byref(current))
        assert current.value == 0x84C3, "inspection must restore ACTIVE_TEXTURE before the actual draw"
        get_binding(after)
        assert (after.value != before.value) == bool(count), message.value.decode()
        if count and desktop:
            actual_bias = F()
            get_sampler(after.value, 0x8501, C.byref(actual_bias))
            assert actual_bias.value == min(1.5 if mode == 1 else 3, maximum_bias.value), "production private bias is bounded by the queried device limit"
        draw(primitive, 0, 3)
        policy.bsi_test_quality_end()
        get_binding(after)
        assert before.value == after.value == native_binding, "draw-scoped binding did not restore exactly"
        pixel = (C.c_ubyte * 4)()
        read(size // 2, size // 2, 1, 1, 0x1908, 0x1401, pixel)
        return count, tuple(pixel)

    assert production(0) == (0, native)
    assert production(2) == (1, colors[3]), message.value.decode()
    assert production(2, reviewed=0) == (0, native), "no driver review must leave original sampler/pixels untouched"
    count, actual_light = production(1)
    assert count == 1 and all(abs(a - b) <= 2 for a, b in zip(actual_light, (0, 128, 128, 173)))
    assert production(0) == (0, native), "switch off restores exact native pixels without recreating texture resources"
    gl("glDisable", None, U)(0x0B71)
    assert production(2) == (0, native), "depth-disabled/UI draws must stay untouched"
    gl("glEnable", None, U)(0x0B71)
    gl("glDepthFunc", None, U)(0x0207)
    assert production(2) == (0, native), "always-pass UI-like depth state is not scene qualification"
    gl("glDepthFunc", None, U)(0x0203)
    gl("glDepthMask", None, C.c_ubyte)(0)
    assert production(2) == (0, native), "non-depth-writing passes must stay untouched"
    gl("glDepthMask", None, C.c_ubyte)(1)
    gl("glEnable", None, U)(0x0BE2)
    assert production(2) == (0, native), "blended/UI draws must stay untouched"
    gl("glDisable", None, U)(0x0BE2)
    sampler_f(samplers[0], 0x813A, .5)
    assert production(2)[0] == 0, "existing sampler minimum-LOD overrides must not be stacked/reset"
    sampler_f(samplers[0], 0x813A, -1000)
    active(0x84C0)
    gl("glTexParameteri", None, U, U, I)(0x0DE1, 0x813D, 0)
    active(0x84C3)
    assert production(2) == (0, native), "a one-level view is not a reducible mip chain"
    active(0x84C0)
    gl("glTexParameteri", None, U, U, I)(0x0DE1, 0x813D, 1000)
    active(0x84C3)
    assert get_error() == 0, message.value.decode()

    # Native minification distinguishes an additive bias from a floor. Exercise
    # the ACTUAL production begin/draw/end for BOTH source sampling modes, not
    # just the descriptor adapter or a manually authored replacement sampler.
    active(0x84C0)
    texture_i = gl("glTexParameteri", None, U, U, I)
    texture_f = gl("glTexParameterf", None, U, U, F)
    for key, value in ((0x2801, 0x2703), (0x2800, 0x2600), (0x2802, 0x812F), (0x2803, 0x812F)):
        texture_i(0x0DE1, key, value)
    texture_f(0x0DE1, 0x813A, -1000)
    texture_f(0x0DE1, 0x813B, 6)
    active(0x84C3)

    def source_lods():
        state = []
        active(0x84C0)
        keys = (0x813A, 0x813B, 0x8501) if desktop else (0x813A, 0x813B)
        for key in keys:
            value = F()
            gl("glGetTexParameterfv", None, U, U, C.POINTER(F))(0x0DE1, key, C.byref(value))
            state.append(value.value)
            get_sampler(samplers[0], key, C.byref(value))
            state.append(value.value)
        active(0x84C3)
        return tuple(state)

    def interpolated(lod):
        lod = min(6, lod)
        lower, upper = math.floor(lod), min(6, math.ceil(lod))
        amount = lod - lower
        return tuple(round(a * (1 - amount) + b * amount) for a, b in zip(colors[lower], colors[upper]))

    saved_source_lods = source_lods()
    cycles = 0
    for native_binding in (samplers[0], 0):
        bind_sampler(0, native_binding)
        for scale, native_lod in ((1, 0), (16, 4)):
            sample_scale(scale_location, scale)
            assert production(0) == (0, colors[native_lod]), "test gradient must really select native mip 0/4"
            for mode, floor in ((1, 1.5), (2, 3)):
                bias = min(floor, maximum_bias.value) if desktop else 0
                expected = interpolated(max(floor, native_lod + bias))
                for _ in range(8):
                    count, observed = production(mode)
                    assert count == 1 and all(abs(a - b) <= 2 for a, b in zip(observed, expected)), ("actual production minification", desktop, native_binding, scale, mode, observed, expected)
                    assert production(0) == (0, colors[native_lod]), "Off must restore exact pixels at BOTH gradients/source modes"
                    assert source_lods() == saved_source_lods, "source texture/sampler LOD state must remain unchanged on every cycle"
                    cycles += 1
    assert cycles == 64
    sample_scale(scale_location, 1)
    native_binding = samplers[0]
    bind_sampler(0, native_binding)
    assert get_error() == 0, message.value.decode()
    gl("glEnable", None, U)(0xDEADBEEF)
    assert production(2) == (1, colors[3])
    assert get_error() == 0x0500 and get_error() == 0, "sampler inspection must not consume the application's pending API error"
    policy.bsi_test_quality_release()
    active(0x84C0)
    bind_sampler(0, 0)
    gl("glDeleteSamplers", None, I, C.POINTER(U))(3, samplers)
    gl("glBindTexture", None, U, U)(0x0DE1, 0)
    gl("glDeleteTextures", None, I, C.POINTER(U))(1, C.byref(texture))
    gl("glBindVertexArray", None, U)(0)
    gl("glDeleteVertexArrays", None, I, C.POINTER(U))(1, C.byref(vao))
    gl("glUseProgram", None, U)(0)
    gl("glDeleteProgram", None, U)(program)
    assert get_error() == 0
    print("PASS: production draw-scoped " + ("desktop OpenGL queried bias+floor" if desktop else "GLES 3 floor-only")
          + " selects real test-owned mip colors at native lambda 0 AND 4 in both texture/sampler source modes; 64 exact Off/source-LOD restoration cycles; active-unit/depth/UI/no-mip/override/review bypass and pending errors preserved")
    print("LIMIT: owned software-context sampler proof, not game interception, physical GPU, profile compatibility or FPS evidence")
