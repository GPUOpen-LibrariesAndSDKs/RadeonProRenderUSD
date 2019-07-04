from pxr import Tf
from pxr.Usdviewq.plugin import PluginContainer

from ctypes import cdll
from ctypes.util import find_library

import psutil, os
	
def getRprPath():
    p = psutil.Process( os.getpid() )
    for dll in p.memory_maps():
      if dll.path.find("hdRpr") != -1:
	   return  dll.path
    print "hdRpr module not loaded"	   
    return None
	   
def setAov(aov):
    rprPath = getRprPath()
    if rprPath is not None:
	   lib = cdll.LoadLibrary(rprPath)
	   lib.SetRprGlobalAov(aov)
	   
	   
def setFilter(filter):
    rprPath = getRprPath()
    if rprPath is not None:
	   lib = cdll.LoadLibrary(rprPath)
	   lib.SetRprGlobalFilter(filter)
	   
	   
def setRenderDevice(usdviewApi, renderDeviceId):
    rprPath = getRprPath()
    if rprPath is not None:
        lib = cdll.LoadLibrary(rprPath)
        lib.SetRprGlobalRenderDevice(renderDeviceId)
        usdviewApi._UsdviewApi__appController._reopenStage()
        usdviewApi._UsdviewApi__appController._rendererPluginChanged('HdRprPlugin')
	   
	
def ColorAov(usdviewApi):
    setAov(0)
	
def NormalAov(usdviewApi):
    setAov(1)

def DepthAov(usdviewApi):
    setAov(2)
	
def PrimIdAov(usdviewApi):
    setAov(3)
	
	
def NoFilter(usdviewApi):
    setFilter(0)

def BilateralFilter(usdviewApi):
    setFilter(1)
	
def EawFilter(usdviewApi):
    setFilter(2)
	
	
def renderDeviceCPU(usdviewApi):
    setRenderDevice(usdviewApi, 0)

def renderDeviceGPU(usdviewApi):
    setRenderDevice(usdviewApi, 1)


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
			
        self.aovPrimId = plugRegistry.registerCommandPlugin(
            "RprPluginContainer.PrimIdAov",
            "Normal",
            PrimIdAov)

        self.noFilter = plugRegistry.registerCommandPlugin(
            "RprPluginContainer.NoFilter",
            "No Filter",
            NoFilter)

        self.bilateralFilter = plugRegistry.registerCommandPlugin(
            "RprPluginContainer.BilateralFilter",
            "Bilateral",
            BilateralFilter)
			
        self.eawFilter = plugRegistry.registerCommandPlugin(
            "RprPluginContainer.EawFilter",
            "EAW",
            EawFilter)
				
			
			
        self.rDeviceCpu = plugRegistry.registerCommandPlugin(
            "RprPluginContainer.renderDeviceCPU",
            "CPU",
            renderDeviceCPU)
			
        self.rDeviceGpu = plugRegistry.registerCommandPlugin(
            "RprPluginContainer.renderDeviceGPU",
            "GPU",
            renderDeviceGPU)

			

    def configureView(self, plugRegistry, plugUIBuilder):

        rprMenu = plugUIBuilder.findOrCreateMenu("RPR")
        renderModeSubMenu = rprMenu.findOrCreateSubmenu("AOV")
        renderModeSubMenu.addItem(self.aovColor)
        renderModeSubMenu.addItem(self.aovNormal)
        renderModeSubMenu.addItem(self.aovDepth)		
        renderModeSubMenu.addItem(self.aovPrimId)	
		
        filterSubMenu = rprMenu.findOrCreateSubmenu("Filter")
        filterSubMenu.addItem(self.noFilter)
        filterSubMenu.addItem(self.bilateralFilter)
        filterSubMenu.addItem(self.eawFilter)

        renderDeviceSubMenu = rprMenu.findOrCreateSubmenu("Render Device")
        renderDeviceSubMenu.addItem(self.rDeviceCpu)
        renderDeviceSubMenu.addItem(self.rDeviceGpu)
		
		
Tf.Type.Define(RprPluginContainer)