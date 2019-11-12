#!/usr/bin/python2.7
import os
import re
import glob
import platform
import argparse
import subprocess

from distutils import util

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
            return util.strtobool(raw_input())
        except ValueError:
            print('yes or no')

def install_package(install_dir, package):
    if query_agreement(install_dir):
        ret = subprocess.call(['sh', package[0], '--exclude-subdir', '--prefix={}'.format(install_dir)])
        if ret != 0:
            print('Could not install package. Try running with root privileges')

def install_to_houdini_dir(hfs, package):
    if macOS():
        hfs = os.path.dirname(hfs)
    install_package(hfs, package)

valid_platforms = ['Linux', 'Darwin']
valid_targets = ['Houdini']
package_pattern = re.compile(r'hdRpr-(?P<target>.*?)-.*-(?P<platform_>.*?).sh', re.VERBOSE)
def get_package_from_path(path):
    match = package_pattern.match(path)
    platform_ = match.group('platform_')
    if platform_ not in valid_platforms:
        print('"{}": unknown platform. Skipping.'.format(path))
        return None

    target = match.group('target')
    if target not in valid_targets:
        print('"{}": unknown target. Skipping.'.format(path))
        return None
    return (path, target)

script_description = (
'''
Script that allows to easily install hdRpr across Windows, Ubuntu and macOS.
To install hdRpr into specific location pass install_dir.
''')

parser = argparse.ArgumentParser(
    prog='hdRpr installer',
    description=script_description,
    formatter_class=argparse.RawDescriptionHelpFormatter)

parser.add_argument('install_dir', action='store_const', const=None,
                    help='Directory where hdRpr will be installed. Typically it\'s going to be Houdini installation directory')
parser.add_argument('package_path', action='store_const', const=None,
                    help='Path to hdRpr package. If not set, package will be automatically finded in script folder')

args = parser.parse_args()

if args.package_path:
    package = get_package_from_path(args.package_path)
    if not package:
        exit()
else:
    packages = []

    for path in glob.glob('hdRpr*.sh'):
        if os.path.isfile(path):
            package = get_package_from_path(path)
            if package:
                packages.append(package)

    num_packages = len(packages)
    if num_packages == 0:
        print('Could not find any packages to install')
        exit()
    elif num_packages == 1:
        package = packages[0]
    else:
        print('not implemented')
        exit()

print('Installing "{}"'.format(package[0]))

if args.install_dir:
    install_package(args.install_dir, package)
elif 'HFS' in os.environ:
    install_to_houdini_dir(os.environ['HFS'], package)
else:
    if windows():
        hfs_search_paths = ['C:\\Program Files\\Side Effects Software\\Houdini*']
    elif linux():
        hfs_search_paths = ['/opt/hfs*']
    else:
        hfs_search_paths = ['/Applications/Houdini/Houdini*/Frameworks/Houdini.framework/Versions/Current/Resources']

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
        while True:
            print('Few Houdini installation has been found. Select the desired one.')
            i = 0
            for path in install_variants:
                print('{}. "{}"'.format(i, path))
                i += 1
            print('Enter number.')
            try:
                i = int(raw_input())
                if i >= 0 and i < len(install_variants):
                    number = i
                    break
            except ValueError:
                pass
            print('Incorrect number.')

        install_to_houdini_dir(install_variants[number], package)
