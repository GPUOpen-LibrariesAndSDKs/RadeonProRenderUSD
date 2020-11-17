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

#include "VOP_RPRMaterial.h"

#include <pxr/base/tf/stringUtils.h>
#include <pxr/base/arch/fileSystem.h>

#include <PRM/PRM_Include.h>
#include <PRM/PRM_SpareData.h>

#include <MaterialXFormat/XmlIo.h>

PXR_NAMESPACE_OPEN_SCOPE

/**
    Simple identity macro that does nothing but helps to identify allocations of
    memory that will never be deleted, until we find a way to do so.
*/
#define LEAKED(ptr) ptr

static VOP_Type GetVOPType(RprUsdMaterialNodeInput::Type rprType) {
    switch (rprType) {
        case RprUsdMaterialNodeElement::kAngle:
        case RprUsdMaterialNodeElement::kFloat:
            return VOP_TYPE_FLOAT;
        case RprUsdMaterialNodeElement::kVector3:
            return VOP_TYPE_VECTOR;
        case RprUsdMaterialNodeElement::kVector2:
            return VOP_TYPE_VECTOR2;
        case RprUsdMaterialNodeElement::kColor3:
            return VOP_TYPE_COLOR;
        case RprUsdMaterialNodeElement::kNormal:
            return VOP_TYPE_NORMAL;
        case RprUsdMaterialNodeElement::kBoolean:
        case RprUsdMaterialNodeElement::kInteger:
            return VOP_TYPE_INTEGER;
        case RprUsdMaterialNodeElement::kToken:
        case RprUsdMaterialNodeElement::kString:
        case RprUsdMaterialNodeElement::kFilepath:
            return VOP_TYPE_STRING;
        case RprUsdMaterialNodeElement::kVolumeShader:
            return VOP_ATMOSPHERE_SHADER;
        case RprUsdMaterialNodeElement::kSurfaceShader:
            return VOP_SURFACE_SHADER;
        case RprUsdMaterialNodeElement::kDisplacementShader:
            return VOP_DISPLACEMENT_SHADER;
        default:
            return VOP_TYPE_UNDEF;
    }
}

static PRM_Type const& GetPRMType(RprUsdMaterialNodeInput::Type rprType) {
    switch (rprType) {
        case RprUsdMaterialNodeElement::kFloat:
            return PRM_FLT;
        case RprUsdMaterialNodeElement::kAngle:
            return PRM_ANGLE;
        case RprUsdMaterialNodeElement::kVector2:
        case RprUsdMaterialNodeElement::kVector3:
        case RprUsdMaterialNodeElement::kNormal:
            return PRM_XYZ;
        case RprUsdMaterialNodeElement::kColor3:
            return PRM_RGB;
        case RprUsdMaterialNodeElement::kInteger:
            return PRM_INT;
        case RprUsdMaterialNodeElement::kBoolean:
            return PRM_TOGGLE;
        case RprUsdMaterialNodeElement::kToken:
            return PRM_ORD_E;
        case RprUsdMaterialNodeElement::kString:
            return PRM_STRING_E;
        case RprUsdMaterialNodeElement::kFilepath:
            return PRM_FILE;
        default:
            return PRM_LIST_TERMINATOR;
    }
}

static PRM_Range* NewPRMRange(RprUsdMaterialNodeInput const* input) {
    if (input->GetType() == RprUsdMaterialNodeElement::kFloat ||
        input->GetType() == RprUsdMaterialNodeElement::kInteger) {
        bool isRangeSet = false;
        float min = std::numeric_limits<float>::lowest();
        float max = std::numeric_limits<float>::max();
        auto minFlag = PRM_RANGE_UI;
        auto maxFlag = PRM_RANGE_UI;

        auto uiMinString = input->GetUIMin();
        auto uiMinSoftString = input->GetUISoftMin();

        auto uiMaxString = input->GetUIMax();
        auto uiMaxSoftString = input->GetUISoftMax();

        if (uiMinString) {
            min = std::stof(uiMinString);
            minFlag = PRM_RANGE_RESTRICTED;
            isRangeSet = true;
        } else if (uiMinSoftString) {
            min = std::stof(uiMinSoftString);
            isRangeSet = true;
        }

        if (uiMaxString) {
            max = std::stof(uiMaxString);
            maxFlag = PRM_RANGE_RESTRICTED;
            isRangeSet = true;
        } else if (uiMaxSoftString) {
            max = std::stof(uiMaxSoftString);
            isRangeSet = true;
        }

        if (isRangeSet) {
            return new PRM_Range(minFlag, min, maxFlag, max);
        }
    }

    return nullptr;
}

static PRM_Default* NewPRMDefault(
    RprUsdMaterialNodeInput const* input,
    unsigned i_nb_defaults,
    PRM_ChoiceList** choiceListPtr) {
    auto valueStr = input->GetValueString();
    if (!valueStr) {
        return nullptr;
    }

    std::vector<float> values;

    if (input->GetType() == RprUsdMaterialNodeElement::kBoolean) {
        if (std::strcmp(valueStr, "true") == 0) {
            values.push_back(1.0f);
        } else if (std::strcmp(valueStr, "false") == 0) {
            values.push_back(0.0f);
        }
    } else if (input->GetType() == RprUsdMaterialNodeElement::kFloat ||
               input->GetType() == RprUsdMaterialNodeElement::kAngle ||
               input->GetType() == RprUsdMaterialNodeElement::kInteger ||
               input->GetType() == RprUsdMaterialNodeElement::kVector3 ||
               input->GetType() == RprUsdMaterialNodeElement::kVector2 ||
               input->GetType() == RprUsdMaterialNodeElement::kColor3 ||
               input->GetType() == RprUsdMaterialNodeElement::kNormal) {
        auto valueStrings = TfStringTokenize(valueStr, ", \t");
        if (valueStrings.size() == i_nb_defaults) {
            for (size_t i = 0; i < valueStrings.size(); ++i) {
                values.push_back(std::stof(valueStrings[i]));
            }
        }
    } else if (input->GetType() == RprUsdMaterialNodeElement::kToken) {
        auto defau1t = LEAKED(new PRM_Default(0, LEAKED(strdup(valueStr))));
        auto items = LEAKED(new std::vector<PRM_Item>);

        for (auto& value : input->GetTokenValues()) {
            items->push_back(PRM_Item(LEAKED(strdup(value.GetText()))));
        }
        items->push_back(PRM_Item());

        auto& choiceList = *choiceListPtr;
        auto choiceListType = PRM_ChoiceListType(PRM_CHOICELIST_SINGLE | PRM_CHOICELIST_USE_TOKEN);
        choiceList = LEAKED(new PRM_ChoiceList(choiceListType, items->data()));

        return defau1t;
    } else if (input->GetType() == RprUsdMaterialNodeElement::kString) {
        return LEAKED(new PRM_Default(0, LEAKED(strdup(valueStr))));
    }

    if (values.size() == i_nb_defaults) {
        auto def = new PRM_Default[i_nb_defaults];
        for (unsigned i = 0; i < i_nb_defaults; ++i) {
            def[i] = PRM_Default(values[i]);
        }
        return def;
    }

    return nullptr;
}

static unsigned GetNumChannels(RprUsdMaterialNodeInput const* input) {
    if (input->GetType() == RprUsdMaterialNodeElement::kColor3 ||
        input->GetType() == RprUsdMaterialNodeElement::kVector3 ||
        input->GetType() == RprUsdMaterialNodeElement::kNormal) {
        return 3;
    } else if (input->GetType() == RprUsdMaterialNodeElement::kVector2) {
        return 2;
    }

    return 1;
}

static std::vector<PRM_Template>* GetShaderTemplates(RprUsdMaterialNodeInfo const* shaderInfo) {
    /*
        The templates and their components (names and such) are dynamically
        allocated here but never deleted, since they're expected to be valid for
        the duration of the process. It's not that different from allocating
        them as static variables, which is the usual way to do this when only
        one type of operator is defined. Allocating them dynamically allows
        multiple operator types, each with an arbitrary number of parameters, to
        be created.
        It could be interesting to keep them around in VOP_RPRMaterialOperator
        and delete them when its destructor is called. Unfortunately, even
        VOP_Operator's destructor doesn't seem to even be called, so we simply
        use the LEAKED macro to mark them until we find out what we should do.
    */
    auto templates = LEAKED(new std::vector<PRM_Template>);

    // Scan inputs for UI folders, we create tab for each of them
    std::vector<std::string> tabNames;
    std::map<std::string, std::vector<RprUsdMaterialNodeInput const*>> inputsPerTab;
    for (size_t i = 0; i < shaderInfo->GetNumInputs(); ++i) {
        auto input = shaderInfo->GetInput(i);

        std::string uiFolder;
        if (input->GetUIFolder()) {
            uiFolder = input->GetUIFolder();
        }

        auto tabIt = inputsPerTab.find(uiFolder);
        if (tabIt == inputsPerTab.end()) {
            tabNames.push_back(uiFolder);
            inputsPerTab[uiFolder] = {input};
        } else {
            tabIt->second.push_back(input);
        }
    }

    auto tabs = LEAKED(new std::vector<PRM_Default>);
    for (auto& tabName : tabNames) {
        if (tabName.empty()) {
            continue;
        }
        auto inputsIt = inputsPerTab.find(tabName);
        auto numInputsInTab = static_cast<float>(inputsIt->second.size());
        tabs->push_back(PRM_Default(numInputsInTab, LEAKED(strdup(tabName.c_str()))));
    }

    if (!tabs->empty()) {
        static PRM_Name tabsName("tabs");
        templates->push_back(PRM_Template(PRM_SWITCHER, tabs->size(), &tabsName, tabs->data()));
    }

    for (auto& tabName : tabNames) {
        auto inputsIt = inputsPerTab.find(tabName);
        for (auto input : inputsIt->second) {
            auto numChannels = GetNumChannels(input);
            auto uiName = input->GetUIName();
            if (!uiName) {
                continue;
            }

            auto docString = input->GetDocString();
            const char* doc = docString ? LEAKED(strdup(docString)) : nullptr;

            auto name = LEAKED(new PRM_Name(PRM_Name::COPY, input->GetName(), uiName));
            auto& type = GetPRMType(input->GetType());
            auto range = LEAKED(NewPRMRange(input));
            PRM_ChoiceList* choiceList = nullptr;
            auto defau1t = LEAKED(NewPRMDefault(input, numChannels, &choiceList));

            PRM_ConditionalBase* conditionalBase = nullptr;
            PRM_SpareData* spareData = nullptr;
            PRM_Callback callback = nullptr;
            int paramGroup = 1;

            templates->push_back(PRM_Template(type, numChannels, name, defau1t, choiceList, range, callback, spareData, paramGroup, doc, conditionalBase));
        }
    }

    templates->push_back(PRM_Template());
    return templates;
}

PRM_Template* VOP_RPRMaterial::GetTemplates(RprUsdMaterialNodeInfo const* shaderInfo) {
    return &GetShaderTemplates(shaderInfo)->at(0);
}

VOP_RPRMaterial::VOP_RPRMaterial(OP_Network* parent, const char* name, OP_Operator* entry)
    : VOP_Node(parent, name, entry) {

    auto rpr_entry = dynamic_cast<VOP_RPRMaterialOperator*>(entry);
    m_shaderInfo = rpr_entry->shaderInfo;

    for (size_t i = 0; i < m_shaderInfo->GetNumOutputs(); ++i) {
        auto output = m_shaderInfo->GetOutput(i);

        if (output->GetType() == RprUsdMaterialNodeElement::kVolumeShader ||
            output->GetType() == RprUsdMaterialNodeElement::kDisplacementShader ||
            output->GetType() == RprUsdMaterialNodeElement::kSurfaceShader) {

            if (output->GetType() == RprUsdMaterialNodeElement::kVolumeShader) {
                m_shaderType = VOP_ATMOSPHERE_SHADER;
            } else if (output->GetType() == RprUsdMaterialNodeElement::kSurfaceShader) {
                m_shaderType = VOP_SURFACE_SHADER;
            } else if (output->GetType() == RprUsdMaterialNodeElement::kDisplacementShader) {
                m_shaderType = VOP_DISPLACEMENT_SHADER;
            }

            setMaterialFlag(true);
            break;
        }
    }
}

const char* VOP_RPRMaterial::inputLabel(unsigned i_idx) const {
    return m_shaderInfo->GetInput(i_idx)->GetName();
}

const char* VOP_RPRMaterial::outputLabel(unsigned i_idx) const {
    return m_shaderInfo->GetOutput(i_idx)->GetName();
}

unsigned VOP_RPRMaterial::minInputs() const {
    return 0;
}

unsigned VOP_RPRMaterial::getNumVisibleInputs() const {
    return m_shaderInfo->GetNumInputs();
}

unsigned VOP_RPRMaterial::orderedInputs() const {
    return m_shaderInfo->GetNumInputs();
}

#if HDK_API_VERSION >= 18000000

UT_StringHolder VOP_RPRMaterial::getShaderName(
    VOP_ShaderNameStyle style,
    VOP_Type shader_type) const {
    /* This name is what becomes the shader id in USD land. Our Hydra plugin
       will use it to find the correct shader. */
    if (style == VOP_ShaderNameStyle::PLAIN) {
        return m_shaderInfo->GetName();
    }

    return VOP_Node::getShaderName(style, shader_type);
}

VOP_Type VOP_RPRMaterial::getShaderType() const {
    return m_shaderType;
}

#endif

void VOP_RPRMaterial::getInputNameSubclass(UT_String &in, int i_idx) const {
    in = inputLabel(i_idx);
}

int VOP_RPRMaterial::getInputFromNameSubclass(const UT_String &in) const {
    for (size_t idx = 0; idx < m_shaderInfo->GetNumInputs(); ++idx) {
        if (m_shaderInfo->GetInput(idx)->GetName() == in) {
            return static_cast<int>(idx);
        }
    }
    return -1;
}

void VOP_RPRMaterial::getInputTypeInfoSubclass(
    VOP_TypeInfo &o_type_info,
    int i_idx) {
    o_type_info.setType(GetVOPType(m_shaderInfo->GetInput(i_idx)->GetType()));
}

void VOP_RPRMaterial::getAllowedInputTypeInfosSubclass(
    unsigned i_idx,
    VOP_VopTypeInfoArray &o_type_infos) {
    VOP_TypeInfo info;
    getInputTypeInfoSubclass(info, i_idx);
    o_type_infos.append(info);
}

void VOP_RPRMaterial::getOutputNameSubclass(UT_String &out, int i_idx) const {
    out = outputLabel(i_idx);
}

void VOP_RPRMaterial::getOutputTypeInfoSubclass(
    VOP_TypeInfo &o_type_info,
    int i_idx) {
    o_type_info.setType(GetVOPType(m_shaderInfo->GetOutput(i_idx)->GetType()));
}

VOP_MaterialX::VOP_MaterialX(OP_Network* parent, const char* name, OP_Operator* entry)
    : VOP_RPRMaterial(parent, name, entry) {

}

PRM_Template* VOP_MaterialX::GetTemplates(RprUsdMaterialNodeInfo const* shaderInfo) {
    auto templates = GetShaderTemplates(shaderInfo);

    for (auto& prm : *templates) {
        if (prm.getType() == PRM_STRING_E) {
            if (std::strcmp("surfaceElement", prm.getToken()) == 0 ||
                std::strcmp("displacementElement", prm.getToken()) == 0) {
                auto choiceListType = PRM_ChoiceListType(PRM_CHOICELIST_SINGLE | PRM_CHOICELIST_USE_TOKEN);
                auto choiceList = LEAKED(new PRM_ChoiceList(choiceListType, &VOP_MaterialX::ElementChoiceGenFunc));
                prm.setChoiceListPtr(choiceList);
            }
        }
    }

    templates->pop_back();

    // Add error message parm.
    // It will be displayed if the selected mtlx have no renderable elements
    //
    auto msgName = LEAKED(new PRM_Name("msg"));
    auto msgDefault = LEAKED(new PRM_Default(0.0, "No renderable elements"));
    templates->emplace_back(PRM_LABEL, PRM_TYPE_NO_LABEL, 1, msgName, msgDefault);

    // Reload button

    // Currently, the only way to reload a material in Houdini is to modify the material node itself.
    // So we add dummy parameter that will be changed on reload button press
    //
    static const char* dummyParmName = "reloadDummy";
    templates->emplace_back(PRM_INT_E, 1, LEAKED(new PRM_Name(dummyParmName)));
    templates->back().setInvisible(true);

    // Add reload button parm
    //
    auto separatorName = LEAKED(new PRM_Name("reloadSeparator"));
    templates->emplace_back(PRM_SEPARATOR, 1, separatorName);
    auto buttonName = LEAKED(new PRM_Name("reload", "Reload"));
    PRM_Callback buttonCallback([](void* data, int, float time, const PRM_Template*) -> int {
        auto vop = static_cast<VOP_MaterialX*>(CAST_VOPNODE((OP_Node*)data));
        if (vop->m_file.isstring()) {
            if (auto reloadDummy = vop->getParmPtr(dummyParmName)) {
                double modificationTime;
                if (ArchGetModificationTime(vop->m_file, &modificationTime)) {
                    if (vop->m_fileModificationTime != modificationTime) {

                        // Update UI
                        //
                        vop->opChanged(OP_PARM_CHANGED, (void*)vop->getParmIndex("file"));

                        // Force Hydra material reload
                        //
                        reloadDummy->setValue(time, ++vop->m_reloadDummy);

                        return 1;
                    }
                }
            }
        }

        return 0;
    });
    templates->emplace_back(PRM_CALLBACK_NOREFRESH, 1, buttonName, nullptr, nullptr, nullptr, buttonCallback);

    templates->emplace_back();

    return &templates->at(0);
}

void VOP_MaterialX::ElementChoiceGenFunc(
    void* op,
    PRM_Name* choices, int maxChoicesSize,
    const PRM_SpareData* spare, const PRM_Parm* parm) {
    if (maxChoicesSize <= 0) {
        return;
    }

    auto parmToken = parm->getToken();
    RPRMtlxLoader::OutputType outputType;
    if (std::strcmp("surfaceElement", parmToken) == 0) {
        outputType = RPRMtlxLoader::kOutputSurface;
    } else if (std::strcmp("displacementElement", parmToken) == 0) {
        outputType = RPRMtlxLoader::kOutputDisplacement;
    } else {
        return;
    }

    auto vop = static_cast<VOP_MaterialX*>(CAST_VOPNODE((OP_Node*)op));
    auto renderElementPaths = vop->m_renderableElements.namePaths[outputType];
    size_t choiceCount = size_t(maxChoicesSize - 1); // -1 for sentinel PRM_Name
    size_t iChoice = 0;
    if (!renderElementPaths.empty()) {
        size_t maxNumElementPaths = choiceCount - 1; // -1 for None choice
        size_t namePathCount = std::min(maxNumElementPaths, renderElementPaths.size());
        for (; iChoice < namePathCount; ++iChoice) {
            auto path = renderElementPaths[iChoice].c_str();
            choices[iChoice].setToken(path);
            choices[iChoice].setLabel(path);
        }
    }

    if (iChoice < choiceCount) {
        choices[iChoice].setToken("");
        choices[iChoice].setLabel("None");
        ++iChoice;
    }

    choices[iChoice] = PRM_Name();
}

void VOP_MaterialX::opChanged(OP_EventType reason, void* data) {
    VOP_RPRMaterial::opChanged(reason, data);

    if (reason == OP_PARM_CHANGED) {
        int parmIndex = int(reinterpret_cast<intptr_t>(data));
        auto& changedParm = getParm(parmIndex);
        if (std::strcmp(changedParm.getToken(), "file") == 0) {
            bool keepSelections = false;

            UT_String newFile;
            double newFileModificationTime;
            changedParm.getValue(0.0, newFile, 0, true, 0);
            if (!ArchGetModificationTime(newFile, &newFileModificationTime)) {
                newFileModificationTime = 0.0;
            }

            // Keep selected renderable elements if file was edited
            //
            if (m_file == newFile) {
                if (m_fileModificationTime != newFileModificationTime &&
                    m_fileModificationTime != 0.0) {
                    keepSelections = true;
                }
            } else {
                m_file = std::move(newFile);
            }
            m_fileModificationTime = newFileModificationTime;

            // Rebuild renderable elements cache
            //
            m_renderableElements = {};
            if (auto mtlxLoader = RprUsdMaterialRegistry::GetInstance().GetMtlxLoader()) {
                try {
                    auto mtlxDoc = MaterialX::createDocument();
                    MaterialX::readFromXmlFile(mtlxDoc, m_file.toStdString());
                    mtlxDoc->importLibrary(mtlxLoader->GetStdlib());
                    m_renderableElements = mtlxLoader->GetRenderableElements(mtlxDoc.get());
                } catch (MaterialX::Exception& e) {
                    // no-op
                }
            }

            // Hide everything but file parm when a file is not specified
            //
            bool isUiVisible = bool(m_file);

            // Reset element parameters if previously selected ones not valid
            //
            bool hasAnyElements = false;
            const char* parmNames[RPRMtlxLoader::kOutputsTotal] = {
                "surfaceElement", "displacementElement"
            };
            for (int i = 0; i < RPRMtlxLoader::kOutputsTotal; ++i) {
                if (auto parm = getParmPtr(parmNames[i])) {
                    auto& namePaths = m_renderableElements.namePaths[i];
                    bool parmVisible = false;
                    if (!namePaths.empty()) {
                        parmVisible = true;
                        hasAnyElements = true;
                    }
                    parm->setVisibleState(isUiVisible && parmVisible);

                    if (keepSelections) {
                        UT_String prevNamePath;
                        parm->getValue(0.0, prevNamePath, 0, true, 0);

                        // Keep renderable element disabled
                        //
                        if (!prevNamePath.isstring()) {
                            continue;
                        }

                        // Or if this renderable element is still available
                        //
                        auto namePathIt = std::find_if(namePaths.begin(), namePaths.end(),
                            [&prevNamePath](std::string const& namePath) {
                                return prevNamePath == namePath.c_str();
                            }
                        );
                        if (namePathIt != namePaths.end()) {
                            continue;
                        }
                    }

                    parm->setValue(0.0, namePaths.empty() ? "" : namePaths[0].c_str(), CH_STRING_LITERAL);
                }
            }

            if (auto msgParm = getParmPtr("msg")) {
                msgParm->setVisibleState(isUiVisible && !hasAnyElements);
            }

            if (auto buttonParm = getParmPtr("reload")) {
                buttonParm->setVisibleState(isUiVisible);
                if (auto separatorParm = getParmPtr("reloadSeparator")) {
                    separatorParm->setVisibleState(isUiVisible);
                }
            }
        }
    }
}

bool VOP_MaterialX::runCreateScript() {
    int numParms = getNumParms();
    for (int i = getParmIndex("stPrimvarName") + 1; i < numParms; ++i) {
        getParm(i).setVisibleState(false);
    }

    return true;
}

static const char* GetUIName(RprUsdMaterialNodeInfo const* shaderInfo) {
    if (shaderInfo->GetUIName()) return shaderInfo->GetUIName();

    // If UI name is not given, try to use name
    std::string name = shaderInfo->GetName();

    std::string rprPrefix("rpr_");
    auto rprPos = name.find(rprPrefix);
    if (TfStringStartsWith(name, rprPrefix)) {
        for (size_t i = 0; i < rprPrefix.size() - 1; ++i) {
            name[rprPos + i] = std::toupper(name[rprPos + i]);
        }
    }

    bool isWord = false;
    for (size_t i = 0; i < name.size(); ++i) {
        if (std::isalpha(name[i])) {
            if (!isWord) {
                isWord = true;
                name[i] = std::toupper(name[i]);
            }
        } else {
            isWord = false;
        }

        if (strchr("_", name[i])) {
            name[i] = ' ';
        }
    }

    return LEAKED(strdup(name.c_str()));
}

VOP_RPRMaterialOperator* VOP_RPRMaterialOperator::Create(RprUsdMaterialNodeInfo const* shaderInfo) {
    if (shaderInfo->GetName()) {
        if (std::strcmp("rpr_materialx_node", shaderInfo->GetName()) == 0) {
            return VOP_RPRMaterialOperator::_Create<VOP_MaterialX>(shaderInfo);
        }
    }

    return VOP_RPRMaterialOperator::_Create<VOP_RPRMaterial>(shaderInfo);
}

template <typename VOP>
VOP_RPRMaterialOperator* VOP_RPRMaterialOperator::_Create(RprUsdMaterialNodeInfo const* shaderInfo) {
    auto templates = VOP::GetTemplates(shaderInfo);
    auto construct = [](OP_Network* parent, const char* name, OP_Operator* entry) -> OP_Node* {
        return new VOP(parent, name, entry);
    };
    return new VOP_RPRMaterialOperator(shaderInfo, construct, templates);
}

VOP_RPRMaterialOperator::VOP_RPRMaterialOperator(
    RprUsdMaterialNodeInfo const* shaderInfo,
    OP_Constructor construct,
    PRM_Template* templates)
    : VOP_Operator(
        TfStringPrintf("RPR::%s", shaderInfo->GetName()).c_str(),
        GetUIName(shaderInfo),
        construct,
        templates,
        VOP_Node::theChildTableName,
        shaderInfo->GetNumInputs(), shaderInfo->GetNumInputs(),
        // Put rpr here so Houdini's Material Builder won't see our VOPs
        "rpr",
        nullptr,
        OP_FLAG_OUTPUT,
        shaderInfo->GetNumOutputs())
    , shaderInfo(shaderInfo) {

    std::string subMenuPath("RPR");
    if (shaderInfo->GetUIFolder()) {
        subMenuPath += "/";
        subMenuPath += shaderInfo->GetUIFolder();
    }
    setOpTabSubMenuPath(subMenuPath.c_str());

    setIconName("RPR");

    /*
        The RenderMask is what ends up being the MaterialNetworkSelector in
        Hydra. If we don't set it, the default translator will not provide
        networks at all. And if it does not match the Hydra plugin, we won't
        see the networks there.
    */
    auto vop_info = static_cast<VOP_OperatorInfo*>(getOpSpecificData());
    vop_info->setRenderMask("rpr");
}

PXR_NAMESPACE_CLOSE_SCOPE
