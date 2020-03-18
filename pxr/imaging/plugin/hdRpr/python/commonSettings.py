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

visibility_mask_setting = {
    'name': 'primvars:rpr:visibilityMask',
    'ui_name': 'Render Visibility',
    'defaultValue': '*',
    'hints': [
        ('*', 'Visible to all'),
        ('primary', 'Visible only to primary rays'),
        ('primary,shadow', 'Visible only to primary and shadow rays'),
        ('-primary', 'Invisible to primary rays'),
        ('-primary,-shadow,-light', 'Invisible emissive light'),
        ('', 'Invisible'),
    ],
    'help': 'The visibility mask is a comma-separated list of inclusive or exclusive ray visibility flags.\\n' \
            'For example, \"primary,shadow\" means that object is visible only for primary and shadow rays;\\n' \
            '\"-primary,-light,-shadow\" - visible for all ray types except primary, light and shadow rays.\\n' \
            'Mixing inclusion and exclusion do not make sense.\\n' \
            'Exclusion flag will be prioritized in case of mixing, i.e. inclusion flags ignored.\\n' \
            'Possible values: primary, shadow, reflection, refraction, transparent, diffuse, glossyReflection, glossyRefraction, light'
}

