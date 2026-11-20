"""Locate only the independent Python Qt host's Windows DLL directory.

This is fixture setup, not an emulator deployment or an arbitrary-DLL loader.
PySide6's Windows wheels put Qt DLLs beside QtCore.pyd even when QLibraryInfo
reports a nonexistent bin subdirectory. PyQt5 normally reports Qt5/bin.
"""
from pathlib import Path


def windows_host_binaries(binding_root, reported_binaries, major):
    root = Path(binding_root).resolve()
    if major not in (5, 6):
        raise ValueError(f"Unsupported Qt fixture major: {major}")
    for directory in (Path(reported_binaries).resolve(), root):
        # Never fall back to an SDK merely because it is on PATH or reported
        # by an already mixed host. The Python binding must own this runtime.
        if directory.is_relative_to(root) and (directory / f"Qt{major}Core.dll").is_file():
            return directory
    raise RuntimeError(f"No Qt {major} host Core DLL inside Python binding {root}; reported binaries: {reported_binaries}")
