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
import sys
import shutil
import platform
import argparse
import subprocess
import contextlib
import multiprocessing

def get_cpu_count():
    try:
        return multiprocessing.cpu_count()
    except NotImplementedError:
        return 1

def format_multi_procs(numJobs):
    tag = '/M:' if platform.system() == 'Windows' else '-j'
    return "{tag}{procs}".format(tag=tag, procs=numJobs)

def self_path():
    path = os.path.dirname(sys.argv[0])
    if not path:
        path = '.'
    return path

@contextlib.contextmanager
def current_working_directory(dir):
    curdir = os.getcwd()
    os.chdir(dir)
    try: yield
    finally: os.chdir(curdir)

def get_package_path(build_dir, config):
    with current_working_directory(build_dir):
        subprocess.call(['cmake', '--build', '.', '--config', config, '--', format_multi_procs(get_cpu_count())])
        output = subprocess.check_output(['cpack', '-C', config])
        print(output)

        pattern = re.compile(r'CPack:\ -\ package:\ (?P<package_path>.*?)\ generated.', re.VERBOSE)
        for line in str(output).split('\n'):
            match = pattern.match(line.rstrip())
            if match:
                package_path = match.group('package_path')
                return package_path

self_dir = os.path.abspath(self_path())
hdrpr_root_path = os.path.abspath(os.path.join(self_path(), '../../../../..'))

parser = argparse.ArgumentParser(formatter_class=argparse.RawDescriptionHelpFormatter)
parser.add_argument('-b', '--build_dir', type=str, default='{}/build'.format(hdrpr_root_path))
parser.add_argument('-o', '--output_package_path', type=str, default=None)
parser.add_argument('-c', '--config', type=str, default='Release')
args = parser.parse_args()

package_path = get_package_path(args.build_dir, args.config)
if package_path:
    package_basename = os.path.basename(package_path)

    output_package_path = args.output_package_path
    if not output_package_path:
        package_name = package_basename
        package_ext = '.tar.gz'
        if package_basename.endswith(package_ext):
            package_name = package_basename[:-len(package_ext)]
        output_package_path = '{}-package.zip'.format(package_name)

    if not os.path.exists('tmp_dir'):
        os.mkdir('tmp_dir')
    with current_working_directory('tmp_dir'):
        if platform.system() != 'Linux':
            installer_content = 'python install.py\n'
            if platform.system() == "Windows":
                installer_ext = 'bat'
                installer_content = 'pushd %~dp0\n' + installer_content
                installer_content += 'pause\n'
            if platform.system() == "Darwin":
                installer_content = 'cd $(dirname $0)\n' + installer_content
                installer_ext = 'command'
            install_file_path = 'install.' + installer_ext
            install_file = open(install_file_path, 'w')
            install_file.write(installer_content)
            install_file.close()

            if platform.system() == 'Darwin':
                os.chmod(install_file_path, 0o755)

        shutil.copyfile(package_path, package_basename)
        shutil.copyfile(self_dir + '/install.py', 'install.py')
        shutil.copyfile(hdrpr_root_path + '/INSTALL.md', 'INSTALL.md')
        shutil.copyfile(hdrpr_root_path + '/LICENSE.md', 'LICENSE.md')

    shutil.make_archive('tmp_package', 'zip', 'tmp_dir', './')
    shutil.copyfile('tmp_package.zip', output_package_path)

    shutil.rmtree('tmp_dir')
    os.remove('tmp_package.zip')
