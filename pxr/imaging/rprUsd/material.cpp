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

#include "pxr/imaging/rprUsd/material.h"
#include "pxr/imaging/rprUsd/error.h"

#include "pxr/base/gf/vec2f.h"

#include <RadeonProRender.hpp>

PXR_NAMESPACE_OPEN_SCOPE

bool RprUsdMaterial::AttachTo(rpr::Shape* mesh, bool displacementEnabled) const {
    bool fail = RPR_ERROR_CHECK(mesh->SetMaterial(m_surfaceNode), "Failed to set shape material");

    fail |= RPR_ERROR_CHECK(mesh->SetVolumeMaterial(m_volumeNode), "Failed to set shape volume material");

    if (displacementEnabled && m_displacementNode) {
        size_t dummy;
        int subdFactor;
        if (RPR_ERROR_CHECK(mesh->GetInfo(RPR_SHAPE_SUBDIVISION_FACTOR, sizeof(subdFactor), &subdFactor, &dummy), "Failed to query mesh subdivision factor")) {
            subdFactor = 0;
        }

        if (subdFactor == 0) {
            TF_WARN("Displacement material requires subdivision to be enabled. The subdivision will be enabled with refine level of 1");
            if (!RPR_ERROR_CHECK(mesh->SetSubdivisionFactor(1), "Failed to set mesh subdividion")) {
                subdFactor = 1;
            }
        }
        if (subdFactor > 0) {
            fail |= RPR_ERROR_CHECK(mesh->SetDisplacementMaterial(m_displacementNode), "Failed to set shape displacement material");

            GfVec2f displacementScale(0.0f, 1.0f);
            if (m_displacementScale.IsHolding<GfVec2f>()) {
                displacementScale = m_displacementScale.UncheckedGet<GfVec2f>();
            }

            fail |= RPR_ERROR_CHECK(mesh->SetDisplacementScale(displacementScale[0], displacementScale[1]), "Failed to set shape displacement scale");
        }
    } else {
        fail |= RPR_ERROR_CHECK(mesh->SetDisplacementMaterial(nullptr), "Failed to unset shape displacement material");
    }

    fail |= RPR_ERROR_CHECK(mesh->SetShadowCatcher(m_isShadowCatcher), "Failed to set shape shadow catcher");
    fail |= RPR_ERROR_CHECK(mesh->SetReflectionCatcher(m_isReflectionCatcher), "Failed to set shape reflection catcher");

    return !fail;
}

bool RprUsdMaterial::AttachTo(rpr::Curve* curve) const {
    return !RPR_ERROR_CHECK(curve->SetMaterial(m_surfaceNode), "Failed to set curve material");
}

void RprUsdMaterial::DetachFrom(rpr::Shape* mesh) {
    RPR_ERROR_CHECK(mesh->SetMaterial(nullptr), "Failed to unset shape material");
    RPR_ERROR_CHECK(mesh->SetVolumeMaterial(nullptr), "Failed to unset shape volume material");
    RPR_ERROR_CHECK(mesh->SetDisplacementMaterial(nullptr), "Failed to unset shape displacement material");
    RPR_ERROR_CHECK(mesh->SetShadowCatcher(false), "Failed to unset shape shadow catcher");
    RPR_ERROR_CHECK(mesh->SetReflectionCatcher(false), "Failed to unset shape reflection catcher");
}

void RprUsdMaterial::DetachFrom(rpr::Curve* curve) {
    RPR_ERROR_CHECK(curve->SetMaterial(nullptr), "Failed to unset curve material");
}

void RprUsdMaterial::SetName(const char *name) {
    if (m_surfaceNode) m_surfaceNode->SetName(name);
    if (m_displacementNode) m_displacementNode->SetName(name);
    if (m_volumeNode) m_volumeNode->SetName(name);
}

PXR_NAMESPACE_CLOSE_SCOPE
