from pxr import Tf
from pxr.Plug import Registry
from pxr.Usdviewq.plugin import PluginContainer

from ctypes import cdll, c_char_p, c_int
from ctypes.util import find_library

import os

TahoePluginType = 0
HybridPluginType = 1

def getRprPath(_pathCache=[None]):
    if _pathCache[0]:
        return _pathCache[0]

    rprPluginType = Registry.FindTypeByName('HdRprPlugin')
    plugin = Registry().GetPluginForType(rprPluginType)
    if plugin and plugin.path:
        _pathCache[0] = plugin.path
    return _pathCache[0]

def currentPluginType(rprLib):
    rprLib.GetRprPluginType.restype = c_int
    return rprLib.GetRprPluginType()

def createRprTmpDirIfNeeded(rprLib):
    rprLib.GetRprTmpDir.restype = c_char_p
    rprTmpDir = rprLib.GetRprTmpDir()
    if not os.path.exists(rprTmpDir):
        os.makedirs(rprTmpDir)

def reemitStage(usdviewApi):
    usdviewApi._UsdviewApi__appController._reopenStage()
    usdviewApi._UsdviewApi__appController._rendererPluginChanged('HdRprPlugin')


def setAov(aov):
    rprPath = getRprPath()
    if rprPath is not None:
	   lib = cdll.LoadLibrary(rprPath)
	   createRprTmpDirIfNeeded(lib)
	   lib.SetRprGlobalAov(aov)
	   
	   
def switchDenoising():
    rprPath = getRprPath()
    if rprPath is not None:
        lib = cdll.LoadLibrary(rprPath)
        createRprTmpDirIfNeeded(lib)

        lib.IsRprDenoisingEnabled.restype = c_int
        isDenoisingEnabled = lib.IsRprDenoisingEnabled()
        # TODO: change action isSelected when available

        lib.SetRprGlobalDenoising(not isDenoisingEnabled)

	   
def setRenderDevice(usdviewApi, renderDeviceId):
    rprPath = getRprPath()
    if rprPath is not None:
        lib = cdll.LoadLibrary(rprPath)
        createRprTmpDirIfNeeded(lib)
        lib.SetRprGlobalRenderDevice(renderDeviceId)
        reemitStage(usdviewApi)

def setRendererPlugin(usdviewApi, rendererPluginId):
    rprPath = getRprPath()
    if rprPath is not None:
        lib = cdll.LoadLibrary(rprPath)
        createRprTmpDirIfNeeded(lib)
        lib.SetRprRendererPlugin(rendererPluginId)
        reemitStage(usdviewApi)

def setHybridQuality(usdviewApi, quality):
    rprPath = getRprPath()
    if rprPath is not None:
        lib = cdll.LoadLibrary(rprPath)
        createRprTmpDirIfNeeded(lib)
        lib.SetRprHybridQuality(quality)
        if currentPluginType(lib) != HybridPluginType:
            lib.SetRprRendererPlugin(HybridPluginType)
            reemitStage(usdviewApi)
	
def ColorAov(usdviewApi):
    setAov(0)

def NormalAov(usdviewApi):
    setAov(1)

def DepthAov(usdviewApi):
    setAov(2)

def UVAov(usdviewApi):
    setAov(3)

def PrimIdAov(usdviewApi):
    setAov(4)

def SwitchDenoising(usdviewApi):
    switchDenoising()

def renderDeviceCPU(usdviewApi):
    setRenderDevice(usdviewApi, 0)

def renderDeviceGPU(usdviewApi):
    setRenderDevice(usdviewApi, 1)

def SetTahoe(usdviewApi):
    setRendererPlugin(usdviewApi, TahoePluginType)

def SetHybridLowQuality(usdviewApi):
    setHybridQuality(usdviewApi, 0)
def SetHybridMediumQuality(usdviewApi):
    setHybridQuality(usdviewApi, 1)
def SetHybridHighQuality(usdviewApi):
    setHybridQuality(usdviewApi, 2)


class RprPluginContainer(PluginContainer):

    def registerPlugins(self, plugRegistry, usdviewApi):

        self.aovColor = plugRegistry.registerCommandPlugin(
            "RprPluginContainer.ColorAov",
            "Color",
            ColorAov)

        self.aovNormal = plugRegistry.registerCommandPlugin(
            "RprPluginContainer.NormalAov",
            "Normal",
            NormalAov)

        self.aovDepth = plugRegistry.registerCommandPlugin(
            "RprPluginContainer.DepthAov",
            "Depth",
            DepthAov)

        self.aovUV = plugRegistry.registerCommandPlugin(
            "RprPluginContainer.UVAov",
            "primvars:st",
            UVAov)

        self.aovPrimId = plugRegistry.registerCommandPlugin(
            "RprPluginContainer.PrimIdAov",
            "PrimId",
            PrimIdAov)

        self.switchDenoising = plugRegistry.registerCommandPlugin(
            "RprPluginContainer.NoFilter",
            "Denoise",
            SwitchDenoising)

        self.rDeviceCpu = plugRegistry.registerCommandPlugin(
            "RprPluginContainer.renderDeviceCPU",
            "CPU",
            renderDeviceCPU)

        self.rDeviceGpu = plugRegistry.registerCommandPlugin(
            "RprPluginContainer.renderDeviceGPU",
            "GPU",
            renderDeviceGPU)

        self.setTahoe = plugRegistry.registerCommandPlugin(
            "RprPluginContainer.setTahoe",
            "Full",
            SetTahoe)

        self.setHybridLowQuality = plugRegistry.registerCommandPlugin(
            "RprPluginContainer.setHybridLowQuality",
            "Low",
            SetHybridLowQuality)
        self.setHybridMediumQuality = plugRegistry.registerCommandPlugin(
            "RprPluginContainer.setHybridMediumQuality",
            "Medium",
            SetHybridMediumQuality)
        self.setHybridHighQuality = plugRegistry.registerCommandPlugin(
            "RprPluginContainer.setHybridHighQuality",
            "High",
            SetHybridHighQuality)


    def configureView(self, plugRegistry, plugUIBuilder):

        rprMenu = plugUIBuilder.findOrCreateMenu("RPR")
        renderModeSubMenu = rprMenu.findOrCreateSubmenu("AOV")
        renderModeSubMenu.addItem(self.aovColor)
        renderModeSubMenu.addItem(self.aovNormal)
        renderModeSubMenu.addItem(self.aovDepth)
        renderModeSubMenu.addItem(self.aovUV)
        renderModeSubMenu.addItem(self.aovPrimId)

        rprMenu.addItem(self.switchDenoising)

        renderDeviceSubMenu = rprMenu.findOrCreateSubmenu("Render Device")
        renderDeviceSubMenu.addItem(self.rDeviceCpu)
        renderDeviceSubMenu.addItem(self.rDeviceGpu)

        renderQualityMenu = rprMenu.findOrCreateSubmenu("Render Quality")
        renderQualityMenu.addItem(self.setHybridLowQuality)
        renderQualityMenu.addItem(self.setHybridMediumQuality)
        renderQualityMenu.addItem(self.setHybridHighQuality)
        renderQualityMenu.addItem(self.setTahoe)
		
		
Tf.Type.Define(RprPluginContainer)