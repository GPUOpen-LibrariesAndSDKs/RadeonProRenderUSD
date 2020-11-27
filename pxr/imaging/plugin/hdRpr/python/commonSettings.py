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

visibility_flag_settings = [
    {
        'name': 'primvars:rpr:visibilityPrimary',
        'ui_name': 'Camera Visibility',
        'defaultValue': True,
        'help': 'Used to show or hide an object from the camera.\\n' \
                'Disabling camera visibility is the most optimized way to hide ' \
                'an object from the camera but still have it cast shadows, ' \
                'be visible in reflections, etc.'
    },
    {
        'name': 'primvars:rpr:visibilityShadow',
        'ui_name': 'Shadow Visibility',
        'defaultValue': True,
        'help': 'Shadow visibility controls whether to show or to hide shadows cast by ' \
                'the object onto other surfaces (including reflected shadows and shadows ' \
                'seen through transparent objects). You might need this option to hide shadows ' \
                'that darken other objects in the scene or create unwanted effects.'
    },
    {
        'name': 'primvars:rpr:visibilityReflection',
        'ui_name': 'Reflection Visibility',
        'defaultValue': True,
        'help': 'Reflection visibility makes an object visible or invisible in reflections on ' \
                'specular surfaces. Note that hiding an object from specular reflections keeps ' \
                'its shadows (including reflected shadows) visible.'
    },
    {
        'name': 'primvars:rpr:visibilityGlossyReflection',
        'ui_name': 'Glossy Reflection Visibility',
        'defaultValue': True
    },
    {
        'name': 'primvars:rpr:visibilityRefraction',
        'ui_name': 'Refraction Visibility',
        'defaultValue': True,
        'help': 'Refraction visibility makes an object visible or invisible when seen through ' \
                'transparent objects. Note that hiding an object from refractive rays keeps its ' \
                'shadows (including refracted shadows) visible.'
    },
    {
        'name': 'primvars:rpr:visibilityGlossyRefraction',
        'ui_name': 'Glossy Refraction Visibility',
        'defaultValue': True
    },
    {
        'name': 'primvars:rpr:visibilityDiffuse',
        'ui_name': 'Diffuse Visibility',
        'defaultValue': True,
        'help': 'Diffuse visibility affects indirect diffuse rays and makes an object visible ' \
                'or invisible in reflections on diffuse surfaces.'
    },
    {
        'name': 'primvars:rpr:visibilityTransparent',
        'ui_name': 'Transparent Visibility',
        'defaultValue': True
    },
    {
        'name': 'primvars:rpr:visibilityLight',
        'ui_name': 'Light Visibility',
        'defaultValue': True
    }
]

class SettingValue(object):
    def __init__(self, key, ui_name=None, enable_py_condition=None):
        self._key = key
        self._ui_name = ui_name
        self.enable_py_condition = enable_py_condition

    def __eq__(self, obj):
        return self._key == obj

    def __ne__(self, obj):
        return not self == obj

    def get_ui_name(self):
        return self._ui_name if self._ui_name else self._key

    def get_key(self):
        return self._key.replace(' ', '')

