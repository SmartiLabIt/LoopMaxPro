# LoopMaxPro - C++ only flags.
# Kept out of build_flags because they would also reach the C compiler and produce warnings.
Import("env")  # noqa: F821

env.Append(CXXFLAGS=["-fno-exceptions", "-fno-rtti"])  # noqa: F821
