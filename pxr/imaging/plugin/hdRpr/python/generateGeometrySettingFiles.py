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

from commonSettings import visibility_flag_settings
from houdiniDsGenerator import generate_houdini_ds

geometry_settings = [
    {
        'name': 'Mesh',
        'settings': [
            {
                'name': 'primvars:rpr:id',
                'ui_name': 'ID',
                'defaultValue': 0,
                'minValue': 0,
                'maxValue': 1 ** 16
            },
            {
                'name': 'primvars:rpr:subdivisionLevel',
                'ui_name': 'Subidivision Level',
                'defaultValue': 0,
                'minValue': 0,
                'maxValue': 7
            },
            {
                'name': 'primvars:rpr:ignoreContour',
                'ui_name': 'Ignore Contour',
                'defaultValue': False,
                'help': 'Whether to extract contour for a mesh or not'
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
