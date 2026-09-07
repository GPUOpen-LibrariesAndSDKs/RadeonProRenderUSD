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


## Optional preview paths

`HDRPR_ENABLE_OIDN` defaults to ON but is optional. CMake looks for Open Image
Denoise 2, preferring the selected Houdini installation. If disabled or absent,
the renderer still builds and shows the raw image. When available, Northstar can
apply the CPU OIDN filter to beauty with albedo/normal guides before tone mapping
and gamma. Alpha is preserved. GPU ray tracing stays on the selected render
GPU; the CPU only runs this spatial filter. The installed plugin retains the
selected OIDN library directory in its runtime search path. Rebuild for a moved
or different host installation.

Northstar exposes `rpr:denoising:enable` (default false) and
`rpr:denoising:minSamples` (default 16). Adaptive sampling can be used with this
filter. Filtering can soften fine reflected or transmitted features, so compare
against a higher-sample raw reference when approving materials.

Linux now registers the SDK's HybridPro backend. Its SVGF/ASVGF denoising can run
at native resolution by selecting `None` for `rpr:hybrid:upscalingQuality`.
The existing FSR2 quality modes remain available when a temporal denoiser is
enabled. This separates denoising from upscaling without changing the upstream
default quality selection. The HUD identifies the active backend, denoiser and
FSR2 mode; display updates are not completed rendered frames.

Hybrid/HybridPro device discovery uses the optional SDK supported-device export.
Each advertised physical index is probed with one GPU flag; its name is read as
active device zero. Older SDKs without that export retain GPU0 discovery.
Hybrid supports one selected GPU per context. Explicit multiple or stale device
selections fail rather than silently falling back to a different GPU. Device
configuration is reread when a context is created, so switching devices needs a
renderer-context restart, not a Houdini restart.

Both GPUs must be visible to Vulkan for index selection. On the tested Linux
workstation, `MESA_VK_DEVICE_SELECT=10de:1e84` prefers NVIDIA for Houdini's display
while leaving AMD available to HybridPro. A trailing `!` or
`MESA_VK_DEVICE_SELECT_FORCE_DEFAULT_DEVICE` hides other devices. Restricting
`VK_DRIVER_FILES` to a single driver likewise changes discovery. These are
workstation environment choices, not mandatory plugin settings.

Local checks include a normal-launcher load with bundled OIDN 2.3.3, matched
128-sample Northstar raw/OIDN captures, and adaptive sampling plus OIDN. A
standalone helper check verified finite HDR output, preserved alpha, resolution
changes, and operation with/without guides. Configure also succeeds with both
`HDRPR_ENABLE_OIDN=OFF` and `RPR_ROCM7_ROOT` empty. Other Houdini/OS combinations
remain passively supported but untested.


HybridPro device-index discovery was checked on NVIDIA RTX 2070 Super, AMD
Radeon AI PRO R9700 and llvmpipe. AMD selection survived an in-GUI context
restart with an unchanged camera. However, a later AMD HybridPro render/switch
on RADV 25.2.8 hit VK_ERROR_DEVICE_LOST, hard GPU recovery and a Houdini crash.
Treat this AMD Vulkan path as experimental; a device-name query or an earlier
completed frame does not establish stability. Northstar/HIP is the validated
AMD path for this workstation. No matched AMD/NVIDIA performance claim is made
from the incomplete HybridPro sequence.

NVIDIA HybridPro also completed a fixed-camera128-sample SVGF capture at native
resolution. It reduced noise in some regions while leaving substantial noise in
reflections/transmission. ASVGF/FSR2 image quality and temporal behavior remain
unvalidated in the GUI; those controls are exposed but not production-approved.
