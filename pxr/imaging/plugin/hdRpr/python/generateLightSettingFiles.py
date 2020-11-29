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

light_visibility_flag_settings = visibility_flag_settings
for setting in light_visibility_flag_settings:
    if setting['name'] == 'primvars:rpr:visibilityPrimary' or \
       setting['name'] == 'primvars:rpr:visibilityShadow' or \
       setting['name'] == 'primvars:rpr:visibilityLight':
        setting['defaultValue'] = False

light_settings = [
    {
        'name': 'Light',
        'settings': light_visibility_flag_settings
    }
]

def generate(install, generate_ds_files):
    if generate_ds_files:
        generate_houdini_ds(install, 'Light', light_settings)
