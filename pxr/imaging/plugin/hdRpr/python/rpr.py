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

from ctypes import cdll, c_int
from ctypes.util import find_library

import os

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

def setRenderDevice(usdviewApi, renderDeviceId):
    rprPath = getRprPath()
    if rprPath is not None:
        lib = cdll.LoadLibrary(rprPath)
        lib.SetHdRprRenderDevice(renderDeviceId)
        reemitStage(usdviewApi)

def setRenderQuality(usdviewApi, quality):
    rprPath = getRprPath()
    if rprPath is not None:
        lib = cdll.LoadLibrary(rprPath)
        lib.GetHdRprRenderQuality.restype = c_int
        currentQuality = lib.GetHdRprRenderQuality()
        lib.SetHdRprRenderQuality(quality)
        if (currentQuality == 3 and quality < 3) or \
           (currentQuality < 3 and quality == 3):
            reemitStage(usdviewApi)

def renderDeviceCPU(usdviewApi):
    setRenderDevice(usdviewApi, 0)

def renderDeviceGPU(usdviewApi):
    setRenderDevice(usdviewApi, 1)

def SetRenderLowQuality(usdviewApi):
    setRenderQuality(usdviewApi, 0)
def SetRenderMediumQuality(usdviewApi):
    setRenderQuality(usdviewApi, 1)
def SetRenderHighQuality(usdviewApi):
    setRenderQuality(usdviewApi, 2)
def SetRenderFullQuality(usdviewApi):
    setRenderQuality(usdviewApi, 3)

class RprPluginContainer(PluginContainer):

    def registerPlugins(self, plugRegistry, usdviewApi):
        self.rDeviceCpu = plugRegistry.registerCommandPlugin(
            "RprPluginContainer.renderDeviceCPU",
            "CPU",
            renderDeviceCPU)
        self.rDeviceGpu = plugRegistry.registerCommandPlugin(
            "RprPluginContainer.renderDeviceGPU",
            "GPU",
            renderDeviceGPU)

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

        self.restartAction = plugRegistry.registerCommandPlugin(
            "RprPluginContainer.restartAction",
            "Restart",
            reemitStage)


    def configureView(self, plugRegistry, plugUIBuilder):

        rprMenu = plugUIBuilder.findOrCreateMenu("RPR")

        renderDeviceSubMenu = rprMenu.findOrCreateSubmenu("Render Device")
        renderDeviceSubMenu.addItem(self.rDeviceCpu)
        renderDeviceSubMenu.addItem(self.rDeviceGpu)

        renderQualityMenu = rprMenu.findOrCreateSubmenu("Render Quality")
        renderQualityMenu.addItem(self.setRenderLowQuality)
        renderQualityMenu.addItem(self.setRenderMediumQuality)
        renderQualityMenu.addItem(self.setRenderHighQuality)
        renderQualityMenu.addItem(self.setRenderFullQuality)

        rprMenu.addItem(self.restartAction)

Tf.Type.Define(RprPluginContainer)
