# Standalone HybridPro diagnostics

`testHybridSdk.cpp` links only the RPR SDK. It does not use Hydra, Houdini, or a
window. Run it explicitly on a selected GPU; it is not part of the CPU test
suite. A driver or SDK failure can reset that GPU. It does not attempt to
recover a lost context, and a process timeout bounds SDK shutdown hangs.

Build using the headers and libraries from the same SDK version:

```bash
RPR_TEST_SDK=/absolute/path/to/RadeonProRenderSDK/RadeonProRender
RPR_TEST_LIB="$RPR_TEST_SDK/binUbuntu20"
c++ -std=c++17 -O2 -I"$RPR_TEST_SDK/inc" testHybridSdk.cpp \
  -L"$RPR_TEST_LIB" -lRadeonProRender64 -Wl,-rpath,"$RPR_TEST_LIB" \
  -o /tmp/testHybridSdk
mkdir -p /tmp/hybrid-results
timeout -k 3s 30s /tmp/testHybridSdk "$RPR_TEST_LIB/HybridPro.so" \
  /tmp/hybrid-results/default steady 0 > /tmp/hybrid-results/default.log 2>&1
python3 checkHybridSdk.py /tmp/hybrid-results/default.log
```

The positional arguments are the HybridPro library, output prefix, variant
(default `steady`), and the machine-local SDK GPU index (default 0; range 0–15).
The program prints the actual selected GPU name before rendering. Set
`RPR_TEST_EXPECT_GPU` to require a substring in that name. This selection does
not create probe contexts on other devices.

`RPR_TEST_SIZE` selects a square image, default 128 (32–2048).
`RPR_TEST_CYCLES` selects fresh context cycles, default 3 (1–20).
`RPR_TEST_ALTERNATE_GPU=1` alternates GPU0 and GPU1 between cycles, starting at
the supplied index; it requires both devices. Leave it unset for one device.
Output directories must already exist. Each snapshot is native float32 RGBA;
the log reports mean linear RGB. Timings include a completed framebuffer
readback and exclude context creation and writing files.

Every variant renders 32 samples, continues to 128 without clearing, then
clears and renders 128 again. The fixed illuminated scene should keep similar
radiance across those images. Successful SDK calls alone are **not** a passing
image test: `checkHybridSdk.py` checks that static brightness stays within 5%,
the light-off image is zero where present, and every started context closes.
The checker uses only Python's standard library. Inspect the raw images too;
mean radiance cannot catch every spatial artifact. The fixture and tolerance
are diagnostic, not a general image-comparison metric.

| Variant | Additional behavior |
| --- | --- |
| `steady` | One SDK iteration per render call; diffuse floor and an emissive quad. |
| `batch` | Submit native batches of 32, 96 and 128 iterations. |
| `set-iterations` | Reauthor one iteration before every render call. |
| `camera` | Turn the emitter off/on, move the camera, then restore it. |
| `materials` | Add metal and solid glass spheres plus a procedural checker floor; run the camera/light edits at a square camera aspect ratio. |
| `edits` | Also detach/delete/recreate the floor immediately. |
| `defer-delete` | Keep the detached floor alive through replacement rendering/readback, delete it, then render again. |
| `keep-detached` | Keep the detached floor alive until final teardown. |
| `churn` / `churn-immediate` | Replace floor, emitter and their materials 16 times per context; release old objects after one replacement render/readback, or immediately. |
| `svgf` | Use the SDK's SVGF denoiser. |
| `framecount` | Diagnostic unsupported-API probe; tested HybridPro returns -23. |
| `null-scene` | Deliberate reproduction of HybridPro crashing on `rprContextSetScene(context, nullptr)` during cleanup. Normal variants follow the SDK tutorial's direct scene deletion. |

On the tested R9700/RADV 25.2.8 configuration, the default path produces
exactly quarter brightness after increasing 32→128 samples, then black after
clearing. It reproduces after reboot. `RADV_DEBUG=syncshaders` removes this
failure in the standalone fixtures. Mesa documents this flag as synchronizing
after each draw/dispatch:
[RADV environment variables](https://docs.mesa3d.org/envvars.html#radv-driver-environment-variables).
It is a diagnostic workaround, not enabled by this program or the plugin.

With SDK v3.1.6.patch1, a 512×512 metal/glass fixture alternated six fresh
AMD/NVIDIA contexts with synchronization enabled, completed all teardowns,
passed the radiance/reset/light-off checks, and produced closely matching
images. This does not validate Houdini delegate switching or device-loss
recovery. An earlier AMD geometry-edit run with synchronization still lost the
device; subsequent immediate and deferred deletion controls both passed.
Deferred deletion is therefore **not** a demonstrated fix and was not added
to the delegate. Denoising, FSR2, and production-scene behavior need separate
checks.
