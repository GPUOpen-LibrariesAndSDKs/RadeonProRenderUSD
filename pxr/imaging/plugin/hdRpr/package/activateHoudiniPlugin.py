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
import re
import json
import glob
import argparse
import platform

def get_houdini_user_pref_dir(hver):
    houdini_user_pref_dir = os.getenv('HOUDINI_USER_PREF_DIR')
    if houdini_user_pref_dir:
        if '__HVER__' in houdini_user_pref_dir:
            houdini_user_pref_dir.replace(' __HVER__', hver)
            return houdini_user_pref_dir
        else:
            print('Invalid HOUDINI_USER_PREF_DIR specified')
            exit(1)

    def get_houdini_user_pref_dir_from_parent_dir(parent_dir):
        pref_dir = os.path.join(parent_dir, 'houdini' + hver)
        if os.path.exists(pref_dir):
            return pref_dir
        return None

    home = os.getenv('HOME')
    if home:
        houdini_user_pref_dir = get_houdini_user_pref_dir_from_parent_dir(home)
        if houdini_user_pref_dir:
            return houdini_user_pref_dir

    if platform.system() == 'Windows':
        userprofile = os.getenv('USERPROFILE')
        if not userprofile:
            print('Set HOUDINI_USER_PREF_DIR environment variable')
            exit(1)

        documents_dir = os.path.join(userprofile, 'documents')
        houdini_user_pref_dir = get_houdini_user_pref_dir_from_parent_dir(documents_dir)
        if houdini_user_pref_dir:
            return houdini_user_pref_dir
    elif platform.system() == 'Darwin':
        if home:
            macos_fallback_dir = '{}/Library/Preferences/houdini/{}'.format(home, hver)
            if os.path.exists(macos_fallback_dir):
                return macos_fallback_dir

    print('Can not determine HOUDINI_USER_PREF_DIR. Please check your environment and specify HOUDINI_USER_PREF_DIR')
    exit(1)


parser = argparse.ArgumentParser(formatter_class=argparse.RawDescriptionHelpFormatter)

parser.add_argument('-d', '--deactivate', default=False, action='store_true')
parser.add_argument('-p', '--plugin_path', type=str,
                    help='Path to hdRpr plugin. If not set, the plugin will be automatically found in the current working directory')

args = parser.parse_args()

houdini_user_pref_dir = get_houdini_user_pref_dir('18.0')

houdini_packages_dir = os.path.join(houdini_user_pref_dir, 'packages')
os.makedirs(houdini_packages_dir, exist_ok=True)

package_desc_filepath = os.path.join(houdini_packages_dir, 'hdRpr.json')

if args.deactivate:
    if os.path.exists(package_desc_filepath):
        os.remove(package_desc_filepath)
        print('hdRpr has been deactivated')
    else:
        print('hdRpr plugin is not active')
else:
    if args.plugin_path:
        if not os.path.exists(args.plugin_path):
            print('Invalid plugin path')
            exit(1)

        plugin_dir = args.plugin_path
    else:
        def is_valid_plugin_dir(path):
            plugin_info_path = os.path.join(path, 'plugin/usd/hdRpr/resources/plugInfo.json')
            return os.path.isfile(plugin_info_path)

        if is_valid_plugin_dir('.'):
            plugin_dir = '.'
        else:
            valid_targets = ['Houdini']
            plugin_pattern = re.compile(r'hdRpr-.*-(?P<target>.*?)-.*-(?P<platform_>.*?)', re.VERBOSE)
            for path in glob.glob('hdRpr*'):
                if os.path.isdir(path):
                    match = plugin_pattern.match(path)

                    target = match.group('target')
                    if target not in valid_targets:
                        print('"{}": unknown target. Skipping.'.format(path))
                        continue
                    if is_valid_plugin_dir(path):
                        plugin_dir = path
                        break
            else:
                print('Can not find hdRpr plugin. Specify --plugin_path explicitly')
                exit(1)


    plugin_dir = os.path.abspath(plugin_dir)
    plugin_dir = plugin_dir.replace('\\', '/')

    env = [
        {"RPR": plugin_dir},
        {"HOUDINI_PATH": "$RPR/houdini"}
    ]
    if platform.system() == 'Windows':
        env += [{"PATH": "$RPR/bin"}]

    with open(package_desc_filepath, 'w') as desc_file:
        desc_file.write(json.dumps({"env":env}))
    print('hdRpr has been activated')
