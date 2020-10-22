# Copyright 2020 Advanced Micro Devices, Inc
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#     http://www.apache.org/licenses/LICENSE-2.0
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
# 
import os
import sys
import argparse
import platform

from houdiniDsGenerator import generate_houdini_ds
from commonSettings import SettingValue

def get_render_setting(render_setting_categories, category_name, name):
    for category in render_setting_categories:
        if category['name'] != category_name:
            continue

        for setting in category['settings']:
            if setting['name'] == name:
                return setting

def hidewhen_render_quality(operator, quality, render_setting_categories=None):
    if operator in ('==', '!='):
        return 'renderQuality {} "{}"'.format(operator, quality)
    elif operator == '<':
        render_quality = get_render_setting(render_setting_categories, 'RenderQuality', 'renderQuality')
        values = render_quality['values']

        hidewhen = []
        for value in values:
            if value == quality:
                break
            hidewhen.append(hidewhen_render_quality('==', value.get_key()))

        return hidewhen
    else:
        raise ValueError('Operator "{}" not implemented'.format(operator))

def hidewhen_hybrid(render_setting_categories):
    return hidewhen_render_quality('<', 'Full', render_setting_categories)

def hidewhen_not_northstar(render_setting_categories):
    return hidewhen_render_quality('!=', 'Northstar', render_setting_categories)

HYBRID_DISABLED_PLATFORM = 'Darwin'

render_setting_categories = [
    {
        'name': 'RenderQuality',
        'settings': [
            {
                'name': 'renderQuality',
                'ui_name': 'Render Quality',
                'help': 'Render restart might be required',
                'defaultValue': 'Full',
                'values': [
                    SettingValue('Low', disabled_platform=HYBRID_DISABLED_PLATFORM),
                    SettingValue('Medium', disabled_platform=HYBRID_DISABLED_PLATFORM),
                    SettingValue('High', disabled_platform=HYBRID_DISABLED_PLATFORM),
                    SettingValue('Full'),
                    SettingValue('Northstar', 'Full 2.0 (Beta)')
                ]
            }
        ]
    },
    {
        'name': 'RenderMode',
        'settings': [
            {
                'name': 'renderMode',
                'ui_name': 'Render Mode',
                'defaultValue': 'Global Illumination',
                'values': [
                    SettingValue('Global Illumination'),
                    SettingValue('Direct Illumination'),
                    SettingValue('Wireframe'),
                    SettingValue('Material Index'),
                    SettingValue('Position'),
                    SettingValue('Normal'),
                    SettingValue('Texcoord'),
                    SettingValue('Ambient Occlusion'),
                    SettingValue('Diffuse')
                ]
            },
            {
                'name': 'aoRadius',
                'ui_name': 'Ambient Occlusion Radius',
                'defaultValue': 1.0,
                'minValue': 0.0,
                'maxValue': 100.0,
                'houdini': {
                    'hidewhen': 'renderMode != "AmbientOcclusion"'
                }
            }
        ],
        'houdini': {
            'hidewhen': hidewhen_hybrid
        }
    },
    {
        'name': 'Device',
        'houdini': {
            'hidewhen': hidewhen_hybrid
        },
        'settings': [
            {
                'name': 'renderDevice',
                'ui_name': 'Render Device',
                'help': 'Restart required.',
                'defaultValue': 'GPU',
                'values': [
                    SettingValue('CPU'),
                    SettingValue('GPU'),
                    # SettingValue('CPU+GPU')
                ]
            }
        ]
    },
    {
        'name': 'Denoise',
        'houdini': {
            'hidewhen': lambda settings: hidewhen_render_quality('<', 'High', settings)
        },
        'settings': [
            {
                'name': 'enableDenoising',
                'ui_name': 'Enable Denoising',
                'defaultValue': False,
                'houdini': {
                    'custom_tags': [
                        '"uiicon" VIEW_display_denoise'
                    ]
                }
            }
        ]
    },
    {
        'name': 'Sampling',
        'houdini': {
            'hidewhen': lambda settings: hidewhen_render_quality('==', 'Low', settings)
        },
        'settings': [
            {
                'name': 'maxSamples',
                'ui_name': 'Max Pixel Samples',
                'help': 'Maximum number of samples to render for each pixel.',
                'defaultValue': 256,
                'minValue': 1,
                'maxValue': 2 ** 16
            }
        ]
    },
    {
        'name': 'AdaptiveSampling',
        'houdini': {
            'hidewhen': hidewhen_hybrid
        },
        'settings': [
            {
                'name': 'minAdaptiveSamples',
                'ui_name': 'Min Pixel Samples',
                'help': 'Minimum number of samples to render for each pixel. After this, adaptive sampling will stop sampling pixels where noise is less than \'Variance Threshold\'.',
                'defaultValue': 64,
                'minValue': 1,
                'maxValue': 2 ** 16
            },
            {
                'name': 'varianceThreshold',
                'ui_name': 'Variance Threshold',
                'help': 'Cutoff for adaptive sampling. Once pixels are below this amount of noise, no more samples are added. Set to 0 for no cutoff.',
                'defaultValue': 0.0,
                'minValue': 0.0,
                'maxValue': 1.0
            }
        ]
    },
    {
        'name': 'Quality',
        'houdini': {
            'hidewhen': hidewhen_hybrid
        },
        'settings': [
            {
                'name': 'maxRayDepth',
                'ui_name': 'Max Ray Depth',
                'help': 'The number of times that a ray bounces off various surfaces before being terminated.',
                'defaultValue': 8,
                'minValue': 1,
                'maxValue': 50
            },
            {
                'name': 'maxRayDepthDiffuse',
                'ui_name': 'Diffuse Ray Depth',
                'help': 'The maximum number of times that a light ray can be bounced off diffuse surfaces.',
                'defaultValue': 3,
                'minValue': 0,
                'maxValue': 50
            },
            {
                'name': 'maxRayDepthGlossy',
                'ui_name': 'Glossy Ray Depth',
                'help': 'The maximum number of ray bounces from specular surfaces.',
                'defaultValue': 3,
                'minValue': 0,
                'maxValue': 50
            },
            {
                'name': 'maxRayDepthRefraction',
                'ui_name': 'Refraction Ray Depth',
                'help': 'The maximum number of times that a light ray can be refracted, and is designated for clear transparent materials, such as glass.',
                'defaultValue': 3,
                'minValue': 0,
                'maxValue': 50
            },
            {
                'name': 'maxRayDepthGlossyRefraction',
                'ui_name': 'Glossy Refraction Ray Depth',
                'help': 'The Glossy Refraction Ray Depth parameter is similar to the Refraction Ray Depth. The difference is that it is aimed to work with matte refractive materials, such as semi-frosted glass.',
                'defaultValue': 3,
                'minValue': 0,
                'maxValue': 50
            },
            {
                'name': 'maxRayDepthShadow',
                'ui_name': 'Shadow Ray Depth',
                'help': 'Controls the accuracy of shadows cast by transparent objects. It defines the maximum number of surfaces that a light ray can encounter on its way causing these surfaces to cast shadows.',
                'defaultValue': 2,
                'minValue': 0,
                'maxValue': 50
            },
            {
                'name': 'raycastEpsilon',
                'ui_name': 'Ray Cast Epsilon',
                'help': 'Determines an offset used to move light rays away from the geometry for ray-surface intersection calculations.',
                'defaultValue': 2e-5,
                'minValue': 1e-6,
                'maxValue': 1.0
            },
            {
                'name': 'enableRadianceClamping',
                'ui_name': 'Enable Clamp Radiance',
                'defaultValue': False,
            },
            {
                'name': 'radianceClamping',
                'ui_name': 'Clamp Radiance',
                'help': 'Limits the intensity, or the maximum brightness, of samples in the scene. Greater clamp radiance values produce more brightness.',
                'defaultValue': 0.0,
                'minValue': 0.0,
                'maxValue': 1e6
            },
            {
                'name': 'interactiveMaxRayDepth',
                'ui_name': 'Interactive Max Ray Depth',
                'help': 'Controls value of \'Max Ray Depth\' in interactive mode.',
                'defaultValue': 2,
                'minValue': 1,
                'maxValue': 50
            }
        ]
    },
    {
        'name': 'Tonemapping',
        'settings': [
            {
                'name': 'enableTonemap',
                'ui_name': 'Enable Tone Mapping',
                'help': 'Enable linear photographic tone mapping filter. More info in RIF documentation',
                'defaultValue': False
            },
            {
                'name': 'tonemapExposureTime',
                'ui_name': 'Tone Mapping Exposure Time',
                'help': 'Film exposure time',
                'defaultValue': 0.125,
                'minValue': 0.0,
                'maxValue': 10.0,
                'houdini': {
                    'hidewhen': 'enableTonemap == 0'
                }
            },
            {
                'name': 'tonemapSensitivity',
                'ui_name': 'Tone Mapping Sensitivity',
                'help': 'Luminance of the scene (in candela per m^2)',
                'defaultValue': 1.0,
                'minValue': 0.0,
                'maxValue': 10.0,
                'houdini': {
                    'hidewhen': 'enableTonemap == 0'
                }
            },
            {
                'name': 'tonemapFstop',
                'ui_name': 'Tone Mapping Fstop',
                'help': 'Aperture f-number',
                'defaultValue': 1.0,
                'minValue': 0.0,
                'maxValue': 100.0,
                'houdini': {
                    'hidewhen': 'enableTonemap == 0'
                }
            },
            {
                'name': 'tonemapGamma',
                'ui_name': 'Tone Mapping Gamma',
                'help': 'Gamma correction value',
                'defaultValue': 1.0,
                'minValue': 0.0,
                'maxValue': 5.0,
                'houdini': {
                    'hidewhen': 'enableTonemap == 0'
                }
            }
        ]
    },
    {
        'name': 'Alpha',
        'settings': [
            {
                'name': 'enableAlpha',
                'ui_name': 'Enable Color Alpha',
                'defaultValue': True,
                'houdini': {
                    'hidewhen': hidewhen_hybrid
                }
            }
        ]
    },
    {
        'name': 'MotionBlur',
        'settings': [
            {
                'name': 'enableBeautyMotionBlur',
                'ui_name': 'Enable Beaty Motion Blur',
                'defaultValue': True,
                'help': 'If disabled, only velocity AOV will store information about movement on the scene. Required for motion blur that is generated in post-processing.',
                'houdini': {
                    'hidewhen': hidewhen_not_northstar
                }
            }
        ]
    },
    {
        'name': 'Seed',
        'settings': [
            {
                'name': 'uniformSeed',
                'ui_name': 'Use Uniform Seed',
                'defaultValue': True,
                'houdini': {
                    'hidewhen': 'renderQuality < 3'
                }
            }
        ]
    },
    {
        'name': 'UsdNativeCamera',
        'settings': [
            {
                'name': 'aspectRatioConformPolicy',
                'defaultValue': 'UsdRenderTokens->expandAperture',
            },
            {
                'name': 'instantaneousShutter',
                'defaultValue': False,
            },
        ]
    }
]

def camel_case_capitalize(w):
    return w[0].upper() + w[1:]

def generate_render_setting_files(install_path, generate_ds_files):
    header_template = (
'''
#ifndef GENERATED_HDRPR_CONFIG_H
#define GENERATED_HDRPR_CONFIG_H

#include "pxr/imaging/hd/tokens.h"
#include "pxr/imaging/hd/renderDelegate.h"

#include <mutex>

PXR_NAMESPACE_OPEN_SCOPE

{rs_tokens_declaration}
{rs_mapped_values_enum}

class HdRprConfig {{
public:
    enum ChangeTracker {{
        Clean = 0,
        DirtyAll = ~0u,
        DirtyInteractiveMode = 1 << 0,
{rs_category_dirty_flags}
    }};

    static HdRenderSettingDescriptorList GetRenderSettingDescriptors();
    static std::unique_lock<std::mutex> GetInstance(HdRprConfig** instance);

    void Sync(HdRenderDelegate* renderDelegate);

    void SetInteractiveMode(bool enable);
    bool GetInteractiveMode() const;

{rs_get_set_method_declarations}
    bool IsDirty(ChangeTracker dirtyFlag) const;
    void CleanDirtyFlag(ChangeTracker dirtyFlag);
    void ResetDirty();

private:
    HdRprConfig() = default;

    struct PrefData {{
        bool enableInteractive;

{rs_variables_declaration}

        PrefData();

        void SetDefault();

        bool IsValid();
    }};
    PrefData m_prefData;

    uint32_t m_dirtyFlags = DirtyAll;
    int m_lastRenderSettingsVersion = -1;
}};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // GENERATED_HDRPR_CONFIG_H
''').strip()

    cpp_template = (
'''
#include "config.h"
#include "rprApi.h"
#include "pxr/base/arch/fileSystem.h"
#include "pxr/usd/usdRender/tokens.h"

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PUBLIC_TOKENS(HdRprRenderSettingsTokens, HDRPR_RENDER_SETTINGS_TOKENS);
TF_DEFINE_PRIVATE_TOKENS(_tokens,
    ((houdiniInteractive, "houdini:interactive"))
);

{rs_public_token_definitions}

namespace {{

{rs_range_definitions}
}} // namespace anonymous

HdRenderSettingDescriptorList HdRprConfig::GetRenderSettingDescriptors() {{
    HdRenderSettingDescriptorList settingDescs;
{rs_list_initialization}
    return settingDescs;
}}


std::unique_lock<std::mutex> HdRprConfig::GetInstance(HdRprConfig** instancePtr) {{
    static std::mutex instanceMutex;
    static HdRprConfig instance;
    *instancePtr = &instance;
    return std::unique_lock<std::mutex>(instanceMutex);
}}

void HdRprConfig::Sync(HdRenderDelegate* renderDelegate) {{
    int currentSettingsVersion = renderDelegate->GetRenderSettingsVersion();
    if (m_lastRenderSettingsVersion != currentSettingsVersion) {{
        m_lastRenderSettingsVersion = currentSettingsVersion;
    
        auto getBoolSetting = [&renderDelegate](TfToken const& token, bool defaultValue) {{
            auto boolValue = renderDelegate->GetRenderSetting(token);
            if (boolValue.IsHolding<int64_t>()) {{
                return static_cast<bool>(boolValue.UncheckedGet<int64_t>());
            }} else if (boolValue.IsHolding<bool>()) {{
                return static_cast<bool>(boolValue.UncheckedGet<bool>());
            }}
            return defaultValue;
        }};

        auto interactiveMode = renderDelegate->GetRenderSetting<std::string>(_tokens->houdiniInteractive, "normal");
        SetInteractiveMode(interactiveMode != "normal");

{rs_sync}
    }}
}}

void HdRprConfig::SetInteractiveMode(bool enable) {{
    if (m_prefData.enableInteractive != enable) {{
        m_prefData.enableInteractive = enable;
        m_dirtyFlags |= DirtyInteractiveMode;
    }}
}}

bool HdRprConfig::GetInteractiveMode() const {{
    return m_prefData.enableInteractive;
}}

{rs_get_set_method_definitions}

bool HdRprConfig::IsDirty(ChangeTracker dirtyFlag) const {{
    return m_dirtyFlags & dirtyFlag;
}}

void HdRprConfig::CleanDirtyFlag(ChangeTracker dirtyFlag) {{
    m_dirtyFlags &= ~dirtyFlag;
}}

void HdRprConfig::ResetDirty() {{
    m_dirtyFlags = Clean;
}}

HdRprConfig::PrefData::PrefData() {{
    SetDefault();
}}

void HdRprConfig::PrefData::SetDefault() {{
    enableInteractive = false;

{rs_set_default_values}
}}

bool HdRprConfig::PrefData::IsValid() {{
    return true
{rs_validate_values};
}}

PXR_NAMESPACE_CLOSE_SCOPE

''').strip()

    dirty_flags_offset = 1

    rs_public_token_definitions = ''
    rs_tokens_declaration = '#define HDRPR_RENDER_SETTINGS_TOKENS \\\n'
    rs_category_dirty_flags = ''
    rs_get_set_method_declarations = ''
    rs_variables_declaration = ''
    rs_mapped_values_enum = ''
    rs_range_definitions = ''
    rs_list_initialization = ''
    rs_sync = ''
    rs_get_set_method_definitions = ''
    rs_set_default_values = ''
    rs_validate_values = ''
    for category in render_setting_categories:
        disabled_category = False

        category_name = category['name']
        dirty_flag = 'Dirty{}'.format(category_name)
        rs_category_dirty_flags += '        {} = 1 << {},\n'.format(dirty_flag, dirty_flags_offset)
        dirty_flags_offset += 1

        for setting in category['settings']:
            name = setting['name']
            rs_tokens_declaration += '    ({}) \\\n'.format(name)

            name_title = camel_case_capitalize(name)

            default_value = setting['defaultValue']

            c_type_str = type(default_value).__name__
            if c_type_str == 'str':
                c_type_str = 'TfToken'
            type_str = c_type_str

            if 'values' in setting:
                value_tokens_list_name = '__{}Tokens'.format(name_title)
                value_tokens_name = 'HdRpr{}Tokens'.format(name_title)

                rs_mapped_values_enum += '#define ' + value_tokens_list_name
                for value in setting['values']:
                    rs_mapped_values_enum += ' ({})'.format(value.get_key())
                rs_mapped_values_enum += '\n'

                rs_mapped_values_enum += 'TF_DECLARE_PUBLIC_TOKENS({}, {});\n\n'.format(value_tokens_name, value_tokens_list_name)
                rs_public_token_definitions += 'TF_DEFINE_PUBLIC_TOKENS({}, {});\n'.format(value_tokens_name, value_tokens_list_name)

                type_str = 'TfToken'
                c_type_str = type_str
                default_value = next(value for value in setting['values'] if value == default_value)

            rs_get_set_method_declarations += '    void Set{}({} {});\n'.format(name_title, c_type_str, name)
            rs_get_set_method_declarations += '    {} const& Get{}() const {{ return m_prefData.{}; }}\n\n'.format(type_str, name_title, name)

            rs_variables_declaration += '        {} {};\n'.format(type_str, name)

            if isinstance(default_value, bool):
                rs_sync += '        Set{name_title}(getBoolSetting(HdRprRenderSettingsTokens->{name}, k{name_title}Default));\n'.format(name_title=name_title, name=name)
            else:
                rs_sync += '        Set{name_title}(renderDelegate->GetRenderSetting(HdRprRenderSettingsTokens->{name}, k{name_title}Default));\n'.format(name_title=name_title, name=name)

            if 'values' in setting:
                rs_range_definitions += '#define k{name_title}Default {value_tokens_name}->{value}'.format(name_title=name_title, value_tokens_name=value_tokens_name, value=default_value.get_key())
            else:
                value_str = str(default_value)
                if isinstance(default_value, bool):
                    value_str = value_str.lower()
                rs_range_definitions += 'const {type} k{name_title}Default = {type}({value});\n'.format(type=type_str, name_title=name_title, value=value_str)

            set_validation = ''
            if 'minValue' in setting or 'maxValue' in setting:
                rs_validate_values += '           '
            if 'minValue' in setting:
                rs_range_definitions += 'const {type} k{name_title}Min = {type}({value});\n'.format(type=type_str, name_title=name_title, value=setting['minValue'])
                set_validation += '    if ({name} < k{name_title}Min) {{ return; }}\n'.format(name=name, name_title=name_title)
                rs_validate_values += '&& {name} < k{name_title}Min'.format(name=name, name_title=name_title)
            if 'maxValue' in setting:
                rs_range_definitions += 'const {type} k{name_title}Max = {type}({value});\n'.format(type=type_str, name_title=name_title, value=setting['maxValue'])
                set_validation += '    if ({name} > k{name_title}Max) {{ return; }}\n'.format(name=name, name_title=name_title)
                rs_validate_values += '&& {name} > k{name_title}Max'.format(name=name, name_title=name_title)
            if 'minValue' in setting or 'maxValue' in setting:
                rs_validate_values += '\n'
            rs_range_definitions += '\n'

            if 'values' in setting:
                value_range = value_tokens_name + '->allTokens'
                set_validation += '    if (std::find({range}.begin(), {range}.end(), {name}) == {range}.end()) return;\n'.format(range=value_range, name=name)

            if 'ui_name' in setting:
                rs_list_initialization += '    settingDescs.push_back({{"{}", HdRprRenderSettingsTokens->{}, VtValue(k{}Default)}});\n'.format(setting['ui_name'], name, name_title)

            if disabled_category:
                rs_get_set_method_definitions += 'void HdRprConfig::Set{name_title}({c_type} {name}) {{ /* Platform no-op */ }}'.format(name_title=name_title, c_type=c_type_str, name=name)
            else:
                rs_get_set_method_definitions += (
'''
void HdRprConfig::Set{name_title}({c_type} {name}) {{
{set_validation}
    if (m_prefData.{name} != {name}) {{
        m_prefData.{name} = {name};
        m_dirtyFlags |= {dirty_flag};
    }}
}}
''').format(name_title=name_title, c_type=c_type_str, name=name, dirty_flag=dirty_flag, set_validation=set_validation)

            rs_set_default_values += '    {name} = k{name_title}Default;\n'.format(name=name, name_title=name_title)

    rs_tokens_declaration += '\nTF_DECLARE_PUBLIC_TOKENS(HdRprRenderSettingsTokens, HDRPR_RENDER_SETTINGS_TOKENS);\n'

    header_dst_path = os.path.join(install_path, 'config.h')
    header_file = open(header_dst_path, 'w')
    header_file.write(header_template.format(
        rs_tokens_declaration=rs_tokens_declaration,
        rs_category_dirty_flags=rs_category_dirty_flags,
        rs_get_set_method_declarations=rs_get_set_method_declarations,
        rs_variables_declaration=rs_variables_declaration,
        rs_mapped_values_enum=rs_mapped_values_enum))

    cpp_dst_path = os.path.join(install_path, 'config.cpp')
    cpp_file = open(cpp_dst_path, 'w')
    cpp_file.write(cpp_template.format(
        rs_public_token_definitions=rs_public_token_definitions,
        rs_range_definitions=rs_range_definitions,
        rs_list_initialization=rs_list_initialization,
        rs_sync=rs_sync,
        rs_get_set_method_definitions=rs_get_set_method_definitions,
        rs_set_default_values=rs_set_default_values,
        rs_validate_values=rs_validate_values))

    if generate_ds_files:
        generate_houdini_ds(install_path, 'Global', render_setting_categories)


if __name__ == "__main__":
    p = argparse.ArgumentParser()
    p.add_argument("install", help="The install root for generated files.")
    p.add_argument("--generate_ds_files", default=False, action='store_true')
    args = p.parse_args()

    generate_render_setting_files(args.install, args.generate_ds_files)
