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

from houdiniDsGenerator import generate_houdini_ds

light_settings = [
    {
        'name': 'Light',
        'settings': [
            {
            'name': 'rpr:object:visibility:camera',
                'ui_name': 'Camera Visibility',
                'defaultValue': False,
                'help': 'Used to show or hide an object from the camera.\\n' \
                        'Disabling camera visibility is the most optimized way to hide ' \
                        'an object from the camera but still have it cast shadows, ' \
                        'be visible in reflections, etc.'
            },
            {
                'name': 'rpr:object:visibility:shadow',
                'ui_name': 'Shadow Visibility',
                'defaultValue': False,
                'help': 'Shadow visibility controls whether to show or to hide shadows cast by ' \
                        'the object onto other surfaces (including reflected shadows and shadows ' \
                        'seen through transparent objects). You might need this option to hide shadows ' \
                        'that darken other objects in the scene or create unwanted effects.'
            },
            {
                'name': 'rpr:object:visibility:reflection',
                'ui_name': 'Reflection Visibility',
                'defaultValue': True,
                'help': 'Reflection visibility makes an object visible or invisible in reflections on ' \
                        'specular surfaces. Note that hiding an object from specular reflections keeps ' \
                        'its shadows (including reflected shadows) visible.'
            },
            {
                'name': 'rpr:object:visibility:glossyReflection',
                'ui_name': 'Glossy Reflection Visibility',
                'defaultValue': True
            },
            {
                'name': 'rpr:object:visibility:refraction',
                'ui_name': 'Refraction Visibility',
                'defaultValue': True,
                'help': 'Refraction visibility makes an object visible or invisible when seen through ' \
                        'transparent objects. Note that hiding an object from refractive rays keeps its ' \
                        'shadows (including refracted shadows) visible.'
            },
            {
                'name': 'rpr:object:visibility:glossyRefraction',
                'ui_name': 'Glossy Refraction Visibility',
                'defaultValue': True
            },
            {
                'name': 'rpr:object:visibility:diffuse',
                'ui_name': 'Diffuse Visibility',
                'defaultValue': True,
                'help': 'Diffuse visibility affects indirect diffuse rays and makes an object visible ' \
                        'or invisible in reflections on diffuse surfaces.'
            },
            {
                'name': 'rpr:object:visibility:transparent',
                'ui_name': 'Transparent Visibility',
                'defaultValue': True
            },
            {
                'name': 'rpr:object:visibility:light',
                'ui_name': 'Light Visibility',
                'defaultValue': True
            },
            {
                'name': 'rpr:light:intensity:sameWithKarma',
                'ui_name': 'Make Intensity Same With Karma',
                'defaultValue': False
            }
        ]
    }
]

def generate(install, generate_ds_files):
    if generate_ds_files:
        generate_houdini_ds(install, 'Light', light_settings)
