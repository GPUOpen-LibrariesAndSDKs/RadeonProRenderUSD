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
        return '{} {} "{}"'.format(houdini_parm_name('core:renderQuality'), operator, quality)
    elif operator == '<':
        render_quality = get_render_setting(render_setting_categories, 'RenderQuality', 'core:renderQuality')
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
    return hidewhen_render_quality('<', 'Northstar', render_setting_categories)

def hidewhen_not_northstar(render_setting_categories):
    return hidewhen_render_quality('!=', 'Northstar', render_setting_categories)

def houdini_parm_name(name):
    import hou
    return hou.encode('rpr:' + name)

HYBRID_IS_AVAILABLE_PY_CONDITION = lambda: 'platform.system() != "Darwin"'
NORTHSTAR_ENABLED_PY_CONDITION = lambda: 'hou.pwd().parm("{}").evalAsString() == "Northstar"'.format(houdini_parm_name('core:renderQuality'))
NOT_NORTHSTAR_ENABLED_PY_CONDITION = lambda: 'hou.pwd().parm("{}").evalAsString() != "Northstar"'.format(houdini_parm_name('core:renderQuality'))

render_setting_categories = [
    {
        'name': 'RenderQuality',
        'settings': [
            {
                'name': 'core:renderQuality',
                'ui_name': 'Render Quality',
                'help': 'Render restart might be required',
                'defaultValue': 'Northstar',
                'values': [
                    SettingValue('Low', enable_py_condition=HYBRID_IS_AVAILABLE_PY_CONDITION),
                    SettingValue('Medium', enable_py_condition=HYBRID_IS_AVAILABLE_PY_CONDITION),
                    SettingValue('High', enable_py_condition=HYBRID_IS_AVAILABLE_PY_CONDITION),
                    SettingValue('HybridPro', enable_py_condition=HYBRID_IS_AVAILABLE_PY_CONDITION),
                    SettingValue('Northstar', 'Full')
                ]
            }
        ]
    },
    {
        'name': 'RenderMode',
        'settings': [
            {
                'name': 'core:renderMode',
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
                    SettingValue('Diffuse'),
                    SettingValue('Contour', enable_py_condition=NORTHSTAR_ENABLED_PY_CONDITION),
                ]
            },
            {
                'name': 'ambientOcclusion:radius',
                'ui_name': 'Ambient Occlusion Radius',
                'defaultValue': 1.0,
                'minValue': 0.0,
                'maxValue': 100.0,
                'houdini': {
                    'hidewhen': 'core:renderMode != "AmbientOcclusion"'
                }
            },
            {
                'folder': 'Contour Settings',
                'houdini': {
                    'hidewhen': 'core:renderMode != "Contour"'
                },
                'settings': [
                    {
                        'name': 'contour:antialiasing',
                        'ui_name': 'Antialiasing',
                        'defaultValue': 1.0,
                        'minValue': 0.0,
                        'maxValue': 1.0,
                        'houdini': {
                            'hidewhen': 'core:renderMode != "Contour"'
                        }
                    },
                    {
                        'name': 'contour:useNormal',
                        'ui_name': 'Use Normal',
                        'defaultValue': True,
                        'help': 'Whether to use geometry normals for edge detection or not',
                        'houdini': {
                            'hidewhen': 'core:renderMode != "Contour"'
                        }
                    },
                    {
                        'name': 'contour:linewidthNormal',
                        'ui_name': 'Linewidth Normal',
                        'defaultValue': 1.0,
                        'minValue': 0.0,
                        'maxValue': 100.0,
                        'help': 'Linewidth of edges detected via normals',
                        'houdini': {
                            'hidewhen': ['core:renderMode != "Contour"', 'contour:useNormal == 0']
                        }
                    },
                    {
                        'name': 'contour:normalThreshold',
                        'ui_name': 'Normal Threshold',
                        'defaultValue': 45.0,
                        'minValue': 0.0,
                        'maxValue': 180.0,
                        'houdini': {
                            'hidewhen': ['core:renderMode != "Contour"', 'contour:useNormal == 0']
                        }
                    },
                    {
                        'name': 'contour:usePrimId',
                        'ui_name': 'Use Primitive Id',
                        'defaultValue': True,
                        'help': 'Whether to use primitive Id for edge detection or not',
                        'houdini': {
                            'hidewhen': 'core:renderMode != "Contour"'
                        }
                    },
                    {
                        'name': 'contour:linewidthPrimId',
                        'ui_name': 'Linewidth Primitive Id',
                        'defaultValue': 1.0,
                        'minValue': 0.0,
                        'maxValue': 100.0,
                        'help': 'Linewidth of edges detected via primitive Id',
                        'houdini': {
                            'hidewhen': ['core:renderMode != "Contour"', 'contour:usePrimId == 0']
                        }
                    },
                    {
                        'name': 'contour:useMaterialId',
                        'ui_name': 'Use Material Id',
                        'defaultValue': True,
                        'help': 'Whether to use material Id for edge detection or not',
                        'houdini': {
                            'hidewhen': 'core:renderMode != "Contour"'
                        }
                    },
                    {
                        'name': 'contour:linewidthMaterialId',
                        'ui_name': 'Linewidth Material Id',
                        'defaultValue': 1.0,
                        'minValue': 0.0,
                        'maxValue': 100.0,
                        'help': 'Linewidth of edges detected via material Id',
                        'houdini': {
                            'hidewhen': ['core:renderMode != "Contour"', 'contour:useMaterialId == 0']
                        }
                    },
                    {
                        'name': 'contour:useUv',
                        'ui_name': 'Use UV',
                        'defaultValue': True,
                        'help': 'Whether to use UV for edge detection or not',
                        'houdini': {
                            'hidewhen': 'core:renderMode != "Contour"'
                        }
                    },
                    {
                        'name': 'contour:linewidthUv',
                        'ui_name': 'Linewidth UV',
                        'defaultValue': 1.0,
                        'minValue': 0.0,
                        'maxValue': 100.0,
                        'help': 'Linewidth of edges detected via UV',
                        'houdini': {
                            'hidewhen': ['core:renderMode != "Contour"', 'contour:useUv == 0']
                        }
                    },
                    {
                        'name': 'contour:uvThreshold',
                        'ui_name': 'UV Threshold',
                        'defaultValue': 1.0,
                        'minValue': 0.0,
                        'maxValue': 1.0,
                        'help': 'Threshold of edges detected via UV',
                        'houdini': {
                            'hidewhen': ['core:renderMode != "Contour"', 'contour:useUv == 0']
                        }
                    },
                    {
                        'name': 'contour:debug',
                        'ui_name': 'Debug',
                        'defaultValue': False,
                        'help': 'Whether to show colored outlines according to used features or not.\\n'
                                'Colors legend:\\n'
                                ' * red - primitive Id\\n'
                                ' * green - material Id\\n'
                                ' * blue - normal\\n'
                                ' * yellow - primitive Id + material Id\\n'
                                ' * magenta - primitive Id + normal\\n'
                                ' * cyan - material Id + normal\\n'
                                ' * black - all',
                        'houdini': {
                            'hidewhen': 'core:renderMode != "Contour"'
                        }
                    }
                ]
            }
        ],
        'houdini': {
            'hidewhen': hidewhen_hybrid
        }
    },
    {
        'name': 'Denoise',
        'houdini': {
            'hidewhen': lambda settings: hidewhen_render_quality('<', 'High', settings)
        },
        'settings': [
            {
                'name': 'denoising:enable',
                'ui_name': 'Enable AI Denoising',
                'defaultValue': False,
                'houdini': {
                    'custom_tags': [
                        '"uiicon" VIEW_display_denoise'
                    ]
                }
            },
            {
                'folder': 'Denoise Settings',
                'houdini': {
                    'hidewhen': 'denoising:enable == 0'
                },
                'settings': [
                    {
                        'name': 'denoising:minIter',
                        'ui_name': 'Denoise Min Iteration',
                        'defaultValue': 4,
                        'minValue': 1,
                        'maxValue': 2 ** 16,
                        'help': 'The first iteration on which denoising should be applied.'
                    },
                    {
                        'name': 'denoising:iterStep',
                        'ui_name': 'Denoise Iteration Step',
                        'defaultValue': 32,
                        'minValue': 1,
                        'maxValue': 2 ** 16,
                        'help': 'Denoise use frequency. To denoise on each iteration, set to 1.'
                    }
                ]
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
                'ui_name': 'Max Samples',
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
                'name': 'adaptiveSampling:minSamples',
                'ui_name': 'Min Samples',
                'help': 'Minimum number of samples to render for each pixel. After this, adaptive sampling will stop sampling pixels where noise is less than \'Variance Threshold\'.',
                'defaultValue': 64,
                'minValue': 1,
                'maxValue': 2 ** 16
            },
            {
                'name': 'adaptiveSampling:noiseTreshold',
                'ui_name': 'Noise Threshold',
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
                'name': 'quality:rayDepth',
                'ui_name': 'Max Ray Depth',
                'help': 'The number of times that a ray bounces off various surfaces before being terminated.',
                'defaultValue': 8,
                'minValue': 1,
                'maxValue': 50
            },
            {
                'name': 'quality:rayDepthDiffuse',
                'ui_name': 'Diffuse Ray Depth',
                'help': 'The maximum number of times that a light ray can be bounced off diffuse surfaces.',
                'defaultValue': 3,
                'minValue': 0,
                'maxValue': 50
            },
            {
                'name': 'quality:rayDepthGlossy',
                'ui_name': 'Glossy Ray Depth',
                'help': 'The maximum number of ray bounces from specular surfaces.',
                'defaultValue': 3,
                'minValue': 0,
                'maxValue': 50
            },
            {
                'name': 'quality:rayDepthRefraction',
                'ui_name': 'Refraction Ray Depth',
                'help': 'The maximum number of times that a light ray can be refracted, and is designated for clear transparent materials, such as glass.',
                'defaultValue': 3,
                'minValue': 0,
                'maxValue': 50
            },
            {
                'name': 'quality:rayDepthGlossyRefraction',
                'ui_name': 'Glossy Refraction Ray Depth',
                'help': 'The Glossy Refraction Ray Depth parameter is similar to the Refraction Ray Depth. The difference is that it is aimed to work with matte refractive materials, such as semi-frosted glass.',
                'defaultValue': 3,
                'minValue': 0,
                'maxValue': 50
            },
            {
                'name': 'quality:rayDepthShadow',
                'ui_name': 'Shadow Ray Depth',
                'help': 'Controls the accuracy of shadows cast by transparent objects. It defines the maximum number of surfaces that a light ray can encounter on its way causing these surfaces to cast shadows.',
                'defaultValue': 2,
                'minValue': 0,
                'maxValue': 50
            },
            {
                'name': 'quality:raycastEpsilon',
                'ui_name': 'Ray Cast Epsilon',
                'help': 'Determines an offset used to move light rays away from the geometry for ray-surface intersection calculations.',
                'defaultValue': 2e-3,
                'minValue': 1e-6,
                'maxValue': 1.0
            },
            {
                'name': 'quality:radianceClamping',
                'ui_name': 'Max Radiance',
                'help': 'Limits the intensity, or the maximum brightness, of samples in the scene. Greater clamp radiance values produce more brightness.',
                'defaultValue': 0.0,
                'minValue': 0.0,
                'maxValue': 1e6
            },
            {
                'name': 'quality:imageFilterRadius',
                'ui_name': 'Pixel filter width',
                'help': 'Determines Pixel filter width (anti-aliasing).',
                'defaultValue': 1.5,
                'minValue': 0.0,
                'maxValue': 5.0
            }
        ]
    },
    {
        'name': 'InteractiveQuality',
        'settings': [
            {
                'name': 'quality:interactive:rayDepth',
                'ui_name': 'Interactive Max Ray Depth',
                'help': 'Controls value of \'Max Ray Depth\' in interactive mode.',
                'defaultValue': 2,
                'minValue': 1,
                'maxValue': 50,
                'houdini': {
                    'hidewhen': hidewhen_hybrid
                }
            },
            {
                'name': 'quality:interactive:downscale:resolution',
                'ui_name': 'Interactive Resolution Downscale',
                'help': 'Controls how much rendering resolution is downscaled in interactive mode. Formula: resolution / (2 ^ downscale). E.g. downscale==2 will give you 4 times smaller rendering resolution.',
                'defaultValue': 3,
                'minValue': 0,
                'maxValue': 10,
                'houdini': {
                    'hidewhen': hidewhen_not_northstar
                }
            },
            {
                'name': 'quality:interactive:downscale:enable',
                'ui_name': 'Downscale Resolution When Interactive',
                'help': 'Controls whether in interactive mode resolution should be downscaled or no.',
                'defaultValue': True,
            }
        ]
    },
    {
        'name': 'Gamma',
        'settings': [
            {
                'name': 'gamma:enable',
                'ui_name': 'Enable Gamma',
                'help': 'Enable Gamma',
                'defaultValue': False
            },
            {
                'name': 'gamma:value',
                'ui_name': 'Gamma',
                'help': 'Gamma value',
                'defaultValue': 1.0,
                'minValue': 0.0,
                'maxValue': 5.0,
                'houdini': {
                    'hidewhen': 'gamma:enable == 0'
                }
            }
        ]
    },
    {
        'name': 'Tonemapping',
        'settings': [
            {
                'name': 'tonemapping:enable',
                'ui_name': 'Enable Tone Mapping',
                'help': 'Enable linear photographic tone mapping filter. More info in RIF documentation',
                'defaultValue': False
            },
            {
                'name': 'tonemapping:exposureTime',
                'ui_name': 'Film Exposure Time (sec)',
                'help': 'Film exposure time',
                'defaultValue': 0.125,
                'minValue': 0.0,
                'maxValue': 10.0,
                'houdini': {
                    'hidewhen': 'tonemapping:enable == 0'
                }
            },
            {
                'name': 'tonemapping:sensitivity',
                'ui_name': 'Film Sensitivity',
                'help': 'Luminance of the scene (in candela per m^2)',
                'defaultValue': 1.0,
                'minValue': 0.0,
                'maxValue': 10.0,
                'houdini': {
                    'hidewhen': 'tonemapping:enable == 0'
                }
            },
            {
                'name': 'tonemapping:fstop',
                'ui_name': 'Fstop',
                'help': 'Aperture f-number',
                'defaultValue': 1.0,
                'minValue': 0.0,
                'maxValue': 100.0,
                'houdini': {
                    'hidewhen': 'tonemapping:enable == 0'
                }
            },
            {
                'name': 'tonemapping:gamma',
                'ui_name': 'Tone Mapping Gamma',
                'help': 'Gamma correction value',
                'defaultValue': 1.0,
                'minValue': 0.0,
                'maxValue': 5.0,
                'houdini': {
                    'hidewhen': 'tonemapping:enable == 0'
                }
            }
        ]
    },
    {
        'name': 'Alpha',
        'settings': [
            {
                'name': 'alpha:enable',
                'ui_name': 'Enable Color Alpha',
                'defaultValue': False
            }
        ]
    },
    {
        'name': 'MotionBlur',
        'settings': [
            {
                'name': 'beautyMotionBlur:enable',
                'ui_name': 'Enable Beauty Motion Blur',
                'defaultValue': True,
                'help': 'If disabled, only velocity AOV will store information about movement on the scene. Required for motion blur that is generated in post-processing.',
                'houdini': {
                    'hidewhen': hidewhen_not_northstar
                }
            }
        ]
    },
    {
        'name': 'OCIO',
        'settings': [
            {
                'name': 'ocio:configPath',
                'ui_name': 'OpenColorIO Config Path',
                'defaultValue': '',
                'c_type': 'SdfAssetPath',
                'help': 'The file path of the OpenColorIO config file to be used. Overrides any value specified in OCIO environment variable.',
                'houdini': {
                    'type': 'file',
                    'hidewhen': hidewhen_not_northstar
                }
            },
            {
                'name': 'ocio:renderingColorSpace',
                'ui_name': 'OpenColorIO Rendering Color Space',
                'defaultValue': '',
                'c_type': 'std::string',
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
                    'hidewhen': hidewhen_hybrid
                }
            },
            {
                'name': 'seedOverride',
                'ui_name': 'Random Seed Override',
                'defaultValue': 0,
                'minValue': 0,
                'maxValue': 2 ** 16
            }
        ]
    },
    {
        'name': 'Cryptomatte',
        'settings': [
            {
                'name': 'cryptomatte:outputPath',
                'ui_name': 'Cryptomatte Output Path',
                'defaultValue': '',
                'c_type': 'SdfAssetPath',
                'help': 'Controls where cryptomatte should be saved. Use \'Cryptomatte Output Mode\' to control when cryptomatte is saved.',
                'houdini': {
                    'type': 'file'
                }
            },
            {
                'name': 'cryptomatte:outputMode',
                'ui_name': 'Cryptomatte Output Mode',
                'defaultValue': 'Batch',
                'values': [
                    SettingValue('Batch'),
                    SettingValue('Interactive')
                ],
                'help': 'Batch - save cryptomatte only in the batch rendering mode (USD Render ROP, husk). Interactive - same as the Batch but also save cryptomatte in the non-batch rendering mode. Cryptomatte always saved after \'Max Samples\' is reached.',
                'houdini': {
                    'hidewhen': 'cryptomatte:outputPath == ""',
                }
            },
            {
                'name': 'cryptomatte:previewLayer',
                'ui_name': 'Cryptomatte Add Preview Layer',
                'defaultValue': False,
                'help': 'Whether to generate cryptomatte preview layer or not. Whether you need it depends on the software you are planning to use cryptomatte in. For example, Houdini\'s COP Cryptomatte requires it, Nuke, on contrary, does not.',
                'houdini': {
                    'hidewhen': 'cryptomatte:outputPath == ""',
                }

            }
        ],
        'houdini': {
            'hidewhen': hidewhen_not_northstar
        }
    },
    {
        'name': 'Camera',
        'settings': [
            {
                'name': 'core:cameraMode',
                'ui_name': 'Camera Mode',
                'defaultValue': 'Default',
                'values': [
                    SettingValue('Default'),
                    SettingValue('Latitude Longitude 360'),
                    SettingValue('Latitude Longitude Stereo'),
                    SettingValue('Cubemap', enable_py_condition=NOT_NORTHSTAR_ENABLED_PY_CONDITION),
                    SettingValue('Cubemap Stereo', enable_py_condition=NOT_NORTHSTAR_ENABLED_PY_CONDITION),
                    SettingValue('Fisheye'),
                ]
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
    },
    {
        'name': 'RprExport',
        'settings': [
            {
                'name': 'export:path',
                'defaultValue': '',
                'c_type': 'SdfAssetPath'
            },
            {
                'name': 'export:asSingleFile',
                'defaultValue': False
            },
            {
                'name': 'export:useImageCache',
                'defaultValue': False
            }
        ]
    },
    {
        'name': 'Session',
        'settings': [
            {
                'name': 'renderMode',
                'defaultValue': 'interactive',
                'values': [
                    SettingValue('batch'),
                    SettingValue('interactive')
                ]
            },
            {
                'name': 'progressive',
                'defaultValue': True
            }
        ]
    },
    {
        'name': 'ImageTransformation',
        'settings': [
            {
                'name': 'core:flipVertical',
                'defaultValue': False
            }
        ]
    },
    {
        'name': 'ViewportSettings',
        'houdini': {},
        'settings': [
            {
                'name': 'openglInteroperability',
                'ui_name': 'OpenGL interoperability (Needs render restart)',
                'help': '',
                'defaultValue': False,
            },
            {
                'name': 'viewportUpscaling',
                'ui_name': 'Viewport Upscaling (Needs render restart)',
                'help': '',
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
#include "pxr/usd/sdf/assetPath.h"

#include <mutex>

PXR_NAMESPACE_OPEN_SCOPE

{rs_tokens_declaration}
{rs_mapped_values_enum}

class HdRprConfig {{
public:
    HdRprConfig() = default;

    enum ChangeTracker {{
        Clean = 0,
        DirtyAll = ~0u,
        DirtyInteractiveMode = 1 << 0,
{rs_category_dirty_flags}
    }};

    static HdRenderSettingDescriptorList GetRenderSettingDescriptors();

    void Sync(HdRenderDelegate* renderDelegate);

    void SetInteractiveMode(bool enable);
    bool GetInteractiveMode() const;

{rs_get_set_method_declarations}
    bool IsDirty(ChangeTracker dirtyFlag) const;
    void CleanDirtyFlag(ChangeTracker dirtyFlag);
    void ResetDirty();

private:

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
    ((rprInteractive, "rpr:interactive"))
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

        bool interactiveMode = getBoolSetting(_tokens->rprInteractive, false);

        if (renderDelegate->GetRenderSetting<std::string>(_tokens->houdiniInteractive, "normal") != "normal") {{
            interactiveMode = true;
        }}

        SetInteractiveMode(interactiveMode);

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

    rs_public_token_definitions = []
    rs_tokens_declaration = ['#define HDRPR_RENDER_SETTINGS_TOKENS \\\n']
    rs_category_dirty_flags = []
    rs_get_set_method_declarations = []
    rs_variables_declaration = []
    rs_mapped_values_enum = []
    rs_range_definitions = []
    rs_list_initialization = []
    rs_sync = []
    rs_get_set_method_definitions = []
    rs_set_default_values = []
    rs_validate_values = []
    for category in render_setting_categories:
        disabled_category = False

        category_name = category['name']
        dirty_flag = 'Dirty{}'.format(category_name)
        rs_category_dirty_flags.append('        {} = 1 << {},\n'.format(dirty_flag, dirty_flags_offset))
        dirty_flags_offset += 1

        def process_setting(setting):
            name = setting['name']

            first, *others = name.split(':')
            c_name = ''.join([first[0].lower() + first[1:], *map(camel_case_capitalize, others)])

            rs_tokens_declaration.append('    (({}, "rpr:{}")) \\\n'.format(c_name, name))

            name_title = camel_case_capitalize(c_name)

            default_value = setting['defaultValue']

            if 'c_type' in setting:
                c_type_str = setting['c_type']
            else:
                c_type_str = type(default_value).__name__
                if c_type_str == 'str':
                    c_type_str = 'TfToken'
            type_str = c_type_str

            if 'values' in setting:
                value_tokens_list_name = '__{}Tokens'.format(name_title)
                value_tokens_name = 'HdRpr{}Tokens'.format(name_title)

                rs_mapped_values_enum.append('#define ' + value_tokens_list_name)
                for value in setting['values']:
                    rs_mapped_values_enum.append(' ({})'.format(value.get_key()))
                rs_mapped_values_enum.append('\n')

                rs_mapped_values_enum.append('TF_DECLARE_PUBLIC_TOKENS({}, {});\n\n'.format(value_tokens_name, value_tokens_list_name))
                rs_public_token_definitions.append('TF_DEFINE_PUBLIC_TOKENS({}, {});\n'.format(value_tokens_name, value_tokens_list_name))

                type_str = 'TfToken'
                c_type_str = type_str
                default_value = next(value for value in setting['values'] if value == default_value)

            rs_get_set_method_declarations.append('    void Set{}({} {});\n'.format(name_title, c_type_str, c_name))
            rs_get_set_method_declarations.append('    {} const& Get{}() const {{ return m_prefData.{}; }}\n\n'.format(type_str, name_title, c_name))

            rs_variables_declaration.append('        {} {};\n'.format(type_str, c_name))

            if isinstance(default_value, bool):
                rs_sync.append('        Set{name_title}(getBoolSetting(HdRprRenderSettingsTokens->{c_name}, k{name_title}Default));\n'.format(name_title=name_title, c_name=c_name))
            else:
                rs_sync.append('        Set{name_title}(renderDelegate->GetRenderSetting(HdRprRenderSettingsTokens->{c_name}, k{name_title}Default));\n'.format(name_title=name_title, c_name=c_name))

            if 'values' in setting:
                rs_range_definitions.append('#define k{name_title}Default {value_tokens_name}->{value}'.format(name_title=name_title, value_tokens_name=value_tokens_name, value=default_value.get_key()))
            else:
                value_str = str(default_value)
                if isinstance(default_value, bool):
                    value_str = value_str.lower()
                rs_range_definitions.append('const {type} k{name_title}Default = {type}({value});\n'.format(type=type_str, name_title=name_title, value=value_str))

            set_validation = ''
            if 'minValue' in setting or 'maxValue' in setting:
                rs_validate_values.append('           ')
            if 'minValue' in setting:
                rs_range_definitions.append('const {type} k{name_title}Min = {type}({value});\n'.format(type=type_str, name_title=name_title, value=setting['minValue']))
                set_validation += '    if ({c_name} < k{name_title}Min) {{ return; }}\n'.format(c_name=c_name, name_title=name_title)
                rs_validate_values.append('&& {c_name} < k{name_title}Min'.format(c_name=c_name, name_title=name_title))
            if 'maxValue' in setting:
                rs_range_definitions.append('const {type} k{name_title}Max = {type}({value});\n'.format(type=type_str, name_title=name_title, value=setting['maxValue']))
                set_validation += '    if ({c_name} > k{name_title}Max) {{ return; }}\n'.format(c_name=c_name, name_title=name_title)
                rs_validate_values.append('&& {c_name} > k{name_title}Max'.format(c_name=c_name, name_title=name_title))
            if 'minValue' in setting or 'maxValue' in setting:
                rs_validate_values.append('\n')
            rs_range_definitions.append('\n')

            if 'values' in setting:
                value_range = value_tokens_name + '->allTokens'
                set_validation += '    if (std::find({range}.begin(), {range}.end(), {c_name}) == {range}.end()) return;\n'.format(range=value_range, c_name=c_name)

            if 'ui_name' in setting:
                rs_list_initialization.append('    settingDescs.push_back({{"{}", HdRprRenderSettingsTokens->{}, VtValue(k{}Default)}});\n'.format(setting['ui_name'], c_name, name_title))

            if disabled_category:
                rs_get_set_method_definitions.append('void HdRprConfig::Set{name_title}({c_type} {c_name}) {{ /* Platform no-op */ }}'.format(name_title=name_title, c_type=c_type_str, c_name=c_name))
            else:
                rs_get_set_method_definitions.append((
'''
void HdRprConfig::Set{name_title}({c_type} {c_name}) {{
{set_validation}
    if (m_prefData.{c_name} != {c_name}) {{
        m_prefData.{c_name} = {c_name};
        m_dirtyFlags |= {dirty_flag};
    }}
}}
''').format(name_title=name_title, c_type=c_type_str, c_name=c_name, dirty_flag=dirty_flag, set_validation=set_validation))

            rs_set_default_values.append('    {c_name} = k{name_title}Default;\n'.format(c_name=c_name, name_title=name_title))

        for setting in category['settings']:
            if 'folder' in setting:
                for sub_setting in setting['settings']:
                    process_setting(sub_setting)
            else:
                process_setting(setting)

    rs_tokens_declaration.append('\nTF_DECLARE_PUBLIC_TOKENS(HdRprRenderSettingsTokens, HDRPR_RENDER_SETTINGS_TOKENS);\n')

    header_dst_path = os.path.join(install_path, 'config.h')
    header_file = open(header_dst_path, 'w')
    header_file.write(header_template.format(
        rs_tokens_declaration=''.join(rs_tokens_declaration),
        rs_category_dirty_flags=''.join(rs_category_dirty_flags),
        rs_get_set_method_declarations=''.join(rs_get_set_method_declarations),
        rs_variables_declaration=''.join(rs_variables_declaration),
        rs_mapped_values_enum=''.join(rs_mapped_values_enum)))

    cpp_dst_path = os.path.join(install_path, 'config.cpp')
    cpp_file = open(cpp_dst_path, 'w')
    cpp_file.write(cpp_template.format(
        rs_public_token_definitions=''.join(rs_public_token_definitions),
        rs_range_definitions=''.join(rs_range_definitions),
        rs_list_initialization=''.join(rs_list_initialization),
        rs_sync=''.join(rs_sync),
        rs_get_set_method_definitions=''.join(rs_get_set_method_definitions),
        rs_set_default_values=''.join(rs_set_default_values),
        rs_validate_values=''.join(rs_validate_values)))

    if generate_ds_files:
        production_render_setting_categories = [category for category in render_setting_categories if category['name'] != 'ViewportSettings']
        generate_houdini_ds(install_path, 'Global', production_render_setting_categories)        
        viewport_render_setting_categories = [category for category in render_setting_categories \
            if category['name'] == 'Sampling' or category['name'] == 'AdaptiveSampling' or category['name'] == 'Denoise' or category['name'] == 'ViewportSettings']
        for category in viewport_render_setting_categories:
            del category['houdini']
        generate_houdini_ds(install_path, 'Viewport', viewport_render_setting_categories)


def generate(install, generate_ds_files):
    generate_render_setting_files(install, generate_ds_files)
