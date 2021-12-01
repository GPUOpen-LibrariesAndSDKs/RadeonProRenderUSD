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

#include "LOP_RPRRendererSettings.h"

#include <OP/OP_OperatorTable.h>
#include <OP/OP_Operator.h>

#include <PRM/PRM_Include.h>
#include <PRM/PRM_SpareData.h>

#include <pxr/base/tf/staticTokens.h>
#include <pxr/usd/usd/schemaRegistry.h>
#include <pxr/usd/usd/primDefinition.h>
#include <pxr/imaging/rprUsd/tokens.h>

/**
    Simple identity macro that does nothing but helps to identify allocations of
    memory that will never be deleted, until we find a way to do so.
*/
#define LEAKED(ptr) ptr

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(_tokens,
    (RprRendererSettingsAPI)
);

namespace {

PRM_Template GetPRMFromProp(UsdPrimDefinition const* settingsPrimDef, TfToken const& propertyName) {
    SdfPropertySpecHandle propertySpec = settingsPrimDef->GetSchemaPropertySpec(propertyName);

    std::string prmName = propertyName.GetString();
    std::replace(prmName.begin(), prmName.end(), ':', '_');

    std::string displayName = propertySpec->GetDisplayName();

    auto name = LEAKED(new PRM_Name(PRM_Name::COPY, prmName.c_str(), displayName.c_str()));

    std::string docString = propertySpec->GetDocumentation();
    const char* doc = !docString.empty() ? LEAKED(strdup(docString.c_str())) : nullptr;

    PRM_Range* range = nullptr;
    {
        float min = std::numeric_limits<float>::lowest();
        float max = std::numeric_limits<float>::max();
        auto minFlag = PRM_RANGE_FREE;
        auto maxFlag = PRM_RANGE_FREE;

        if (settingsPrimDef->GetPropertyMetadata(propertyName, RprUsdTokens->rprMinValue, &min)) {
            minFlag = PRM_RANGE_RESTRICTED;
        }
        if (settingsPrimDef->GetPropertyMetadata(propertyName, RprUsdTokens->rprMaxValue, &max)) {
            maxFlag = PRM_RANGE_RESTRICTED;
        }
        if (minFlag != PRM_RANGE_FREE || maxFlag != PRM_RANGE_FREE) {
            range = LEAKED(new PRM_Range(minFlag, min, maxFlag, max));
        }
    }

    int numChannels = 1;
    PRM_Default* defau1t = nullptr;
    PRM_ChoiceList* choiceList = nullptr;
    PRM_Type const* type = &PRM_LIST_TERMINATOR;
    {
        VtValue defaultValue = propertySpec->GetDefaultValue();
        if (defaultValue.IsHolding<bool>()) {
            type = &PRM_TOGGLE;
            defau1t = new PRM_Default(float(defaultValue.UncheckedGet<bool>()));
        } else if (defaultValue.IsHolding<int>()) {
            type = &PRM_INT;
            defau1t = new PRM_Default(float(defaultValue.UncheckedGet<int>()));
        } else if (defaultValue.IsHolding<float>()) {
            type = &PRM_FLT;
            defau1t = new PRM_Default(defaultValue.UncheckedGet<float>());
        } else if (defaultValue.IsHolding<GfVec3f>()) {
            GfVec3f vec3 = defaultValue.UncheckedGet<GfVec3f>();

            if (TfStringStartsWith(propertySpec->GetTypeName().GetType().GetTypeName(), "color")) {
                type = &PRM_RGB;
            } else {
                type = &PRM_XYZ;
            }
            numChannels = 3;
            defau1t = new PRM_Default[numChannels];
            for (int i = 0; i < numChannels; ++i) {
                defau1t[i] = PRM_Default(vec3[i]);
            }
        } else if (defaultValue.IsHolding<std::string>()) {
            std::string string = defaultValue.UncheckedGet<std::string>();

            type = &PRM_STRING_E;
            defau1t = LEAKED(new PRM_Default(0, LEAKED(strdup(string.c_str()))));
        } else if (defaultValue.IsHolding<SdfAssetPath>()) {
            type = &PRM_FILE;
            defau1t = LEAKED(new PRM_Default(0, ""));
        } else if (defaultValue.IsHolding<TfToken>()) {
            VtTokenArray allowedTokens;
            if (!settingsPrimDef->GetPropertyMetadata(propertyName, SdfFieldKeys->AllowedTokens, &allowedTokens)) {
                TF_RUNTIME_ERROR("Token property \"%s\" has no allowed tokens metadata", propertyName.GetText());
                return PRM_Template();
            }

            TfToken token = defaultValue.UncheckedGet<TfToken>();

            type = &PRM_ORD_E;
            defau1t = LEAKED(new PRM_Default(0, LEAKED(strdup(token.GetText()))));

            auto items = LEAKED(new std::vector<PRM_Item>);
            for (auto& value : allowedTokens) {
                items->push_back(PRM_Item(LEAKED(strdup(value.GetText()))));
            }
            items->push_back(PRM_Item());

            auto choiceListType = PRM_ChoiceListType(PRM_CHOICELIST_SINGLE | PRM_CHOICELIST_USE_TOKEN);
            choiceList = LEAKED(new PRM_ChoiceList(choiceListType, items->data()));
        }
    }

    PRM_ConditionalBase* conditionalBase = nullptr;
    PRM_SpareData* spareData = nullptr;
    PRM_Callback callback = nullptr;
    int paramGroup = 1;

    return PRM_Template(*type, numChannels, name, defau1t, choiceList, range, callback, spareData, paramGroup, doc, conditionalBase);
}

std::vector<PRM_Template>* GetTemplates(UsdPrimDefinition const* settingsPrimDef) {
    /*
        The templates and their components (names and such) are dynamically
        allocated here but never deleted, since they're expected to be valid for
        the duration of the process. It's not that different from allocating
        them as static variables, which is the usual way to do this when only
        one type of operator is defined. Allocating them dynamically allows
        multiple operator types, each with an arbitrary number of parameters, to
        be created.
    */
    auto templates = LEAKED(new std::vector<PRM_Template>);

    struct UINode {
        virtual ~UINode() = default;
        virtual UINode* GetChild(std::string const& name) { return nullptr; }
        virtual void AddChild(std::unique_ptr<UINode> child) {}
        virtual bool IsNamedAs(std::string const& name) = 0;

        struct TemplateGroup {
            std::string name;
            size_t numGroups = 0;
            std::vector<PRM_Template> templates;
        };
        virtual TemplateGroup Compile(UsdPrimDefinition const* settingsPrimDef, int depth) = 0;
    };
    struct UINodeInterim : public UINode {
        ~UINodeInterim() override = default;
        std::string name;
        std::vector<std::unique_ptr<UINode>> children;

        UINode* GetChild(std::string const& name) override {
            auto childIt = std::find_if(children.begin(), children.end(),
                [&](auto const& child) { return child->IsNamedAs(name); }
            );
            return childIt == children.end() ? nullptr : childIt->get();
        }
        void AddChild(std::unique_ptr<UINode> child) override { children.push_back(std::move(child)); }
        bool IsNamedAs(std::string const& name) override { return this->name == name; }
        TemplateGroup Compile(UsdPrimDefinition const* settingsPrimDef, int depth) override {
            std::string padding(depth * 2, ' ');
            printf("%s- \"%s\" {\n", padding.c_str(), name.c_str());

            bool isRoot = name.empty();

            std::vector<TemplateGroup> childGroups;
            for (auto& child : children) {
                TemplateGroup childGroup = child->Compile(settingsPrimDef, depth + 1);
                if (childGroup.templates.empty() ||
                    isRoot && childGroup.name.empty()) {
                    continue;
                }
                childGroups.push_back(std::move(childGroup));
            }

            TemplateGroup ret;

            if (isRoot) {
                auto tabs = LEAKED(new std::vector<PRM_Default>);
                for (auto& group : childGroups) {
                    tabs->push_back(PRM_Default(group.numGroups, LEAKED(strdup(group.name.c_str()))));
                }
                static PRM_Name tabsName("tabs");
                ret.numGroups = childGroups.size();
                ret.templates.push_back(PRM_Template(PRM_SWITCHER, tabs->size(), &tabsName, tabs->data()));
                for (auto& group : childGroups) {
                    ret.templates.insert(ret.templates.end(), group.templates.begin(), group.templates.end());
                }
            } else {
                ret.name = name;
                ret.numGroups = childGroups.size();
                for (auto& group : childGroups) {
                    ret.templates.insert(ret.templates.end(), group.templates.begin(), group.templates.end());
                }

                if (depth > 1) {
                    char* nameLabel = LEAKED(strdup(name.c_str()));
                    char* nameToken = nameLabel;

                    for (size_t i = 0; i < name.size(); ++i) {
                        if (nameToken[i] == ' ') {
                            if (nameToken == nameLabel) {
                                nameToken = LEAKED(strdup(name.c_str()));
                            }
                            nameToken[i] = '_';
                        }
                    }

                    PRM_Name* groupName = LEAKED(new PRM_Name(nameToken));
                    auto group = LEAKED(new PRM_Default(childGroups.size(), nameLabel));

                    ret.templates.insert(ret.templates.begin(), PRM_Template(PRM_SWITCHER, 1, groupName, group, 0, 0, 0, &PRM_SpareData::groupTypeCollapsible));

                    printf("%s  * \"%s\": %zu\n", padding.c_str(), nameLabel, childGroups.size());
                }
            }

            printf("%s}\n", padding.c_str());

            return ret;
        }
    };
    struct UINodeProperty : public UINode {
        ~UINodeProperty() override = default;

        TfToken propertyName;
        UINodeProperty(TfToken propertyName) : propertyName(propertyName) {}
        bool IsNamedAs(std::string const& name) override { return propertyName == name; }
        TemplateGroup Compile(UsdPrimDefinition const* settingsPrimDef, int depth) override {
            std::string padding(depth * 2, ' ');
            printf("%s- \"%s\"\n", padding.c_str(), propertyName.GetText());

            auto prmTemplate = GetPRMFromProp(settingsPrimDef, propertyName);
            if (prmTemplate.getType() == PRM_LIST_TERMINATOR) {
                return {};
            }
            TemplateGroup ret;
            ret.templates = {prmTemplate};
            ret.numGroups = 1;
            return ret;
        }
    };

    UINodeInterim uiTree;
    for (auto propertyName : settingsPrimDef->GetPropertyNames()) {
        int rprHidden = 0;
        if (settingsPrimDef->GetPropertyMetadata(propertyName, RprUsdTokens->rprHidden, &rprHidden) && rprHidden) {
            continue;
        }

        std::string displayGroup;
        settingsPrimDef->GetPropertyMetadata(propertyName, SdfFieldKeys->DisplayGroup, &displayGroup);

        auto uiPath = TfStringTokenize(displayGroup, "|");

        UINode* parentNode = &uiTree;
        for (auto& pathElement : uiPath) {
            auto childNode = parentNode->GetChild(pathElement);
            if (!childNode) {
                auto newInterimNode = std::make_unique<UINodeInterim>();
                newInterimNode->name = pathElement;
                childNode = newInterimNode.get();
                parentNode->AddChild(std::move(newInterimNode));
            }

            parentNode = childNode;
        }

        parentNode->AddChild(std::make_unique<UINodeProperty>(propertyName));
    }

    *templates = uiTree.Compile(settingsPrimDef, 0).templates;

    templates->push_back(PRM_Template());
    return templates;
}

} // namespace anonymous

void LOP_RPRRendererSettings::Register(OP_OperatorTable* table) {
    // Disabled until the following TODOs resolved
    // TODO: fix properties ordering - generatedSchema loses properties order from original schema
    // TODO: renderQuality default value is not correct - for some reason, PRM_Default is ignored
    // TODO: expand the Denoise and the Tonemapping groups by default
    // TODO: Render mode property has default channel
    return;

    UsdPrimDefinition const* settingsPrimDef = UsdSchemaRegistry::GetInstance().FindAppliedAPIPrimDefinition(_tokens->RprRendererSettingsAPI);
    if (!settingsPrimDef) {
        fprintf(stderr, "Failed to register LOP_RPRRendererSettings: could not find RprRendererSettingsAPI\n");
        return;
    }

    auto opOperator = new OP_Operator(
        "rpr_lop_rendererSettings",
        "RPR Render Settings",
        [](OP_Network *net, const char *name, OP_Operator *op) -> OP_Node* {
            return new LOP_RPRRendererSettings(net, name, op);
        },
        &GetTemplates(settingsPrimDef)->at(0),
        0u,
        (unsigned)1);
    opOperator->setIconName("RPR");

    table->addOperator(opOperator);
}

LOP_RPRRendererSettings::LOP_RPRRendererSettings(OP_Network *net, const char *name, OP_Operator *op)
    : LOP_Node(net, name, op) {

}

OP_ERROR LOP_RPRRendererSettings::cookMyLop(OP_Context &context) {
    // TODO: set up UsdRenderSettings node
    return error();
}

PXR_NAMESPACE_CLOSE_SCOPE
