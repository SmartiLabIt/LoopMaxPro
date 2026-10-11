# LoopMaxPro - finds a host C++ compiler for the native environment on Windows.
# If g++ is not in PATH, looks for a WinLibs/MinGW installation made with winget.
import glob
import os
import shutil
import sys

Import("env")  # noqa: F821

if sys.platform.startswith("win"):
    # Static runtime: test programs run without the MinGW DLLs in PATH.
    env.Append(LINKFLAGS=["-static"])  # noqa: F821
    # Sockets of the HTTP server of the simulator.
    env.Append(LIBS=["ws2_32"])  # noqa: F821

    if shutil.which("g++") is None:
        local = os.environ.get("LOCALAPPDATA", "")
        candidates = glob.glob(os.path.join(
            local, "Microsoft", "WinGet", "Packages", "BrechtSanders.WinLibs*", "mingw64", "bin"))
        candidates += [r"C:\msys64\ucrt64\bin", r"C:\msys64\mingw64\bin", r"C:\mingw64\bin"]
        for path in candidates:
            if os.path.exists(os.path.join(path, "g++.exe")):
                env.PrependENVPath("PATH", path)  # noqa: F821
                os.environ["PATH"] = path + os.pathsep + os.environ.get("PATH", "")
                print("LoopMaxPro native: using compiler in " + path)
                break
        else:
            print("LoopMaxPro native: g++ not found. Install it with:\n"
                  "  winget install BrechtSanders.WinLibs.POSIX.UCRT")
