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

#include "domeLight.h"
#include "renderParam.h"
#include "primvarUtil.h"
#include "rprApi.h"

#include "pxr/imaging/rprUsd/debugCodes.h"
#include "pxr/imaging/rprUsd/tokens.h"
#include "pxr/imaging/rprUsd/lightRegistry.h"

#include "pxr/usd/ar/resolver.h"
#include "pxr/imaging/hd/light.h"
#include "pxr/imaging/hd/sceneDelegate.h"
#include "pxr/usd/sdf/assetPath.h"
#include "pxr/usd/usdLux/blackbody.h"
#include "pxr/base/gf/matrix4d.h"
#include "pxr/base/arch/env.h"

#ifdef BUILD_AS_HOUDINI_PLUGIN
#include <HOM/HOM_Module.h>
#include <HOM/HOM_ui.h>
#include <HOM/HOM_text.h>
#include <HOM/HOM_SceneViewer.h>
#include <HOM/HOM_GeometryViewport.h>
#include <HOM/HOM_GeometryViewportSettings.h>

#include <HOM/HOM_Node.h>
#include <HOM/HOM_ToggleParmTemplate.h>
#include <HOM/HOM_FloatParmTemplate.h>
#include <HOM/HOM_StringParmTemplate.h>
#include <HOM/HOM_ParmTemplateGroup.h>
#include <HOM/HOM_Parm.h>
#endif // BUILD_AS_HOUDINI_PLUGIN

#include <ghc/filesystem.hpp>
namespace fs = ghc::filesystem;

PXR_NAMESPACE_OPEN_SCOPE

static void removeFirstSlash(std::string& string) {
    // Don't need this for *nix/Mac
#ifdef _WIN32
    if (string[0] == '/' || string[0] == '\\') {
        string.erase(0, 1);
    }
#endif
}

static float computeLightIntensity(float intensity, float exposure) {
    return intensity * exp2(exposure);
}

#ifdef BUILD_AS_HOUDINI_PLUGIN

HOM_Node* FindNodeById(HOM_Node* node, const std::string& id) {
    auto schildren = node->children();
    for (HOM_ElemPtr<HOM_Node>& sc : schildren) {
        HOM_Parm* p = sc.myPointer->parm("primpath");
        if (p && p->evalAsString() == id) {
            return sc.myPointer;
        }

        HOM_Node* foundInSubnodes = FindNodeById(sc.myPointer, id);
        if (foundInSubnodes) {
            return foundInSubnodes;
        }
    }
    return nullptr;
}

HOM_Node* FindNodeById(HOM_Module& hou, const std::string& id) {
    HOM_Node* sceneNode = hou.node("/stage");
    return FindNodeById(sceneNode, id);
}

void CreateSceneBackgroundOverrideParms(HOM_Module& hou, HOM_Node* node) {
    HOM_StdMapStringString enableTags;
    enableTags.insert(std::pair<std::string, std::string>("usdvaluetype", "bool"));
    HOM_StdMapStringString colorTags;
    colorTags.insert(std::pair<std::string, std::string>("usdvaluetype", "color3f"));


    HOM_ParmTemplateGroup* group = node->parmTemplateGroup();
    HOM_ToggleParmTemplate* enableTemplate = hou.newToggleParmTemplate("rpr:backgroundOverrideGlobal:enable", "Global background override enable", false,
        nullptr, true, true, false, nullptr, nullptr, HOM_scriptLanguage::Hscript, enableTags, HOM_StdMapEnumString(), "", HOM_scriptLanguage::Hscript);
    HOM_StringParmTemplate* enableControlTemplate = hou.newStringParmTemplate("rpr:backgroundOverrideGlobal:enable_control", "Global background override enable", 1, std::vector<std::string>(1, "set"), 
        HOM_parmNamingScheme::Base1, HOM_stringParmType::Regular, HOM_fileType::Any, std::vector<std::string>(), std::vector<std::string>(), std::vector<std::string>(), "",
        nullptr, HOM_menuType::Normal, nullptr, true, true, true, nullptr, nullptr, HOM_scriptLanguage::Hscript, HOM_StdMapStringString(), HOM_StdMapEnumString(), std::vector<std::string>(), std::vector<HOM_EnumValue*>());
    HOM_FloatParmTemplate* colorTemplate = hou.newFloatParmTemplate("rpr:backgroundOverrideGlobal:color", "Global background override color", 3, std::vector<double>(3, 0.0),
        0, 1, true, true, HOM_parmLook::Regular, HOM_parmNamingScheme::RGBA, nullptr, true, true, false, nullptr, nullptr, HOM_scriptLanguage::Hscript, colorTags, HOM_StdMapEnumString(), std::vector<std::string>(), std::vector<HOM_EnumValue*>());
    HOM_StringParmTemplate* colorControlTemplate = hou.newStringParmTemplate("rpr:backgroundOverrideGlobal:color_control", "Global background override color", 1, std::vector<std::string>(1, "set"),
        HOM_parmNamingScheme::Base1, HOM_stringParmType::Regular, HOM_fileType::Any, std::vector<std::string>(), std::vector<std::string>(), std::vector<std::string>(), "",
        nullptr, HOM_menuType::Normal, nullptr, true, true, true, nullptr, nullptr, HOM_scriptLanguage::Hscript, HOM_StdMapStringString(), HOM_StdMapEnumString(), std::vector<std::string>(), std::vector<HOM_EnumValue*>());

    group->append(*enableControlTemplate);
    group->append(*enableTemplate);
    group->append(*colorControlTemplate);
    group->append(*colorTemplate);
    node->setParmTemplateGroup(*group);
}

void SetSceneBackgroundOverride(HOM_Module& hou, HOM_Node* node, HdRprApi::BackgroundOverride & override) {
    if (!node) {
        return;
    }
    HOM_text& text = hou.text();
    HOM_Parm* enableParm = node->parm(text.encode("rpr:backgroundOverrideGlobal:enable").c_str());
    HOM_Parm* colorParmR = node->parm((text.encode("rpr:backgroundOverrideGlobal:color") + "r").c_str());
    HOM_Parm* colorParmG = node->parm((text.encode("rpr:backgroundOverrideGlobal:color") + "g").c_str());
    HOM_Parm* colorParmB = node->parm((text.encode("rpr:backgroundOverrideGlobal:color") + "b").c_str());
    if (!enableParm || !colorParmR || !colorParmG || !colorParmB) {
        if (enableParm || colorParmR || colorParmG || colorParmB) {
            // some parms exists, some not - inconsistent situation, exiting
            return;
        }
        CreateSceneBackgroundOverrideParms(hou, node);
        enableParm = node->parm(text.encode("rpr:backgroundOverrideGlobal:enable").c_str());
        colorParmR = node->parm((text.encode("rpr:backgroundOverrideGlobal:color") + "r").c_str());
        colorParmG = node->parm((text.encode("rpr:backgroundOverrideGlobal:color") + "g").c_str());
        colorParmB = node->parm((text.encode("rpr:backgroundOverrideGlobal:color") + "b").c_str());
        if (!enableParm || !colorParmR || !colorParmG || !colorParmB) {
            return;
        }
    }

    enableParm->_set(override.enable ? 1 : 0);
    colorParmR->_set(override.color[0]);
    colorParmG->_set(override.color[1]);
    colorParmB->_set(override.color[2]);
}

HdRprApi::BackgroundOverride BackgroundOverrideSettings(HdSceneDelegate* sceneDelegate, const SdfPath& nodeId) {
    HdRprApi::BackgroundOverride result;
    result.enable = false;
    result.color = GfVec3f(1.0f);
    try {
        //HOM_AutoUnlock lock();
        HOM_Module& hou = HOM();
        std::string an = hou.applicationName();

        if (an.rfind("houdini", 0) != 0) {
            result.enable = HdRpr_GetParam(sceneDelegate, nodeId, RprUsdTokens->rprBackgroundOverrideGlobalEnable, false);
            result.color = HdRpr_GetParam(sceneDelegate, nodeId, RprUsdTokens->rprBackgroundOverrideGlobalColor, GfVec3f(1.0f));
            return result;
        }

        HOM_ui& ui = hou.ui();
        HOM_SceneViewer* sceneViewer = dynamic_cast<HOM_SceneViewer*>(ui.paneTabOfType(HOM_paneTabType::SceneViewer));
        if (sceneViewer) {
            HOM_GeometryViewport* viewport = sceneViewer->selectedViewport();
            HOM_GeometryViewportSettings* settings = viewport->settings();
            result.enable = !settings->displayEnvironmentBackgroundImage();
            std::string schemeName = settings->colorScheme().name();
            if (schemeName == "Grey") {
                result.color = GfVec3f(0.5f);
            }
            else if (schemeName == "Dark") {
                result.color = GfVec3f(0.0f);
            }
            else {          // Light
                result.color = GfVec3f(1.0f);
            }
        }
        HOM_Node* node = FindNodeById(hou, nodeId.GetAsString());
        if (node) {
            SetSceneBackgroundOverride(hou, node, result);
        }
        return result;
    }
    catch (...) {}
    return result;
}

void CreateOverrideEnableParmIfNeeded(const SdfPath& nodeId) {
    HOM_Module& hou = HOM();
    std::string an = hou.applicationName();

    if (an.rfind("houdini", 0) != 0) {
        return;
    }

    HOM_Node* node = FindNodeById(hou, nodeId.GetAsString());
    if (node) {
        HOM_text& text = hou.text();
        HOM_Parm* enableParm = node->parm(text.encode("rpr:backgroundOverride:enable").c_str());
        if (!enableParm) {
            auto lang = HOM_scriptLanguage::Python;
            HOM_ParmTemplateGroup* group = node->parmTemplateGroup();
            std::string enableControlName = text.encode("rpr:backgroundOverride:enable_control");
            HOM_StringParmTemplate* enableControlTemplate = hou.newStringParmTemplate(enableControlName.c_str(), "Background override", 1, std::vector<std::string>(1, "none"),
                HOM_parmNamingScheme::Base1, HOM_stringParmType::Regular, HOM_fileType::Any, std::vector<std::string>(), std::vector<std::string>(), std::vector<std::string>(), "import loputils\nreturn loputils.createEditPropertiesControlMenu(kwargs, 'bool[]')",
                &lang, HOM_menuType::ControlNextParameter, nullptr, false, false, false, nullptr, nullptr, HOM_scriptLanguage::Hscript, HOM_StdMapStringString(), HOM_StdMapEnumString(), std::vector<std::string>(), std::vector<HOM_EnumValue*>());
            group->appendToFolder("RPR", *enableControlTemplate);

            HOM_StdMapStringString enableTags;
            std::string enableName = text.encode("rpr:backgroundOverride:enable");
            std::string disableWhen = "{ " + enableControlName + " == block } { " + enableControlName + " == none }";
            enableTags.insert(std::pair<std::string, std::string>("usdvaluetype", "bool"));
            HOM_ToggleParmTemplate* enableTemplate = hou.newToggleParmTemplate(enableName.c_str(), "Background override", false,
                disableWhen.c_str(), false, false, false, nullptr, nullptr, HOM_scriptLanguage::Hscript, enableTags, HOM_StdMapEnumString(), "", HOM_scriptLanguage::Hscript);
            group->appendToFolder("RPR", *enableTemplate);
            node->setParmTemplateGroup(*group);
        }
    }
}

#else

HdRprApi::BackgroundOverride BackgroundOverrideSettings(HdSceneDelegate* sceneDelegate, const SdfPath& nodeId) {
    HdRprApi::BackgroundOverride result;
    result.enable = false;
    result.color = GfVec3f(1.0f);
    return result;
}

void CreateOverrideEnableParmIfNeeded(const SdfPath& nodeId) {
}

#endif // BUILD_AS_HOUDINI_PLUGIN



// RPR does not support .rat files, so we have to convert them to .exr and replace the path
// this function runs conversion and changes the 'path' argument if needed; returns true on success
bool ResolveRat(std::string& path) {
    auto originalPath = fs::path(path);
    if (originalPath.extension() != ".rat") {
        return true;
    }
    else {
        auto convertedName = originalPath.replace_extension(".exr");
        auto cachePath = ArchGetEnv("HDRPR_CACHE_PATH_OVERRIDE");
        auto targetPathInCache = fs::path(cachePath) / "convertedrat";
        auto convertedNameInCache = targetPathInCache / convertedName.filename();
        // at first, looking for converted file in the location of the original one
        if (fs::exists(convertedName)) {
            path = convertedName.string();
            return true;
        }
        // looking for converted file in cache directory
        else if (fs::exists(convertedNameInCache)) {
            path = convertedNameInCache.string();
            return true;
        }
        // concersion needed
        else {
            auto houbin = ArchGetEnv("HB");
            if (houbin.empty()) {
                return false;
            }
            auto convertor = fs::path(houbin) / "iconvert";
            std::string command = convertor.string() + " " + path + " " + convertedName.string();
            // trying to write converted file in the location of the original one
            if (system(command.c_str()) != 0)
            {
                if (cachePath.empty()) {
                    return false;
                }
                // original file directory could be read only, in this case trying to write into cache directory
                if (!(fs::is_directory(targetPathInCache) && fs::exists(targetPathInCache))) {
                    if (!fs::create_directory(targetPathInCache)) {
                        return false;
                    }
                }
                convertedName = convertedNameInCache;
                command = convertor.string() + " " + path + " " + convertedName.string();
                if (system(command.c_str()) != 0)
                {
                    return false;
                }
            }
            path = convertedName.string();
            return true;
        }
    }
}

HdRprDomeLight::HdRprDomeLight(SdfPath const& id)
    : HdSprim(id) {
    CreateOverrideEnableParmIfNeeded(id);
}

void HdRprDomeLight::Sync(HdSceneDelegate* sceneDelegate,
                          HdRenderParam* renderParam,
                          HdDirtyBits* dirtyBits) {
    auto rprRenderParam = static_cast<HdRprRenderParam*>(renderParam);
    auto rprApi = rprRenderParam->AcquireRprApiForEdit();

    SdfPath const& id = GetId();
    HdDirtyBits bits = *dirtyBits;

    if (bits & HdLight::DirtyTransform) {
#if PXR_VERSION >= 2011
        m_transform = GfMatrix4f(sceneDelegate->GetTransform(id));
#else
        m_transform = GfMatrix4f(HdRpr_GetParam(sceneDelegate, id, HdTokens->transform).Get<GfMatrix4d>());
#endif
        m_transform = GfMatrix4f(1.0).SetScale(GfVec3f(1.0f, 1.0f, -1.0f)) * m_transform;
    }

    bool newLight = false;
    if (bits & HdLight::DirtyParams) {
        if (m_rprLight) {
            rprApi->Release(m_rprLight);
            m_rprLight = nullptr;
        }

        bool isVisible = sceneDelegate->GetVisible(id);
        if (!isVisible) {
            *dirtyBits = HdLight::Clean;
            return;
        }

        HdRprApi::BackgroundOverride backgroundOverride;
        HdRprApi::BackgroundOverride overrideSettings = BackgroundOverrideSettings(sceneDelegate, id);
        backgroundOverride.enable = overrideSettings.enable || HdRpr_GetParam(sceneDelegate, id, RprUsdTokens->rprBackgroundOverrideEnable, false);
        backgroundOverride.color = overrideSettings.color;

        float intensity = HdRpr_GetParam(sceneDelegate, id, HdLightTokens->intensity, 1.0f);
        float exposure = HdRpr_GetParam(sceneDelegate, id, HdLightTokens->exposure, 1.0f);
        float computedIntensity = computeLightIntensity(intensity, exposure);

        std::string texturePath;
        VtValue texturePathValue = HdRpr_GetParam(sceneDelegate, id, HdLightTokens->textureFile);
        if (texturePathValue.IsHolding<SdfAssetPath>()) {
            auto& assetPath = texturePathValue.UncheckedGet<SdfAssetPath>();
            if (assetPath.GetResolvedPath().empty()) {
                texturePath = ArGetResolver().Resolve(assetPath.GetAssetPath());
            } else {
                texturePath = assetPath.GetResolvedPath();
            }
            ResolveRat(texturePath);
            // XXX: Why?
            removeFirstSlash(texturePath);
        } else if (texturePathValue.IsHolding<std::string>()) {
            // XXX: Is it even possible?
            texturePath = texturePathValue.UncheckedGet<std::string>();
            ResolveRat(texturePath);
        }

        if (texturePath.empty()) {
            GfVec3f color = HdRpr_GetParam(sceneDelegate, id, HdPrimvarRoleTokens->color, GfVec3f(1.0f));
            if (HdRpr_GetParam(sceneDelegate, id, HdLightTokens->enableColorTemperature, false)) {
                GfVec3f temperatureColor = UsdLuxBlackbodyTemperatureAsRgb(HdRpr_GetParam(sceneDelegate, id, HdLightTokens->colorTemperature, 5000.0f));
                color[0] *= temperatureColor[0];
                color[1] *= temperatureColor[1];
                color[2] *= temperatureColor[2];
            }

            m_rprLight = rprApi->CreateEnvironmentLight(color, computedIntensity, backgroundOverride);
        } else {
            m_rprLight = rprApi->CreateEnvironmentLight(texturePath, computedIntensity, backgroundOverride);
        }

        if (m_rprLight) {
            newLight = true;

            if (RprUsdIsLeakCheckEnabled()) {
                rprApi->SetName(m_rprLight, id.GetText());
            }
            RprUsdLightRegistry::Register(id, GetLightObject(m_rprLight));
        }
    }

    if (newLight || ((bits & HdLight::DirtyTransform) && m_rprLight)) {
        rprApi->SetTransform(m_rprLight, m_transform);
    }

    *dirtyBits = HdLight::Clean;
}

HdDirtyBits HdRprDomeLight::GetInitialDirtyBitsMask() const {
    return HdLight::AllDirty;
}

void HdRprDomeLight::Finalize(HdRenderParam* renderParam) {
    auto rprRenderParam = static_cast<HdRprRenderParam*>(renderParam);
    
    if (m_rprLight) {
        RprUsdLightRegistry::Release(GetId());
        rprRenderParam->AcquireRprApiForEdit()->Release(m_rprLight);
        m_rprLight = nullptr;
    }

    HdSprim::Finalize(renderParam);
}

PXR_NAMESPACE_CLOSE_SCOPE
