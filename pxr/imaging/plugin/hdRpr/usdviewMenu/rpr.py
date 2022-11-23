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
from pxr import Tf
from pxr.Plug import Registry
from pxr.Usdviewq.plugin import PluginContainer
from pxr.Usdviewq.qt import QtWidgets, QtCore

from ctypes import cdll, c_void_p, c_char_p, cast
from ctypes.util import find_library

import os
import glob

from rpr import RprUsd

def setCacheDir(usdviewApi, type, startDirectory, setter):
    directory = QtWidgets.QFileDialog.getExistingDirectory(
        usdviewApi._UsdviewApi__appController._mainWindow,
        caption='RPR {} Cache Directory'.format(type),
        dir=startDirectory)

    if directory:
        setter(directory)

def SetTextureCacheDir(usdviewApi):
    setCacheDir(usdviewApi, 'Texture', RprUsd.Config.GetTextureCacheDir(), RprUsd.Config.SetTextureCacheDir)
def SetKernelCacheDir(usdviewApi):
    setCacheDir(usdviewApi, 'Kernel', RprUsd.Config.GetKernelCacheDir(), RprUsd.Config.SetKernelCacheDir)

def clearCache(cache_dir):
    num_files_removed = 0
    for pattern in ('*.bin.check', '*.bin', '*.cache'):
        for cache_file in glob.iglob(os.path.join(cache_dir, pattern)):
            os.remove(cache_file)
            num_files_removed += 1
    print('RPR: removed {} cache files'.format(num_files_removed))

def ClearTextureCache(usdviewApi):
    clearCache(RprUsd.Config.GetTextureCacheDir())
def ClearKernelCache(usdviewApi):
    clearCache(RprUsd.Config.GetKernelCacheDir())

def getRprPath(_pathCache=[None]):
    if _pathCache[0]:
        return _pathCache[0]

    rprPluginType = Registry.FindTypeByName('HdRprPlugin')
    plugin = Registry().GetPluginForType(rprPluginType)
    if plugin and plugin.path:
        _pathCache[0] = plugin.path
    return _pathCache[0]

def reemitStage(usdviewApi):
    usdviewApi._UsdviewApi__appController._reopenStage()
    usdviewApi._UsdviewApi__appController._rendererPluginChanged('HdRprPlugin')

def setRenderQuality(usdviewApi, quality):
    rprPath = getRprPath()
    if rprPath is not None:
        lib = cdll.LoadLibrary(rprPath)
        lib.HdRprGetRenderQuality.restype = c_void_p
        lib.HdRprFree.argtypes = [c_void_p]

        currentQualityPtr = lib.HdRprGetRenderQuality()
        if not currentQualityPtr:
            # Enable RPR plugin if it was not done yet
            reemitStage(usdviewApi)

        currentQuality = cast(currentQualityPtr, c_char_p).value
        lib.HdRprFree(currentQualityPtr)

        if quality == currentQuality:
            return
        lib.HdRprSetRenderQuality(quality)

        def getPluginName(quality):
            if quality == b'Full':
                return 'Tahoe'
            elif quality == b'Northstar':
                return 'Northstar'
            elif quality == b'HybridPro':
                return 'HybridPro'
            else:
                return 'Hybrid'

        if getPluginName(quality) != getPluginName(currentQuality):
            reemitStage(usdviewApi)

def ChooseRenderDevice(usdviewApi):
    if RprUsd.devicesConfiguration.open_window(usdviewApi._UsdviewApi__appController._mainWindow, QtCore.Qt.Window, show_restart_warning=False):
        reemitStage(usdviewApi)

def SetRenderLowQuality(usdviewApi):
    setRenderQuality(usdviewApi, b'Low')
def SetRenderMediumQuality(usdviewApi):
    setRenderQuality(usdviewApi, b'Medium')
def SetRenderHighQuality(usdviewApi):
    setRenderQuality(usdviewApi, b'High')
def SetRenderFullQuality(usdviewApi):
    setRenderQuality(usdviewApi, b'Full')
def SetRenderNorthstarQuality(usdviewApi):
    setRenderQuality(usdviewApi, b'Northstar')
def SetRenderHybridProQuality(usdviewApi):
    setRenderQuality(usdviewApi, b'HybridPro')

class RprPluginContainer(PluginContainer):

    def registerPlugins(self, plugRegistry, usdviewApi):
        self.chooseRenderDevice = plugRegistry.registerCommandPlugin(
            "RprPluginContainer.chooseRenderDevice",
            "Render Devices",
            ChooseRenderDevice)

        self.setRenderLowQuality = plugRegistry.registerCommandPlugin(
            "RprPluginContainer.setRenderLowQuality",
            "Low",
            SetRenderLowQuality)
        self.setRenderMediumQuality = plugRegistry.registerCommandPlugin(
            "RprPluginContainer.setRenderMediumQuality",
            "Medium",
            SetRenderMediumQuality)
        self.setRenderHighQuality = plugRegistry.registerCommandPlugin(
            "RprPluginContainer.setRenderHighQuality",
            "High",
            SetRenderHighQuality)
        self.setRenderFullQuality = plugRegistry.registerCommandPlugin(
            "RprPluginContainer.setRenderFullQuality",
            "Full",
            SetRenderFullQuality)
        self.setRenderNorthstarQuality = plugRegistry.registerCommandPlugin(
            "RprPluginContainer.setRenderNorthstarQuality",
            "Northstar",
            SetRenderNorthstarQuality)
        self.setRenderHybridProQuality = plugRegistry.registerCommandPlugin(
            "RprPluginContainer.setRenderHybridProQuality",
            "HybridPro",
            SetRenderHybridProQuality)

        self.setTextureCacheDir = plugRegistry.registerCommandPlugin(
            "RprPluginContainer.setTextureCacheDir",
            "Set Texture Cache Directory",
            SetTextureCacheDir)
        self.setKernelCacheDir = plugRegistry.registerCommandPlugin(
            "RprPluginContainer.setKernelCacheDir",
            "Set Kernel Cache Directory",
            SetKernelCacheDir)
        self.clearTextureCache = plugRegistry.registerCommandPlugin(
            "RprPluginContainer.clearTextureCache",
            "Clear Texture Cache",
            ClearTextureCache)
        self.clearKernelCache = plugRegistry.registerCommandPlugin(
            "RprPluginContainer.clearKernelCache",
            "Clear Kernel Cache",
            ClearKernelCache)

        self.restartAction = plugRegistry.registerCommandPlugin(
            "RprPluginContainer.restartAction",
            "Restart",
            reemitStage)


    def configureView(self, plugRegistry, plugUIBuilder):

        rprMenu = plugUIBuilder.findOrCreateMenu("RPR")

        rprMenu.addItem(self.chooseRenderDevice)

        renderQualityMenu = rprMenu.findOrCreateSubmenu("Render Quality")
        renderQualityMenu.addItem(self.setRenderLowQuality)
        renderQualityMenu.addItem(self.setRenderMediumQuality)
        renderQualityMenu.addItem(self.setRenderHighQuality)
        renderQualityMenu.addItem(self.setRenderFullQuality)
        renderQualityMenu.addItem(self.setRenderNorthstarQuality)
        renderQualityMenu.addItem(self.setRenderHybridProQuality)

        cacheMenu = rprMenu.findOrCreateSubmenu('Cache')
        cacheMenu.addItem(self.setTextureCacheDir)
        cacheMenu.addItem(self.setKernelCacheDir)
        cacheMenu.addItem(self.clearTextureCache)
        cacheMenu.addItem(self.clearKernelCache)

        rprMenu.addItem(self.restartAction)

Tf.Type.Define(RprPluginContainer)
