These standalone Linux CPU tests compile the real `renderThread.cpp` against
Houdini's HDK headers and link only USD Tf/Arch. They do not load hdRpr, create
an RPR context, launch Houdini, or use a GPU. No plugin build/install is needed.

From this directory, with a C++ compiler and GNU `timeout` installed:

```bash
bash runRenderThreadTests.sh /path/to/hfs22.0.429
ASAN_OPTIONS=detect_leaks=0 bash runRenderThreadTests.sh /path/to/hfs22.0.429 20 address,undefined
```

The second argument selects the C++ standard required by the HDK (20 for
Houdini 22; use 17 for a compatible older HDK). The optional third argument
passes a sanitizer list to the compiler. Leak detection is disabled in the
example because LeakSanitizer cannot run under the ptrace-based execution
sandbox used for this validation; address and undefined-behavior checks remain
active. Outside such a sandbox it can be left enabled. Temporary binaries are
removed when the script exits.

`testHdRprPausedShutdown.cpp` is a small regression that also compiles with the
old thread interface. The worker enters a paused callback before its owner
calls `StopThread()`. With the pre-fix implementation, a 3-second timeout exits
124; with the fix, it exits 0. To reproduce the old failure, compile this test
against the pre-fix `renderThread.h` and `renderThread.cpp`, then run it with
`timeout 3s`.

`testHdRprRenderThread.cpp` covers repeated idle shutdown/thread recreation,
100 paused shutdown attempts, abort callback delivery for in-flight work,
nonblocking then blocking stop, restart after cancellation while preserving
pause, 300 concurrent pause/start/stop cycles, and destruction before callback
state expires. Internal event deadlines and the process timeout turn hangs into
failures. The full suite requires the new `StopRender(bool)` and `IsPaused()`
methods.

These tests exercise thread ownership and synchronization. Actual delegate
recreation, SDK device-loss cleanup, AOV allocation/rebinding, and rendering
need separate host integration checks.
