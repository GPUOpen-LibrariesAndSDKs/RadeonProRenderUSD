import os
import glob

import hou
from rpr import RprUsd

def _set_cache_dir(type, start_directory, setter):
    directory = hou.ui.selectFile(
        title='RPR {} Cache Directory'.format(type),
        start_directory=start_directory.replace('\\', '/'),
        file_type=hou.fileType.Directory,
        chooser_mode=hou.fileChooserMode.Write)
    if directory:
        setter(hou.expandString(directory))

def set_texture_cache_dir():
    _set_cache_dir('Texture', RprUsd.Config.GetTextureCacheDir(), RprUsd.Config.SetTextureCacheDir)

def set_kernel_cache_dir():
    _set_cache_dir('Kernel', RprUsd.Config.GetKernelCacheDir(), RprUsd.Config.SetKernelCacheDir)


def _clear_cache(cache_dir):
    num_files_removed = 0
    for pattern in ('*.bin.check', '*.bin', '*.cache'):
        for cache_file in glob.iglob(os.path.join(cache_dir, pattern)):
            os.remove(cache_file)
            num_files_removed += 1
    print('RPR: removed {} cache files'.format(num_files_removed))

def clear_texture_cache():
    _clear_cache(RprUsd.Config.GetTextureCacheDir())

def clear_kernel_cache():
    _clear_cache(RprUsd.Config.GetKernelCacheDir())
