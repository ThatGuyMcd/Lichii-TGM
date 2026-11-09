# Ocarina of Time experimental Windows build

Source: [ThatGuyMcd/Lichii-TGM, oot-experimental](https://github.com/ThatGuyMcd/Lichii-TGM/tree/oot-experimental).
The [PR audit](oot-pr-audit/README.md) records all 305 matching PRs and their dispositions.
The [integration manifest](OOT_EXPERIMENTAL_INTEGRATION.md) explains competing GPU and shader implementations.

## Run the package

Extract the entire ZIP to a writable folder and open `lichii.exe`. Keep the DLLs, `plugins`,
`notes`, and other supplied files together. `eden-cli.exe` is the command-line frontend.
Game files, keys, firmware and user data are not included. The normal Lichii user-data location
is used; the package does not create a portable `user` folder automatically.

The integrated features include asynchronous GPU readback, CPU/JIT and storage optimizations,
shader preparation and pipeline retention, graphics pipeline libraries, task/mesh prediction,
DLSS/DLAA and FSR paths, frame generation, HDR, ultrawide camera/HUD hooks, VRR pacing,
immediate input, the performance overlay, native GPU address tables, descriptor heaps, shader
objects and driver pipeline binaries. Hardware-dependent paths retain capability checks.
Mutually exclusive alternatives use their settings or experiment switches.

Graphics settings expose **Ocarina of Time anti-aliasing**, **HDR**, aspect ratio, and frame
generation. Follow the settings' tooltips for the required resolution and runtime combinations.
For NVIDIA DLSS/DLAA, enable **Use NVIDIA DLSS** as the tooltip describes. AMD FSR 4 and AMD
frame generation use the **AMD FSR runtime folder** setting. Optional NVIDIA, AMD and Lossless
Scaling runtimes are not supplied by this package. Their availability and the selected GPU
determine which paths can run.

Graphics pipeline libraries use `EDEN_NX2_GPL=1`; optional optimized links additionally use
`EDEN_NX2_GPL_OPTIMIZE=1`. Fast-first compilation uses `EDEN_NX2_FAST_FIRST=1`. Predicted
task/mesh prewarm has the per-game `use_mesh_shader_warmup` setting. Diagnostics remain opt-in;
see the integration manifest and `nx2_switches.txt` for additional controls. Environment
variables must be set before starting Lichii.

Native descriptor heaps use `EDEN_NX2_DESCRIPTOR_HEAP=1`, and shader objects use
`EDEN_NX2_SHADER_OBJECT=1`. Both require driver support and retain fallback paths for
incompatible shaders. Native storage and uniform address paths can be disabled separately
with `EDEN_NX2_NATIVE_STORAGE=0` and `EDEN_NX2_NATIVE_UNIFORMS=0`. The integration manifest
records the remaining comparison controls and architectural exclusions.

This is an experimental integration. Build and unit-test results do not establish gameplay
correctness, image quality or performance. No local OoT gameplay session, guard-room test,
HDR display test or vendor-specific frame-generation validation was performed.

## Build from source

Use Windows x64 with Git, CMake, Ninja, LLVM 22 (`clang-cl` and `lld-link`), the installed
Visual Studio C++ tools/Windows SDK, Qt 6.8.3 for MSVC x64 including Qt Charts and Qt Test,
and glslang. From a checkout of this branch:

```powershell
./tools/windows/build-oot-experimental.ps1 `
    -QtRoot 'D:/Qt/6.8.3/msvc2022_64' `
    -GlslangValidator 'D:/tools/glslang/bin/glslang.exe' `
    -BuildDirectory 'D:/build/lichii-oot' -Jobs 4
```

The script configures a Release build, builds both frontends and runs CTest. The build downloads
the project's pinned dependencies. Allow several gigabytes of disk space and sufficient memory;
lower `-Jobs` if compilation exhausts memory. Keep temporary files and the build directory on a
drive with adequate free space. `-SkipTests` omits the test targets and test execution.

The package's `build-provenance.json` identifies its exact source commit and toolchain.
`SHA256SUMS.txt` verifies the supplied executables and DLLs. The adjacent source ZIP contains
the project sources at the same commit; dependency source URLs and versions are recorded in
the project's CMake files and the dependency inventory.
