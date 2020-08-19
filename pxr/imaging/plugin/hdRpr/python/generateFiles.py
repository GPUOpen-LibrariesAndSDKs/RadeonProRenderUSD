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
import subprocess
import argparse
import sys
import os

if __name__ == "__main__":
    p = argparse.ArgumentParser()
    p.add_argument('scripts_dir', help="Directory with scripts")
    p.add_argument("install", help="The install root for generated files.")
    p.add_argument("--houdini_root", help="The install root for generated files.")
    p.add_argument('--hidden-render-qualities', default='', type=str)
    args = p.parse_args()

    generate_ds_files = []
    if args.houdini_root:
        # Generator of DialogScript files requires access to houdini's python modules
        generate_ds_files = ['--generate_ds_files']
        os.environ['PATH'] = os.environ.get('PATH', '') + os.pathsep + os.path.join(args.houdini_root, 'bin')
        os.environ['PYTHONPATH'] = os.environ.get('PYTHONPATH', '') + os.pathsep + os.path.join(os.path.join(args.houdini_root, 'houdini'), 'python2.7libs')

    subprocess.check_call([sys.executable, os.path.join(args.scripts_dir, 'generateLightSettingFiles.py'), args.install] + generate_ds_files)
    subprocess.check_call([sys.executable, os.path.join(args.scripts_dir, 'generateGeometrySettingFiles.py'), args.install] + generate_ds_files)
    subprocess.check_call([
        sys.executable, os.path.join(args.scripts_dir, 'generateRenderSettingFiles.py'),
        '--hidden-render-qualities', args.hidden_render_qualities,
        args.install] + generate_ds_files)
