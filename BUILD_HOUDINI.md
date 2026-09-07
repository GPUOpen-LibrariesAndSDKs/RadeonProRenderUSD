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

Renderer lifecycle changes make Houdini the single restart owner. The optional
`rpr:core:hybridGpuIndex` render setting selects one machine-local Hybrid GPU
index; `-1` retains the saved Devices configuration. An explicit index creates
only the selected context and reads its actual GPU name, without creating
throwaway contexts on the other GPUs. Houdini's `restartrendersettings` includes
this setting, render quality, and the legacy OpenCL backend choice. A controller
should author those settings together and must not also queue timed explicit
restarts. These additions do not require Houdini 22 or ROCm 7.

The delegate joins its worker before destroying configuration, wakes a paused
worker during shutdown, honors blocking/nonblocking stop, and synchronizes AOVs
before restarting. Edit guards now belong to individual delegates. Reallocating
an existing output buffer refreshes its SDK binding and write pointer. Interrupted
or failed renders no longer claim convergence; failed contexts stop submitting
frames and expose an error status. `renderContextId` identifies delegate
recreation for integration checks. CPU-only regression tests are documented in
`pxr/imaging/plugin/hdRpr/testenv/README.md`.

Earlier live validation failed. An NVIDIA HybridPro test on the RTX
2070 Super/595.84 driver stopped after two successful samples with a Vulkan
device loss; the kernel recorded Xid 109 (`CTX SWITCH TIMEOUT`). Attempting to
switch from that lost context crashed inside the SDK's HybridPro context
cleanup before AMD rendering began. Joining the worker correctly does not
establish that this SDK can recover from device loss. Earlier successful
captures must not be taken as proof of switching stability on either GPU.


The earlier build23 fixed-view sequence is **not accepted image/stability validation**.
AMD HybridPro completed 32 samples, switched to Northstar and back, and later
reported 128 samples; its floor pixels after increasing the sample cap were
exactly one quarter of the previous 32-sample values. Another return from Karma
produced large opaque white HDR bands with unchanged scene/view transform.
Completion counters alone must not pass this test. At 21:37:52 on September 6,
2026, AMD gfx ring timeouts/reset began in the same Houdini process. Switching
subsequently crashed at 21:38:37 in the same HybridPro context deletion stack.
Thus the failure is not confined to the NVIDIA device or merely color management.
The initiating cause remains unisolated; neither SDK nor driver blame is proven.

Source inspection also corrected motion-mesh property storage that allocated one
entry but wrote three, initialized light transforms/displacement state and timing
counters, and supplied defined clipping defaults when a reconstructed camera has
lens parameters without a clipping range. The bounded build25 check below includes
these corrections; it does not establish a fix for recovery after GPU resets.
The darkening now reproduces outside Houdini in the standalone HybridPro SDK
fixture, including after reboot. On the R9700/RADV 25.2.8 configuration, one
iteration per SDK render call gives mean RGB 0.00254655682 at 32 samples,
0.000636639204 at 128 samples (exactly one quarter), then zero after clear and
rerender. Native batching, reauthoring iterations and SVGF do not fix it.
The same fixture renders correctly on the RTX 2070 Super/595.84 driver.

`RADV_DEBUG=syncshaders` corrects accumulation and reset in these SDK fixtures.
This Mesa diagnostic flag is not enabled by the plugin and does not establish
whether the missing synchronization is an SDK or driver defect. With this flag,
six fresh HybridPro contexts alternated AMD/NVIDIA in one process, rendered
metal/glass spheres, procedural texture and emissive lighting at 512x512/128
samples, accepted light/camera edits, and completed all teardowns. Their mean
absolute RGB difference was 4.77e-6. These SDK-only results do not validate
recovery from device loss or production-scene stability; the separate Houdini
check is described below.
An earlier synchronized geometry-edit test still lost the AMD device; later
immediate and deferred deletion controls both passed. No speculative deferred
release queue was added to the delegate.

The standalone SDK fixture, image/teardown checker and reproduction instructions
are in `pxr/imaging/plugin/hdRpr/testenv/SDK_TESTS.md`. They require no Houdini
engine or window. CPU synchronization tests remain separate and use no GPU.


## Bounded GUI validation with the synchronization workaround

Installed build25 (renderer source commit `8367d5b`) was tested in one existing
H22.0.429 GUI launched with `RADV_DEBUG=syncshaders`, on the R9700/RADV 25.2.8.
The isolated scene contains chrome and glass spheres, a procedural striped
material and emissive geometry. Raw color AOVs were read directly through Hydra;
these checks did not use screen captures or additional Houdini engines.

At a fixed 935x1099 free-camera view, AMD HybridPro completed 32 then 128 samples
in the same context. Mean RGB changed from 0.187160 to 0.196604 (ratio 1.0505),
rather than the earlier quarter-brightness error. Switching to Northstar and
back created fresh RPR delegates and preserved the image. Switching to Karma CPU
and back also recreated HybridPro successfully: its mean RGB was 0.196844, with
no opaque white bands. Karma's selection/unpaused state was checked, but its
pixels were not inspected. No automatic pause, crash or new kernel GPU fault
was observed during this sequence. Denoising, upscaling and adaptive stopping
were disabled for the HybridPro checks.

A native Solaris camera translation and restore each generated new 128-sample
images without recreating the delegate. H22 HOM `setDefaultCamera()` and live
camera setters changed the reported viewport transform but left Hydra's free
camera and output unchanged, even with `draw()` and a redraw fence. Those probes
were rejected as evidence of camera-render progress. The successful test used
`viewtransform desktop.pane.solaris.viewport txyz ( x y z )`; `viewls -n -v`
identifies the actual Solaris viewport. The separate `.world.` view is not the
LOP camera. This is a host-notification observation, not an SDK camera fix.

The restored 128-sample frame accumulated 0.119 s in SDK render calls versus
1.422 s in resolve/readback. HUD samples/second excludes resolve, while
`totalRenderTime` continues growing after convergence; neither should be used
as complete frame latency. No resolve-throttling optimization was introduced.

This is a bounded material-scene pass with a diagnostic launch flag, not general
stability certification. Continuous navigation, GUI GPU changes, long playback,
ASVGF/FSR2, production fluid geometry and recovery from device loss remain
unvalidated with the workaround. The plugin does not enable `syncshaders`
automatically or require it on other driver/SDK configurations.
