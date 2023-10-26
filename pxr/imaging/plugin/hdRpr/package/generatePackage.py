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
import stat
import sys
import shlex
import shutil
import tarfile
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

def on_rm_error(func, path, exc_info):
    # path contains the path of the file that couldn't be removed
    # let's just assume that it's read-only and unlink it.
    os.chmod(path, stat.S_IWRITE)
    os.unlink(path)

self_dir = os.path.abspath(self_path())
hdrpr_root_path = os.path.abspath(os.path.join(self_path(), '../../../../..'))

parser = argparse.ArgumentParser(formatter_class=argparse.RawDescriptionHelpFormatter)
parser.add_argument('-i', '--src_dir', type=str, default=hdrpr_root_path)
parser.add_argument('-o', '--output_dir', type=str, default='.')
parser.add_argument('-c', '--config', type=str, default='Release')
parser.add_argument('--cmake_options', type=str, default='')
parser.add_argument('--disable_auto_cleanup', default=False, action='store_true')
parser.add_argument('--build_resolver', default=False, help="Windows only", action='store_true')
parser.add_argument('--resolver_cmake_options', type=str, default='', help="Windows only")
args = parser.parse_args()

output_dir = os.path.abspath(args.output_dir)

package_dir = '_package'
cmake_configure_cmd = ['cmake', '-DCMAKE_INSTALL_PREFIX='+package_dir, '-DDUMP_PACKAGE_FILE_NAME=ON']
cmake_configure_cmd += shlex.split(args.cmake_options)
if args.build_resolver:
    cmake_configure_cmd += ["-DRESOLVER_SUPPORT=ON"]
cmake_configure_cmd += ['..']

build_dir = 'build_generatePackage_tmp_dir'
if not args.disable_auto_cleanup and os.path.isdir(build_dir):
    shutil.rmtree(build_dir, onerror=on_rm_error)

os.makedirs(build_dir, exist_ok=True)

with current_working_directory(build_dir):
    configure_output = subprocess.check_output(cmake_configure_cmd).decode()
    print(configure_output)

    package_name = 'hdRpr'
    for line in reversed(configure_output.splitlines()):
        if line.startswith('-- PACKAGE_FILE_NAME'):
            package_name = line[len('-- PACKAGE_FILE_NAME: '):]
            break

    return_code = subprocess.call(['cmake', '--build', '.', '--config', args.config, '--target', 'install', '--', format_multi_procs(get_cpu_count())])
    if return_code != 0:
        exit(return_code)

    if args.build_resolver:
        from buildResolver import build_resolver
        build_resolver(os.path.abspath(package_dir), args.resolver_cmake_options, format_multi_procs(get_cpu_count()))

    with tarfile.open('tmpPackage.tar.gz', 'w:gz') as tar:
        tar.add(package_dir, package_name)
    output_package = os.path.join(output_dir, package_name+'.tar.gz')
    shutil.copyfile('tmpPackage.tar.gz', output_package)
print('{} has been created'.format(os.path.relpath(output_package)))

if not args.disable_auto_cleanup:
    shutil.rmtree(build_dir, onerror=on_rm_error)
