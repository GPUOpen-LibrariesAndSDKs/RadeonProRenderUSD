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

#include "pxr/base/tf/diagnostic.h"

PXR_NAMESPACE_OPEN_SCOPE

HdRprRenderThread::HdRprRenderThread()
    : m_stopCallback([]() {})
    , m_renderCallback([]() { TF_CODING_ERROR("StartThread() called without a render callback set"); })
    , m_shutdownCallback([]() {})
    , m_requestedState(StateInitial)
    , m_pauseRender(false)
    , m_stopRequested(false)
    , m_rendering(false) {

}

HdRprRenderThread::~HdRprRenderThread() {
    StopThread();
}

void HdRprRenderThread::SetRenderCallback(std::function<void()> renderCallback) {
    if (renderCallback) {
        m_renderCallback = renderCallback;
    }
}

void HdRprRenderThread::SetStopCallback(std::function<void()> stopCallback) {
    if (stopCallback) {
        m_stopCallback = stopCallback;
    }
}

void HdRprRenderThread::SetShutdownCallback(std::function<void()> shutdownCallback) {
    if (shutdownCallback) {
        m_shutdownCallback = shutdownCallback;
    }
}

void HdRprRenderThread::StartThread() {
    std::lock_guard<std::mutex> controlLock(m_controlMutex);
    if (m_renderThread.joinable()) {
        TF_CODING_ERROR("StartThread() called while render thread is already running");
        return;
    }

    m_requestedState = StateIdle;
    m_renderThread = std::thread(&HdRprRenderThread::RenderLoop, this);
}

void HdRprRenderThread::StopThread() {
    std::lock_guard<std::mutex> controlLock(m_controlMutex);
    if (!m_renderThread.joinable()) {
        return;
    }

    // Wake a paused callback and request SDK abort before waiting for the
    // callback's state mutex. All delegate members are still alive here.
    RequestStop();
    {
        std::unique_lock<std::mutex> lock(m_requestedStateMutex);
        m_requestedState = StateTerminated;
    }
    m_requestedStateCV.notify_one();
    m_renderThread.join();
}

bool HdRprRenderThread::IsThreadRunning() {
    std::lock_guard<std::mutex> controlLock(m_controlMutex);
    return m_renderThread.joinable();
}

void HdRprRenderThread::StartRender() {
    std::lock_guard<std::mutex> controlLock(m_controlMutex);
    if (IsRendering()) {
        return;
    }
    std::unique_lock<std::mutex> lock(m_requestedStateMutex);
    if (m_requestedState != StateIdle) {
        return;
    }
    {
        std::lock_guard<std::mutex> pauseLock(m_pauseWaitMutex);
        m_stopRequested.store(false);
    }
    m_requestedState = StateRendering;
    m_rendering.store(true);
    m_requestedStateCV.notify_one();
}

void HdRprRenderThread::RequestStop() {
    {
        // The wait predicate and notification share this mutex, preventing a
        // stop request from being lost between the pause check and wait.
        std::lock_guard<std::mutex> lock(m_pauseWaitMutex);
        m_stopRequested.store(true);
    }
    m_pauseWaitCV.notify_all();
    if (IsRendering()) {
        m_stopCallback();
    }
}

void HdRprRenderThread::StopRender(bool blocking) {
    std::lock_guard<std::mutex> controlLock(m_controlMutex);
    RequestStop();
    if (blocking) {
        // RenderLoop holds this mutex throughout the callback. Acquiring it
        // guarantees that the callback has finished before scene edits begin.
        std::unique_lock<std::mutex> lock(m_requestedStateMutex);
        if (m_requestedState == StateRendering) {
            m_requestedState = StateIdle;
        }
        m_rendering.store(false);
    }
}

bool HdRprRenderThread::IsRendering() {
    return m_rendering.load();
}

bool HdRprRenderThread::IsStopRequested() {
    return m_stopRequested.load();
}

void HdRprRenderThread::PauseRender() {
    std::lock_guard<std::mutex> lock(m_pauseWaitMutex);
    m_pauseRender = true;
}

void HdRprRenderThread::ResumeRender() {
    {
        std::lock_guard<std::mutex> lock(m_pauseWaitMutex);
        m_pauseRender = false;
    }
    m_pauseWaitCV.notify_all();
}

bool HdRprRenderThread::IsPaused() const {
    std::lock_guard<std::mutex> lock(m_pauseWaitMutex);
    return m_pauseRender;
}

void HdRprRenderThread::WaitUntilPaused() {
    std::unique_lock<std::mutex> lock(m_pauseWaitMutex);
    m_pauseWaitCV.wait(lock, [this]() {
        return !m_pauseRender || IsStopRequested();
    });
}

void HdRprRenderThread::RenderLoop() {
    while (1) {
        std::unique_lock<std::mutex> lock(m_requestedStateMutex);
        m_requestedStateCV.wait(lock, [this]() {
            return m_requestedState != StateIdle;
        });
        if (m_requestedState == StateRendering) {
            m_renderCallback();
            m_rendering.store(false);
            m_requestedState = StateIdle;
        } else if (m_requestedState == StateTerminated) {
            break;
        }
    }
    m_shutdownCallback();
}

PXR_NAMESPACE_CLOSE_SCOPE
