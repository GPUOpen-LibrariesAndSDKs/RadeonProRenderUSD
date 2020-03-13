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

#ifndef HDRPR_RENDER_THREAD_H
#define HDRPR_RENDER_THREAD_H

#include "pxr/pxr.h"

#include <condition_variable>
#include <functional>
#include <atomic>
#include <thread>
#include <mutex>

PXR_NAMESPACE_OPEN_SCOPE

class HdRprRenderThread {
public:
    HdRprRenderThread();
    ~HdRprRenderThread();

    void SetStopCallback(std::function<void()> stopCallback);
    void SetRenderCallback(std::function<void()> renderCallback);
    void SetShutdownCallback(std::function<void()> shutdownCallback);

    void StartThread();
    void StopThread();
    bool IsThreadRunning();

    void StartRender();
    void StopRender();
    bool IsStopRequested();
    bool IsRendering();

    void PauseRender();
    void ResumeRender();

    void WaitUntilPaused();

private:
    void RenderLoop();

    std::function<void()> m_stopCallback;
    std::function<void()> m_renderCallback;
    std::function<void()> m_shutdownCallback;

    enum State {
        StateInitial,
        StateIdle,
        StateRendering,
        StateTerminated,
    };

    State m_requestedState;
    std::mutex m_requestedStateMutex;
    std::condition_variable m_requestedStateCV;

    std::mutex m_pauseWaitMutex;
    std::condition_variable m_pauseWaitCV;
    bool m_pauseRender;

    std::atomic_flag m_enableRender;
    bool m_stopRequested;

    std::atomic<bool> m_rendering;
    std::thread m_renderThread;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HDRPR_RENDER_THREAD_H
