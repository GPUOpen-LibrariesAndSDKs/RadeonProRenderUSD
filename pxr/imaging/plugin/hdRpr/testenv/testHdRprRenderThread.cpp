/************************************************************************
Copyright 2020 Advanced Micro Devices, Inc
Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at
    http://www.apache.org/licenses/LICENSE-2.0
Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
************************************************************************/

#include "renderThread.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <utility>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); std::abort(); } } while (false)
using pxr::HdRprRenderThread;
struct Event {
    std::mutex mutex;
    std::condition_variable cv;
    bool ready = false;
    void Set() { { std::lock_guard<std::mutex> lock(mutex); ready = true; } cv.notify_all(); }
    void Wait() { std::unique_lock<std::mutex> lock(mutex); CHECK(cv.wait_for(lock, std::chrono::seconds(2), [this] { return ready; })); }
};
void IdleShutdownAndRecreate() {
    HdRprRenderThread thread;
    std::atomic<int> shutdowns(0);
    thread.SetShutdownCallback([&] { ++shutdowns; });
    for (int i = 0; i < 30; ++i) { thread.StartThread(); CHECK(thread.IsThreadRunning()); thread.StopThread(); CHECK(!thread.IsThreadRunning()); thread.StopThread(); }
    CHECK(shutdowns == 30);
}
void PausedShutdown() {
    for (int i = 0; i < 100; ++i) {
        HdRprRenderThread thread;
        Event entered;
        std::atomic<bool> exited(false);
        thread.SetRenderCallback([&] { entered.Set(); thread.WaitUntilPaused(); CHECK(thread.IsStopRequested()); exited = true; });
        thread.PauseRender();
        thread.StartThread();
        thread.StartRender();
        entered.Wait();
        if (i == 0) std::this_thread::sleep_for(std::chrono::milliseconds(20));
        thread.StopThread();
        CHECK(exited);
    }
}
void ShutdownAbortsInFlightWork() {
    HdRprRenderThread thread;
    Event entered, aborted;
    std::atomic<bool> exited(false);
    thread.SetRenderCallback([&] { entered.Set(); aborted.Wait(); CHECK(thread.IsStopRequested()); exited = true; });
    thread.SetStopCallback([&] { aborted.Set(); });
    thread.StartThread(); thread.StartRender(); entered.Wait(); thread.StopThread(); CHECK(exited);
}
void NonblockingThenBlockingStop() {
    HdRprRenderThread thread;
    Event entered, release, requested;
    thread.SetRenderCallback([&] { entered.Set(); release.Wait(); CHECK(thread.IsStopRequested()); });
    thread.SetStopCallback([&] { requested.Set(); });
    thread.StartThread(); thread.StartRender(); entered.Wait();
    thread.StopRender(false); requested.Wait(); CHECK(thread.IsRendering());
    release.Set(); thread.StopRender(); CHECK(!thread.IsRendering()); thread.StopThread();
}
void StopRestartPreservesPause() {
    HdRprRenderThread thread;
    std::atomic<int> entered(0), completed(0);
    thread.SetRenderCallback([&] { ++entered; thread.WaitUntilPaused(); if (!thread.IsStopRequested()) ++completed; });
    thread.PauseRender(); CHECK(thread.IsPaused()); thread.StartThread();
    for (int i = 0; i < 100; ++i) { thread.StartRender(); thread.StopRender(); CHECK(!thread.IsRendering()); CHECK(thread.IsStopRequested()); }
    thread.StartRender(); CHECK(!thread.IsStopRequested());
    thread.ResumeRender(); CHECK(!thread.IsPaused());
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (completed == 0 && std::chrono::steady_clock::now() < until) std::this_thread::yield();
    CHECK(completed == 1); thread.StopRender(); thread.StopThread();
}
void ConcurrentPauseStop() {
    HdRprRenderThread thread;
    std::atomic<bool> finish(false);
    thread.SetRenderCallback([&] { while (!thread.IsStopRequested()) { thread.WaitUntilPaused(); std::this_thread::yield(); } });
    thread.StartThread();
    std::thread pauser([&] { while (!finish) { thread.PauseRender(); thread.ResumeRender(); } });
    for (int i = 0; i < 300; ++i) { thread.StartRender(); thread.StopRender(); }
    finish = true; pauser.join(); thread.StopThread();
}
void CallbackCannotOutliveOwnerState() {
    std::atomic<bool> alive(true), exited(false);
    struct Owner {
        HdRprRenderThread thread;
        struct State { std::atomic<bool>& alive; ~State() { alive = false; } } state;
        Event entered;
        Owner(std::atomic<bool>& alive, std::atomic<bool>& exited) : state{alive} {
            thread.SetRenderCallback([this, &exited] { entered.Set(); thread.WaitUntilPaused(); CHECK(state.alive); exited = true; });
            thread.PauseRender(); thread.StartThread(); thread.StartRender(); entered.Wait();
        }
        ~Owner() { thread.StopThread(); }
    };
    { Owner owner(alive, exited); }
    CHECK(exited && !alive);
}
int main() {
    IdleShutdownAndRecreate(); PausedShutdown(); ShutdownAbortsInFlightWork();
    NonblockingThenBlockingStop(); StopRestartPreservesPause(); ConcurrentPauseStop(); CallbackCannotOutliveOwnerState();
    std::puts("PASS: idle/paused/in-flight shutdown, nonblocking stop, restart, concurrent pause/stop, owner lifetime");
}
