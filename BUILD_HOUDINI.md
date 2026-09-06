# Building the Houdini port

This branch adds capability-based compatibility with recent Houdini/USD SDKs
and an optional, private HIP 7 loader. Houdini 22 and ROCm 7 are supported
choices, not mandatory dependencies. Rebuild the plugin for each HDK/USD ABI.

The tested workstation uses Houdini 22.0.429 on Linux, GCC 13.3, C++20,
libstdc++ ABI 1, Python 3.13, USD 26.05, MaterialX 1.39.5, OpenVDB 13,
Radeon ProRender SDK v3.1.6.patch1, and ROCm 7.2.4 on an AMD Radeon AI PRO
R9700. NVIDIA drives the desktop. This is local validation, not official
certification of that GPU or a claim that all older configurations were tested.

| Configuration | Status |
|---|---|
| H22.0.429 / supplied USD / SDK 3.1.6.patch1 / optional HIP 7 loader | Compiles, loads, discovers the R9700 and renders in the GUI |
| Same H22 SDK with `RPR_ROCM7_ROOT` empty | Configure and API probes pass; default runtime not tested on this workstation |
| Older Houdini/USD/Python/OpenVDB and default SDK runtime | Legacy branches retained; not runtime tested |
| Other operating systems and standalone USD | Existing paths retained; not tested by this port |

The build probes the selected headers for constructor macros, renderer creation,
Sdr discovery, implicit-surface scene indices, and typed HDK port indices. It
uses the C++ standard and Linux ABI exported by the chosen HDK; HDKs without
an ABI definition retain the older ABI-0 fallback. Python bindings and optional
USD libraries are discovered from that same installation.

Use the pinned submodules, or explicitly provide an SDK checkout with its LFS
kernel files materialized. Houdini supplies MaterialX and OpenVDB for this build.
For example, from an environment configured for the selected Houdini install:

```sh
cmake -S . -B build-houdini \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/absolute/path/to/rpr-install \
  -DPYTHON_EXECUTABLE="$HFS/python/bin/python3.13" \
  -DRPR_LOCATION=/absolute/path/to/RadeonProRenderSDK/RadeonProRender \
  -DRPR_NS_KERNELS_DIR=/absolute/path/to/RadeonProRenderSDK/hipbin \
  -DRPR_SDK_PLATFORM=ubuntu20.04 \
  -DPXR_BUILD_TESTS=OFF -DPXR_USE_PYTHON_3=ON
cmake --build build-houdini -j4
cmake --install build-houdini
```

Select the Python executable supplied by your Houdini version. Houdini UI
settings generation imports `hou`; our single-engine session ran that generator
inside the existing GUI, while USD schemas used ordinary bundled Python.
Do not replace installed shared libraries while Houdini has them loaded.

`RPR_ROCM7_ROOT` is empty by default. To opt into an installed HIP 7 runtime,
configure with `-DRPR_ROCM7_ROOT=/absolute/path/to/rocm-7`. This Linux-only option
builds `lib/hip7-loader/libamdhip64.so.6`, which depends on that installation's
HIP 7 library. Set `RPRUSD_HIP_LIBRARY` to the absolute path of this private
loader at runtime. It does not replace system libraries or select a Houdini
version. Leave both options unset to preserve the SDK's default selection.

The explicit loader uses `RTLD_LOCAL`; global HIP symbols collide with
Northstar/Orochi function-pointer variables. On this path, GPU discovery is
bounded by the device count from HIP because probing a nonexistent device
crashes the tested SDK. Explicit load failures are reported, not silently
converted into another backend.

Add the installed `houdini` directory to `HOUDINI_PATH` and `lib/python` to
`PYTHONPATH` through a Houdini package. The installed `houdini/dso/usd_plugins`
entry discovers the delegate. Restart Houdini after package installation, then
select RPR in Solaris. Scene render controls are on the generic Render Settings
LOP's RPR tab; the upstream `rpr_lop_rendererSettings` operator is not registered.

Runtime checks should cover a mesh, an analytic USD sphere, bound glass and
emissive materials, and an authored Render Settings prim. Houdini can provide a
four-channel display buffer for a three-channel RenderVar: conversion follows
the actual buffer format. Houdini's default `C.*[LO]` beauty expression maps to
the native color AOV, alongside the existing `C.*` alias. Other custom LPEs
retain their separate path and are not broadly validated here. The scene-index
plugin requires an integer priority
in its metadata, even when registered in C++.

The HUD exposes samples/cap, output resolution, preview scale, render GPU,
CPU render threads and samples/second. Native viewport FPS measures drawing,
not fully sampled animation throughput. Preview downscale 1 means half width
and height while interacting; 0 means full resolution. Still sample limits
and interaction resolution are separate controls. Adaptive convergence may
finish before reaching the maximum sample count.

In H22.0.429, repeated `hou.SceneViewer.setRendererPaused(False)` calls toggled
the renderer instead of acting idempotently. Check `isRendererPaused()` first
and call the setter only if the state differs. This workaround also works with
normal setters; it should not replace the user's deliberate pause choice.
