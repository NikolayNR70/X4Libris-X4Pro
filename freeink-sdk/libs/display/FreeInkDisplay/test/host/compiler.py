"""Resolve a host C++ compiler for the FreeInkDisplay test launchers."""
import os
import shutil
from pathlib import Path


def compiler_command():
    configured = os.environ.get("CXX")
    if configured:
        return configured
    for name in ("c++", "clang++", "g++"):
        found = shutil.which(name)
        if found:
            return found
    program_files = os.environ.get("ProgramFiles", r"C:\Program Files")
    for candidate in (
        Path(program_files) / "LLVM" / "bin" / "clang++.exe",
        Path(r"C:\msys64\ucrt64\bin\g++.exe"),
        Path(r"C:\msys64\ucrt64\bin\c++.exe"),
    ):
        if candidate.is_file():
            return str(candidate)
    raise RuntimeError("C++ compiler not found; install LLVM or MSYS2, or set CXX")