"""Real GL/GLES mip-selection proof on TEST-OWNED resources, not game hooks."""
import ctypes as C
import re

I, U, F, P = C.c_int, C.c_uint, C.c_float, C.c_void_p


def exercise(gl, policy, desktop, size):
    version = gl("glGetString", C.c_char_p, U)(0x1F02).decode()
    match = re.search(r"(\d+)\.(\d+)", version)
    assert match, version
    actual = tuple(map(int, match.groups()))
    assert actual >= ((3, 3) if desktop else (3, 0)), "The mandatory quality pixel fixture requires GL 3.3/GLES 3 sampler objects; never skip-pass"
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

    vertex = shader(0x8B31, "out vec2 uv;void main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);uv=p;gl_Position=vec4(p*2.-1.,0.,1.);}")
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
    bind_sampler(0, 0)
    gl("glDeleteSamplers", None, I, C.POINTER(U))(3, samplers)
    gl("glBindTexture", None, U, U)(0x0DE1, 0)
    gl("glDeleteTextures", None, I, C.POINTER(U))(1, C.byref(texture))
    gl("glBindVertexArray", None, U)(0)
    gl("glDeleteVertexArrays", None, I, C.POINTER(U))(1, C.byref(vao))
    gl("glUseProgram", None, U)(0)
    gl("glDeleteProgram", None, U)(program)
    assert get_error() == 0
    print("PASS: experimental C++ minimum-LOD policy selects real test-owned " + ("OpenGL" if desktop else "GLES 3")
          + " mip colors at 1.5/3.0; original sampler and exact pixels restore; no-mip/override/unqualified bypass")
    print("LIMIT: owned software-context sampler proof, not game interception, physical GPU, profile compatibility or FPS evidence")
