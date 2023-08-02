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
import argparse

from commonSettings import visibility_flag_settings, SettingValue
from houdiniDsGenerator import generate_houdini_ds

geometry_settings = [
    {
        'name': 'Mesh',
        'settings': [
            {
                'name': 'primvars:rpr:object:id',
                'ui_name': 'ID',
                'defaultValue': 0,
                'minValue': 0,
                'maxValue': 2 ** 16
            },
            {
                'name': 'primvars:rpr:mesh:subdivisionLevel',
                'ui_name': 'Subidivision Level',
                'defaultValue': 0,
                'minValue': 0,
                'maxValue': 7
            },
            {
                'name': 'primvars:rpr:mesh:subdivisionCreaseWeight',
                'ui_name': 'Crease Weight',
                'defaultValue': 0.0,
                'minValue': 0.0,
                'maxValue': 3.0
            },
            {
                'name': '$interpolateBoundary',
                'ui_name': 'Boundary',
                'defaultValue': 'edgeAndCorner',
                'values': [
                    SettingValue('edgeAndCorner'),
                    SettingValue('edgeOnly')
                ]
            },
            {
                'name': 'primvars:rpr:mesh:ignoreContour',
                'ui_name': 'Ignore Contour',
                'defaultValue': False,
                'help': 'Whether to extract contour for a mesh or not'
            },
            {
                'name': 'primvars:rpr:object:assetName',
                'ui_name': 'Cryptomatte Name',
                'defaultValue': '',
                'c_type': 'std::string',
                'help': 'String used to generate cryptomatte ID. If not specified, the path to a primitive used.'
            },
            {
                'name': 'primvars:rpr:object:deform:samples',
                'ui_name': 'Geometry Time Samples',
                'defaultValue': 1,
                'minValue': 1,
                'maxValue': 2 ** 16,
                'help': 'The number of sub-frame samples to compute when rendering deformation motion blur over the shutter open time. The default is 1 (sample only at the start of the shutter time), giving no deformation blur by default. If you want rapidly deforming geometry to blur properly, you must increase this value to 2 or more. Note that this value is limited by the number of sub-samples available in the USD file being rendered.'
            },
            {
                'folder': 'Visibility Settings',
                'settings': visibility_flag_settings
            }
        ]
    }
]

def generate(install, generate_ds_files):
    if generate_ds_files:
        generate_houdini_ds(install, 'Geometry', geometry_settings)
