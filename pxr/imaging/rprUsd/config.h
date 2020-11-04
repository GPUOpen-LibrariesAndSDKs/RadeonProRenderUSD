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

#ifndef PXR_IMAGING_RPR_USD_CONFIG_H
#define PXR_IMAGING_RPR_USD_CONFIG_H

#include "pxr/imaging/rprUsd/api.h"
#include "pxr/base/tf/singleton.h"

#include <mutex>
#include <memory>

PXR_NAMESPACE_OPEN_SCOPE

class RprUsdConfig {
public:
    RPRUSD_API
    static std::unique_lock<std::mutex> GetInstance(RprUsdConfig** instance);

    RPRUSD_API
    ~RprUsdConfig();

    RPRUSD_API
    std::string const& GetFilePath() const { return m_filepath; }

    RPRUSD_API
    bool IsRestartWarningEnabled() const;
    RPRUSD_API
    void SetRestartWarning(bool);

    RPRUSD_API
    std::string GetTextureCacheDir() const;
    RPRUSD_API
    void SetTextureCacheDir(std::string const&);

    RPRUSD_API
    std::string GetKernelCacheDir() const;
    RPRUSD_API
    void SetKernelCacheDir(std::string const&);

private:
    RprUsdConfig();
    friend class TfSingleton<RprUsdConfig>;

    void Save();

private:
    std::string m_filepath;

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

RPRUSD_API_TEMPLATE_CLASS(TfSingleton<RprUsdConfig>);

PXR_NAMESPACE_CLOSE_SCOPE

#endif // PXR_IMAGING_RPR_USD_CONFIG_H
