import os
import sys
import argparse
import platform

dry_run = False

render_setting_categories = [
    {
        'name': 'RenderQuality',
        'disabled_platform': ['Darwin'],
        'settings': [
            {
                'name': 'renderQuality',
                'ui_name': 'Render Quality',
                'help': 'Render restart might be required',
                'defaultValue': 3,
                'values': [
                    "Low",
                    "Medium",
                    "High",
                    "Full"
                ]
            }
        ]
    },
    {
        'name': 'Device',
        'houdini': {
            'hidewhen': 'renderQuality != 3'
        },
        'settings': [
            {
                'name': 'renderDevice',
                'ui_name': 'Render Device',
                'help': 'Restart required.',
                'defaultValue': 1,
                'values': [
                    "CPU",
                    "GPU",
                    # "CPU+GPU"
                ]
            }
        ]
    },
    {
        'name': 'Denoise',
        'houdini': {
            'hidewhen': 'renderQuality < 2'
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
            'hidewhen': 'renderQuality == 0'
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
            'hidewhen': 'renderQuality != 3'
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
            'hidewhen': 'renderQuality != 3'
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
]

def camel_case_capitalize(w):
    return w[0].upper() + w[1:]

def generate_files(install_path):
    header_template = (
'''
#ifndef GENERATED_HDRPR_CONFIG_H
#define GENERATED_HDRPR_CONFIG_H

#include "pxr/imaging/hd/tokens.h"
#include "pxr/imaging/hd/renderDelegate.h"

#include "rprcpp/rprContext.h"

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
        ~PrefData();

        void SetDefault();

        bool Load();
        void Save();

        bool IsValid();
    }};
    PrefData m_prefData;

    uint32_t m_dirtyFlags = DirtyAll;
    int m_lastRenderSettingsVersion = -1;

    constexpr static const char* k_rprPreferenceFilename = "hdRprPreferences.dat";
}};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // GENERATED_HDRPR_CONFIG_H
''').strip()

    cpp_template = (
'''
#include "config.h"
#include "rprApi.h"
#include "pxr/base/arch/fileSystem.h"

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PUBLIC_TOKENS(HdRprRenderSettingsTokens, HDRPR_RENDER_SETTINGS_TOKENS);
TF_DEFINE_PRIVATE_TOKENS(_tokens,
    ((houdiniInteractive, "houdini:interactive"))
);

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

        auto interactiveMode = renderDelegate->GetRenderSetting<std::string>(_tokens->houdiniInteractive, "");
        SetInteractiveMode(interactiveMode != "normal");

{rs_sync}
    }}
}}

void HdRprConfig::SetInteractiveMode(bool enable) {{
    if (m_prefData.enableInteractive != enable) {{
        m_prefData.enableInteractive = enable;
        m_prefData.Save();
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

bool HdRprConfig::PrefData::Load() {{
#ifdef ENABLE_PREFERENCES_FILE
    std::string appDataDir = HdRprApi::GetAppDataPath();
    std::string rprPreferencePath = (appDataDir.empty()) ? k_rprPreferenceFilename : (appDataDir + ARCH_PATH_SEP) + k_rprPreferenceFilename;

    if (FILE* f = fopen(rprPreferencePath.c_str(), "rb")) {{
        if (!fread(this, sizeof(PrefData), 1, f)) {{
            TF_CODING_ERROR("Fail to read rpr preferences dat file");
        }}
        fclose(f);
        return IsValid();
    }}
#endif // ENABLE_PREFERENCES_FILE

    return false;
}}

void HdRprConfig::PrefData::Save() {{
#ifdef ENABLE_PREFERENCES_FILE
    std::string appDataDir = HdRprApi::GetAppDataPath();
    std::string rprPreferencePath = (appDataDir.empty()) ? k_rprPreferenceFilename : (appDataDir + ARCH_PATH_SEP) + k_rprPreferenceFilename;

    if (FILE* f = fopen(rprPreferencePath.c_str(), "wb")) {{
        if (!fwrite(this, sizeof(PrefData), 1, f)) {{
            TF_CODING_ERROR("Fail to write rpr preferences dat file");
        }}
        fclose(f);
    }}
#endif // ENABLE_PREFERENCES_FILE
}}

HdRprConfig::PrefData::PrefData() {{
    if (!Load()) {{
        SetDefault();
    }}
}}

HdRprConfig::PrefData::~PrefData() {{
    Save();
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

    houdini_ds_template = (
'''
#include "$HFS/houdini/soho/parameters/CommonMacros.ds"

{{
    name    "RPR"
    label   "RPR"
    parmtag {{ spare_opfilter    "!!SHOP/PROPERTIES!!" }}
    parmtag {{ spare_classtags   "render" }}

    {houdini_params}
}}

''').strip()

    dirty_flags_offset = 1

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
    houdini_params = ''
    for category in render_setting_categories:
        disabled_category = False
        if 'disabled_platform' in category:
            if platform.system() in category['disabled_platform']:
                disabled_category = True

        category_name = category['name']
        dirty_flag = 'Dirty{}'.format(category_name)
        rs_category_dirty_flags += '        {} = 1 << {},\n'.format(dirty_flag, dirty_flags_offset)
        dirty_flags_offset += 1

        category_hidewhen = None
        if 'houdini' in category:
            category_hidewhen = category['houdini'].get('hidewhen')

        for setting in category['settings']:
            name = setting['name']
            rs_tokens_declaration += '    ({}) \\\n'.format(name)

            name_title = camel_case_capitalize(name)

            default_value = setting['defaultValue']

            c_type_str = type(default_value).__name__
            type_str = c_type_str

            if 'values' in setting:
                setting['minValue'] = 0
                setting['maxValue'] = len(setting['values']) - 1
                rs_mapped_values_enum += 'enum {name_title}Type {{\n'.format(name_title=name_title)
                for value in setting['values']:
                    rs_mapped_values_enum += '    k{name_title}{value},\n'.format(name_title=name_title, value=value)
                rs_mapped_values_enum += '};\n'
                type_str = '{name_title}Type'.format(name_title=name_title)

            rs_get_set_method_declarations += '    void Set{}({} {});\n'.format(name_title, c_type_str, name)
            rs_get_set_method_declarations += '    {} Get{}() const {{ return m_prefData.{}; }}\n\n'.format(type_str, name_title, name)

            rs_variables_declaration += '        {} {};\n'.format(type_str, name)

            value_str = str(default_value)
            if isinstance(default_value, bool):
                value_str = value_str.lower()
                rs_sync += '        Set{name_title}(getBoolSetting(HdRprRenderSettingsTokens->{name}, k{name_title}Default));\n'.format(name_title=name_title, name=name)
            else:
                rs_sync += '        Set{name_title}(renderDelegate->GetRenderSetting(HdRprRenderSettingsTokens->{name}, {type}(k{name_title}Default)));\n'.format(name_title=name_title, name=name, type=c_type_str)

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

            rs_list_initialization += '    settingDescs.push_back({{"{}", HdRprRenderSettingsTokens->{}, VtValue(k{}Default)}});\n'.format(setting['ui_name'], name, name_title)

            if disabled_category:
                rs_get_set_method_definitions += 'void HdRprConfig::Set{name_title}({c_type} {name}) {{ /* Platform no-op */ }}'.format(name_title=name_title, c_type=c_type_str, name=name)
            else:
                rs_get_set_method_definitions += (
'''
void HdRprConfig::Set{name_title}({c_type} {name}) {{
{set_validation}
    if (m_prefData.{name} != {name}) {{
        m_prefData.{name} = {type}({name});
        m_prefData.Save();
        m_dirtyFlags |= {dirty_flag};
    }}
}}
''').format(name_title=name_title, c_type=c_type_str, type=type_str, name=name, dirty_flag=dirty_flag, set_validation=set_validation)

            rs_set_default_values += '    {name} = k{name_title}Default;\n'.format(name=name, name_title=name_title)


            houdini_settings = setting.get('houdini', {})
            houdini_param_label = setting['ui_name']
            houdini_hidewhen_conditions = []
            if 'hidewhen' in houdini_settings or category_hidewhen:
                houdini_hidewhen_conditions.append(houdini_settings.get('hidewhen', category_hidewhen))

            def CreateHoudiniParam(name, label, htype, default, values=[], tags=[], disablewhen_conditions=[], size=None, valid_range=None, help_msg=None, is_controlling_param=False):
                param = 'parm {\n'
                param += '    name "{}"\n'.format(name)
                param += '    label "{}"\n'.format(label)
                param += '    type {}\n'.format(htype)
                if size: param += '    size {}\n'.format(size)
                param += '    default {{ {} }}\n'.format(default)
                for tag in tags:
                    param += '    parmtag {{ {} }}\n'.format(tag)
                if values:
                    param += '    {} {{\n'.format('menujoin' if is_controlling_param else 'menu')
                    for value in values:
                        param += '        "{}" "{}"\n'.format(value[0], value[1])
                    param += '    }\n'
                if disabled_category:
                    param += '    invisible\n'
                if houdini_hidewhen_conditions:
                    param += '    hidewhen "'
                    for condition in houdini_hidewhen_conditions:
                        param += '{{ {} }} '.format(condition)
                    param += '"\n'
                if disablewhen_conditions:
                    param += '    disablewhen "'
                    for condition in disablewhen_conditions:
                        param += '{{ {} }} '.format(condition)
                    param += '"\n'
                if valid_range:                    
                    param += '    range {{ {}! {} }}\n'.format(valid_range[0], valid_range[1])
                if help_msg:
                    param += '    help "{}"\n'.format(help_msg)
                param += '}\n'

                return param

            control_param_name = name + '_control'
            houdini_params += CreateHoudiniParam(control_param_name, houdini_param_label, 'string', '"none"',
                values=[
                    ('set', 'Set or Create'),
                    ('setexisting', 'Set if Exists'),
                    ('block', 'Block'),
                    ('none', 'Do Nothing'),
                ],
                is_controlling_param=True)

            render_param_values = []
            render_param_type = c_type_str
            render_param_default = default_value
            if isinstance(default_value, bool):
                render_param_type = 'toggle'
                render_param_default = 1 if default_value else 0
            elif 'values' in setting:
                render_param_type = 'ordinal'
                for value in setting['values']:
                    render_param_values.append((len(render_param_values), value))

            render_param_range = None
            if 'minValue' in setting and 'maxValue' in setting and not 'values' in setting:
                render_param_range = (setting['minValue'], setting['maxValue'])

            houdini_params += CreateHoudiniParam(name, houdini_param_label, render_param_type, render_param_default,
                values=render_param_values,
                tags=[
                    '"spare_category" "{}"'.format(category_name),
                    '"uiscope" "viewport"',
                    '"usdvaluetype" "{}"'.format(c_type_str)
                ] + houdini_settings.get('custom_tags', []),
                disablewhen_conditions=[
                    control_param_name + ' == block',
                    control_param_name + ' == none',
                ],
                size=1,
                valid_range=render_param_range,
                help_msg=setting.get('help', None))

    rs_tokens_declaration += '\nTF_DECLARE_PUBLIC_TOKENS(HdRprRenderSettingsTokens, HDRPR_RENDER_SETTINGS_TOKENS);'

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
        rs_range_definitions=rs_range_definitions,
        rs_list_initialization=rs_list_initialization,
        rs_sync=rs_sync,
        rs_get_set_method_definitions=rs_get_set_method_definitions,
        rs_set_default_values=rs_set_default_values,
        rs_validate_values=rs_validate_values))

    houdini_ds_dst_path = os.path.join(install_path, 'HdRprPlugin_Global.ds')
    houdini_ds_file = open(houdini_ds_dst_path, 'w')
    houdini_ds_file.write(houdini_ds_template.format(houdini_params=houdini_params))

if __name__ == "__main__":
    p = argparse.ArgumentParser(description="Generate config sources.")
    p.add_argument("install", help="The install root for generated files.")
    p.add_argument("-n", "--dry_run", dest="dry_run", action="store_true", help="Only summarize what would happen")
    args = p.parse_args()

    if args.dry_run:
        dry_run = True

    generate_files(args.install)
