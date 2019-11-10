import os
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

def install_package(path):
    if windows():
        pass
    else:
        subprocess.call(['sh', 'hdRpr.sh', '--exclude-subdir', '--prefix={}'.format(path)])

def query_agreement(install_dir):
    print('hdRpr will be installed to "()"'.format(install_dir))
    print('Do you agree? [y/n]')
    while True:
        try:
            return util.strtobool(raw_input())
        except:
            print('yes or no')

def install_to_houdini_dir(hfs):
    if macOS():
        path = os.path.dirname(path)
    install_package(path)

script_description = (
'''
Script that allows to easily install hdRpr across Windows, Ubuntu and macOS.
To install hdRpr into specific location pass install_dir.
''')

parser = argparse.ArgumentParser(
    prog='hdRpr installer',
    description=script_description,
    formatter_class=argparse.RawDescriptionHelpFormatter)

parser.add_argument("install_dir", type=str,
                    help="Directory where hdRpr will be installed. Typically it's going to be Houdini installation directory")

args = parser.parse_args()

if args.install_dir is not None:
    install_package(args.install_dir)
elif 'HFS' in os.environ:
    install_to_houdini_dir(os.environ['HFS'])
else:

    if windows():
        hfs_search_paths = ['C:\\Program Files\\Side Effects Software\\Houdini*']
    elif linux():
        hfs_search_paths = ['/opt/hfs*']
    else:
        hfs_search_paths = ['/Applications/Houdini/Houdini*/Frameworks/Houdini.framework/Versions/Current/Resources']

    install_variants = []
    for search_path in hfs_search_paths:
        for path in glob.glob(search_path):
            if os.path.islink(path):
                path = os.path.realpath(path)
            if os.path.isdir(path):
                setup_script_path = os.path.join(path, 'houdini_setup')
                if os.path.isfile(setup_script_path):
                    print(path)
                    install_variants.append(path)

    if len(install_variants) == 0:
        print('Could not find any Houdini installation directories. Specify directory using --install_dir argument')
    elif len(install_variants) == 1:
        install_to_houdini_dir(install_variants[0])
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
                if i >= 0 or i < len(install_variants):
                    install_to_houdini_dir(install_variants[i])
            except:
                pass
            print('Incorrect number.')
