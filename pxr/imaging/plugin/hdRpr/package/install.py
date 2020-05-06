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
#!/usr/bin/python2.7
import os
import re
import glob
import platform
import argparse
import tarfile

from distutils import util

# handle input for pyton 2 vs 3
try: 
    input = raw_input
except NameError: 
    input = input

def windows():
    return platform.system() == "Windows"
def linux():
    return platform.system() == "Linux"
def macOS():
    return platform.system() == "Darwin"

if windows():
    hfs_search_paths = ['C:\\Program Files\\Side Effects Software\\Houdini 18*']
elif linux():
    hfs_search_paths = ['/opt/hfs18*']
else:
    hfs_search_paths = ['/Applications/Houdini/Houdini18*/Frameworks/Houdini.framework/Versions/Current/Resources']

def is_valid_install_dir(path):
    if not os.path.isdir(path):
        return False

    setup_script_path = os.path.join(path, 'houdini_setup')
    if not os.path.isfile(setup_script_path):
        return False

    test_dir = os.path.join(path, 'create_access_test')
    try:
        os.mkdir(test_dir)
    except OSError:
        print('No permission to create directories in {}. Run with root privileges to install here.'.format(path))
        return False
    else:
        os.rmdir(test_dir)
    return True

def query_agreement(install_dir):
    print('hdRpr will be installed to "{}"'.format(install_dir))
    print('Do you agree? [y/n]')
    while True:
        try:
            return util.strtobool(input())
        except ValueError:
            print('yes or no')

def install_package(install_dir, package, force=False):
    if force or query_agreement(install_dir):
        try:
            tar_file = tarfile.open(package[0], 'r:gz')
            tar_file.extractall(install_dir)
            for file in tar_file.getnames():
                path = '{install_dir}/{file}'.format(install_dir=install_dir, file=file)
                if not os.path.isdir(path):
                    print('-- Installing: ' + path)
            print('Successfully installed')
        except Exception as e:
            print(e)
            print('Could not install package. Try running with root privileges')

def install_to_houdini_dir(hfs, package, force=False):
    if macOS():
        hfs = os.path.abspath(os.path.join(hfs, os.pardir))
    install_package(hfs, package, force)

def install_to_custom_dir(package):
    print('Please enter Houdini\'s path (e.g. {}):'.format(hfs_search_paths[0]))
    while True:
        path = input()
        if is_valid_install_dir(path):
            install_to_houdini_dir(path, package, force=True)
            break
        print('Invalid path. Please try again')

valid_targets = ['Houdini']
package_pattern = re.compile(r'hdRpr-.*-(?P<target>.*?)-.*-(?P<platform_>.*?).tar.gz', re.VERBOSE)
def get_package_from_path(path):
    match = package_pattern.match(path)

    target = match.group('target')
    if target not in valid_targets:
        print('"{}": unknown target. Skipping.'.format(path))
        return None
    return (path, target)

script_description = (
'''
The script allows to easily install hdRpr across Windows, Ubuntu, and macOS.
To install hdRpr into specific location pass install_dir.
''')

parser = argparse.ArgumentParser(
    description=script_description,
    formatter_class=argparse.RawDescriptionHelpFormatter)

parser.add_argument('-i', '--install_dir', type=str,
                    help='Directory where hdRpr will be installed. Typically it\'s going to be Houdini installation directory')
parser.add_argument('-p', '--package_path', type=str,
                    help='Path to hdRpr package. If not set, the package will be automatically found in a script folder')

args = parser.parse_args()

if args.package_path:
    package = get_package_from_path(args.package_path)
    if not package:
        exit()
else:
    packages = []

    for path in glob.glob('hdRpr*.tar.gz'):
        if os.path.isfile(path):
            package = get_package_from_path(path)
            if package:
                packages.append(package)

    num_packages = len(packages)
    if num_packages == 0:
        print('Could not find any packages to install')
        exit()
    else:
        if num_packages > 1:
            print('Found few packages: {}'.format(packages))        
        package = packages[0]

print('Installing "{}"'.format(package[0]))

if args.install_dir:
    install_package(args.install_dir, package)
elif 'HFS' in os.environ:
    install_to_houdini_dir(os.environ['HFS'], package)
else:
    install_variants = set()
    for search_path in hfs_search_paths:
        for path in glob.glob(search_path):
            if os.path.islink(path):
                path = os.path.realpath(path)
            if is_valid_install_dir(path):
                install_variants.add(path)

    install_variants = list(install_variants)

    if len(install_variants) == 0:
        print('Could not find any Houdini installation directories.')
        install_to_custom_dir(package)
    elif len(install_variants) == 1:
        install_to_houdini_dir(install_variants[0], package)
    else:
        print('Few Houdini installation has been found. Select the desired one.')
        for i, path in enumerate(install_variants, start=1):
            print('{}. "{}"'.format(i, path))
        custom_path_idx = len(install_variants)
        print('{}. Custom path'.format(custom_path_idx))
        print('Enter number.')
        try:
            idx = int(input()) - 1
            if idx == custom_path_idx:
                install_to_custom_dir(package)
            else:
                install_to_houdini_dir(install_variants[idx], package, force=True)
        except (ValueError, IndexError) as e:
            print('Installation canceled: incorrect number.')
