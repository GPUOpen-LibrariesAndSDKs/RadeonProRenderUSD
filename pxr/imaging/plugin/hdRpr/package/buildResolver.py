import os
import shlex
import subprocess

RESOLVER_REPO_URL = "https://github.com/Radeon-Pro/RenderStudioKit.git"
RESOLVER_REPO_TAG = "1f06af8"


def clone_repository(workdir):
    subprocess.call(["git", "clone", RESOLVER_REPO_URL])
    os.chdir("RenderStudioKit")
    subprocess.call(["git", "checkout", RESOLVER_REPO_TAG])
    subprocess.call(["git", "submodule", "update", "--init", "--recursive"])
    os.makedirs("build")
    os.chdir(workdir)


def build(workdir, install_prefix, cmake_args, jobs):
    os.chdir("RenderStudioKit/build")
    cmake_configure_cmd = ['cmake', '-DCMAKE_INSTALL_PREFIX=' + install_prefix, "-DHOUDINI_SUPPORT=ON",
                           "-DPXR_ENABLE_PYTHON_SUPPORT=ON"]
    cmake_configure_cmd += shlex.split(cmake_args)
    cmake_configure_cmd += ['..']
    subprocess.call(cmake_configure_cmd)
    return_code = subprocess.call(['cmake', '--build', '.', '--config', 'Release', '--target', 'install', '--', jobs])
    if return_code != 0:
        exit(return_code)
    os.chdir(workdir)


def build_resolver(install_prefix, cmake_args, jobs):
    workdir = os.getcwd()
    print(os.path.abspath(workdir))
    print("Installing RenderStudioKit")
    print("Cloning repository")
    clone_repository(workdir)
    print("Building resolver")
    build(workdir, install_prefix, cmake_args, jobs)
