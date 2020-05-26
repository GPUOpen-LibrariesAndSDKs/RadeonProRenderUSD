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

//
// Copyright 2017 Pixar
//
// Licensed under the Apache License, Version 2.0 (the "Apache License")
// with the following modification; you may not use this file except in
// compliance with the Apache License and the following modification to it:
// Section 6. Trademarks. is deleted and replaced with:
//
// 6. Trademarks. This License does not grant permission to use the trade
//    names, trademarks, service marks, or product names of the Licensor
//    and its affiliates, except as required to comply with Section 4(c) of
//    the License and to reproduce the content of the NOTICE file.
//
// You may obtain a copy of the Apache License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the Apache License with the above modification is
// distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied. See the Apache License for the specific
// language governing permissions and limitations under the Apache License.
//
#include "rendererPlugin.h"
#include "renderDelegate.h"

#include "pxr/imaging/hd/rendererPluginRegistry.h"

PXR_NAMESPACE_OPEN_SCOPE

TF_REGISTRY_FUNCTION(TfType) {
    HdRendererPluginRegistry::Define<HdRprPlugin>();
}

HdRenderDelegate* HdRprPlugin::CreateRenderDelegate() {
    return new HdRprDelegate(HdRenderSettingsMap());
}

HdRenderDelegate* HdRprPlugin::CreateRenderDelegate(HdRenderSettingsMap const& settingsMap) {
    return new HdRprDelegate(settingsMap);
}

void HdRprPlugin::DeleteRenderDelegate(HdRenderDelegate* renderDelegate) {
    delete renderDelegate;
}

PXR_NAMESPACE_CLOSE_SCOPE
