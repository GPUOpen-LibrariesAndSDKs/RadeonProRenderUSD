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

from commonSettings import visibility_mask_setting
from houdiniDsGenerator import generate_houdini_ds

light_visibility_mask_setting = visibility_mask_setting
light_visibility_mask_setting['defaultValue'] = '-primary,-shadow,-light'
light_settings = [
    {
        'name': 'Light',
        'settings': [
            light_visibility_mask_setting
        ]
    }
]

if __name__ == "__main__":
    p = argparse.ArgumentParser()
    p.add_argument("install", help="The install root for generated files.")
    p.add_argument("--generate_ds_files", default=False, action='store_true')
    args = p.parse_args()

    if args.generate_ds_files:
        generate_houdini_ds(args.install, 'Light', light_settings)
