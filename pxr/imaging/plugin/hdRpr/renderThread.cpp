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
    , m_stopRequested(false)
    , m_pauseRender(false)
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
    if (m_renderThread.joinable()) {
        TF_CODING_ERROR("StartThread() called while render thread is already running");
        return;
    }

    m_requestedState = StateIdle;
    m_renderThread = std::thread(&HdRprRenderThread::RenderLoop, this);
}

void HdRprRenderThread::StopThread() {
    if (!m_renderThread.joinable()) {
        return;
    }

    {
        m_enableRender.clear();
        std::unique_lock<std::mutex> lock(m_requestedStateMutex);
        m_requestedState = StateTerminated;
        m_requestedStateCV.notify_one();
    }
    m_renderThread.join();
}

bool HdRprRenderThread::IsThreadRunning() {
    return m_renderThread.joinable();
}

void HdRprRenderThread::StartRender() {
    if (!IsRendering()) {
        std::unique_lock<std::mutex> lock(m_requestedStateMutex);
        m_enableRender.test_and_set();
        m_requestedState = StateRendering;
        m_rendering.store(true);
        m_requestedStateCV.notify_one();
    }
}

void HdRprRenderThread::StopRender() {
    if (IsRendering()) {
        m_enableRender.clear();
        if (m_pauseRender) {
            // In case rendering thread was blocked by WaitUntilPaused, notify that stop is requested
            m_pauseWaitCV.notify_one();
        }
        // In case rendering thread currently inside of some sort of rendering task that could be stopped call stopCallback to speed up return from renderCallback
        m_stopCallback();
        std::unique_lock<std::mutex> lock(m_requestedStateMutex);
        m_requestedState = StateIdle;
        m_rendering.store(false);
    }
}

bool HdRprRenderThread::IsRendering() {
    return m_rendering.load();
}

bool HdRprRenderThread::IsStopRequested() {
    if (!m_enableRender.test_and_set()) {
        m_stopRequested = true;
    }

    return m_stopRequested;
}

void HdRprRenderThread::PauseRender() {
    std::unique_lock<std::mutex> lock(m_pauseWaitMutex);
    m_pauseRender = true;
}

void HdRprRenderThread::ResumeRender() {
    std::unique_lock<std::mutex> lock(m_pauseWaitMutex);
    m_pauseRender = false;
    m_pauseWaitCV.notify_one();
}

void HdRprRenderThread::WaitUntilPaused() {
    if (!m_pauseRender || IsStopRequested()) {
        return;
    }

    std::unique_lock<std::mutex> lock(m_pauseWaitMutex);
    while (m_pauseRender && !IsStopRequested()) {
        m_pauseWaitCV.wait(lock);
    }
}

void HdRprRenderThread::RenderLoop() {
    while (1) {
        std::unique_lock<std::mutex> lock(m_requestedStateMutex);
        m_requestedStateCV.wait(lock, [this]() {
            return m_requestedState != StateIdle;
        });
        if (m_requestedState == StateRendering) {
            m_renderCallback();
            m_stopRequested = false;
            m_rendering.store(false);
            m_requestedState = StateIdle;
        } else if (m_requestedState == StateTerminated) {
            break;
        }
    }
    m_shutdownCallback();
}

PXR_NAMESPACE_CLOSE_SCOPE
