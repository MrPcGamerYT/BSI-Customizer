"""Test-only native Windows fault addresses; no recovery or production hooks.

Python's faulthandler identifies the QML call, but not a C++ plugin fault's
module. Keep the exception fatal, and print the native address/stack modules so
CI annotations expose useful diagnostics without access to external log hosts.
"""
import ctypes as C
from ctypes import wintypes as W
import sys


def install():
    kernel = C.WinDLL("kernel32", use_last_error=True)
    # An unattended test must fail, not wait forever behind a Windows error
    # dialog. This is process-local test setup, never a player/runtime change.
    get_mode = kernel.GetErrorMode
    get_mode.argtypes = []
    get_mode.restype = W.DWORD
    set_mode = kernel.SetErrorMode
    set_mode.argtypes = [W.DWORD]
    set_mode.restype = W.DWORD
    set_mode(get_mode() | 0x1 | 0x2 | 0x8000)
    get_module = kernel.GetModuleHandleExW
    get_module.argtypes = [W.DWORD, C.c_void_p, C.POINTER(C.c_void_p)]
    get_module.restype = W.BOOL
    get_name = kernel.GetModuleFileNameW
    get_name.argtypes = [C.c_void_p, W.LPWSTR, W.DWORD]
    get_name.restype = W.DWORD
    capture = kernel.RtlCaptureStackBackTrace
    capture.argtypes = [W.DWORD, W.DWORD, C.POINTER(C.c_void_p), C.c_void_p]
    capture.restype = W.WORD

    def describe(address):
        module = C.c_void_p()
        if get_module(0x4 | 0x2, address, C.byref(module)):
            name = C.create_unicode_buffer(2048)
            get_name(module, name, len(name))
            return f"{name.value}+0x{address - module.value:x}"
        return f"unmapped/non-module 0x{address:x}"

    class Record(C.Structure):
        _fields_ = [("code", W.DWORD), ("flags", W.DWORD), ("next", C.c_void_p), ("address", C.c_void_p)]

    class Pointers(C.Structure):
        _fields_ = [("record", C.POINTER(Record)), ("context", C.c_void_p)]

    callback_type = C.WINFUNCTYPE(W.LONG, C.POINTER(Pointers))

    @callback_type
    def exception(pointers):
        record = pointers.contents.record.contents
        if record.code in (0xC0000005, 0xC000001D, 0xC0000409):
            try:
                print(f"NATIVE FATAL 0x{record.code:08x}: {describe(record.address)}", file=sys.stderr, flush=True)
                frames = (C.c_void_p * 48)()
                count = capture(0, len(frames), frames, None)
                for index in range(count):
                    if frames[index]:
                        print(f"  native frame {index}: {describe(frames[index])}", file=sys.stderr, flush=True)
            except Exception as error:
                print("native diagnostic failed:", error, file=sys.stderr, flush=True)
        return 0  # EXCEPTION_CONTINUE_SEARCH: never suppress a failed test

    add = kernel.AddVectoredExceptionHandler
    add.argtypes = [W.ULONG, callback_type]
    add.restype = C.c_void_p
    handle = add(1, exception)
    assert handle, "native fault diagnostics could not be installed"
    return handle, exception  # retain callback until the test process exits
