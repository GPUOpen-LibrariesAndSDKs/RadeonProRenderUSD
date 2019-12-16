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

def query_agreement(install_dir):
    print('hdRpr will be installed to "{}"'.format(install_dir))
    print('Do you agree? [y/n]')
    while True:
        try:
            return util.strtobool(input())
        except ValueError:
            print('yes or no')

def install_package(install_dir, package):
    if query_agreement(install_dir):
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

def install_to_houdini_dir(hfs, package):
    if macOS():
        hfs = os.path.abspath(os.path.join(hfs, os.pardir))
    install_package(hfs, package)

valid_targets = ['Houdini']
package_pattern = re.compile(r'hdRpr-(?P<target>.*?)-.*-(?P<platform_>.*?).tar.gz', re.VERBOSE)
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
    if windows():
        hfs_search_paths = ['C:\\Program Files\\Side Effects Software\\Houdini 18*']
    elif linux():
        hfs_search_paths = ['/opt/hfs18*']
    else:
        hfs_search_paths = ['/Applications/Houdini/Houdini18*/Frameworks/Houdini.framework/Versions/Current/Resources']

    install_variants = set()
    for search_path in hfs_search_paths:
        for path in glob.glob(search_path):
            if os.path.islink(path):
                path = os.path.realpath(path)
            if os.path.isdir(path):
                setup_script_path = os.path.join(path, 'houdini_setup')
                if os.path.isfile(setup_script_path):
                    install_variants.add(path)

    install_variants = list(install_variants)

    if len(install_variants) == 0:
        print('Could not find any Houdini installation directories. Specify directory using --install_dir argument')
    elif len(install_variants) == 1:
        install_to_houdini_dir(install_variants[0], package)
    else:
        print('Few Houdini installation has been found. Select the desired one.')
        for i, path in enumerate(install_variants):
            print('{}. "{}"'.format(i, path))
        print('Enter number.')
        try:
            install_to_houdini_dir(install_variants[int(input())], package)
        except (ValueError, IndexError) as e:
            print('Installation canceled: incorrect number.')
