from pxr import Tf
from pxr.Plug import Registry
from pxr.Usdviewq.plugin import PluginContainer

from ctypes import cdll, c_char_p
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
        lib.SetHdRprRenderQuality(quality)
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

Tf.Type.Define(RprPluginContainer)
