1. The best practice for DPDK is not use bazel but pre-installed as there are many reasons:
   * the vendor has its own DPDK, which is not public available to use.
   * the CPUs are different from your local bazel build.
   * the meson is not working, this is the worst, for Apple M4 CPUS, `-march=native` is not accpeted by GCC.
2. However, uses bazel has good reasons, even for personal use:
   * The blzmod is actually a C++ package center with boost, abseil, and other famouse C++ lib.
