from pxr import Tf
from pxr.Usdviewq.plugin import PluginContainer

from ctypes import cdll
import platform

if platform.system() == 'Darwin':
    lib_name = 'hdRpr.dylib'
elif platform.system() == 'Linux':
    lib_name = 'hdRpr.so'
else:
    lib_name = 'libhdRpr.dll'

def renderModeGlobalIllumination(usdviewApi):
    lib = cdll.LoadLibrary(lib_name)
    lib.SetRprGlobalRenderMode(0)

def renderDirectIllumination(usdviewApi):
    lib = cdll.LoadLibrary(lib_name)
    lib.SetRprGlobalRenderMode(1)
	
def renderDirectIlluminationNoShadow(usdviewApi):
    lib = cdll.LoadLibrary(lib_name)
    lib.SetRprGlobalRenderMode(2)
	
def renderModeWireframe(usdviewApi):
    lib = cdll.LoadLibrary(lib_name)
    lib.SetRprGlobalRenderMode(3)
	
def renderMaterialIndex(usdviewApi):
    lib = cdll.LoadLibrary(lib_name)
    lib.SetRprGlobalRenderMode(4)

def renderModePosition(usdviewApi):
    lib = cdll.LoadLibrary(lib_name)
    lib.SetRprGlobalRenderMode(5)

def renderModeNormal(usdviewApi):
    lib = cdll.LoadLibrary(lib_name)
    lib.SetRprGlobalRenderMode(6)
	
def renderModeTexCoord(usdviewApi):
    lib = cdll.LoadLibrary(lib_name)
    lib.SetRprGlobalRenderMode(7)
	
def renderModeAO(usdviewApi):
    lib = cdll.LoadLibrary(lib_name)
    lib.SetRprGlobalRenderMode(8)

def renderModeDiffuse(usdviewApi):
    lib = cdll.LoadLibrary(lib_name)
    lib.SetRprGlobalRenderMode(9)


	
	
def renderDeviceCPU(usdviewApi):
    lib = cdll.LoadLibrary(lib_name)
    lib.SetRprGlobalRenderDevice(0)
	
def renderDeviceGPU(usdviewApi):
    lib = cdll.LoadLibrary(lib_name)
    lib.SetRprGlobalRenderDevice(1)
	
	
class RprPluginContainer(PluginContainer):

    def registerPlugins(self, plugRegistry, usdviewApi):

        self.rModeGI = plugRegistry.registerCommandPlugin(
            "RprPluginContainer.renderModeGlobalIllumination",
            "Global Illumination",
            renderModeGlobalIllumination)
			
        self.rModeDI = plugRegistry.registerCommandPlugin(
            "RprPluginContainer.renderDirectIllumination",
            "Direct Illumination",
            renderDirectIllumination)

        self.rModeDINS = plugRegistry.registerCommandPlugin(
            "RprPluginContainer.renderDirectIlluminationNoShadow",
            "Direct Illumination No Shadow",
            renderDirectIlluminationNoShadow)
			
        self.rModeWF = plugRegistry.registerCommandPlugin(
            "RprPluginContainer.renderModeWireframe",
            "Wireframe",
            renderModeWireframe)
			
        self.rModeMat = plugRegistry.registerCommandPlugin(
            "RprPluginContainer.renderMaterialIndex",
            "MaterialIndex",
            renderMaterialIndex)

        self.rModePos = plugRegistry.registerCommandPlugin(
            "RprPluginContainer.renderModePosition",
            "Position",
            renderModePosition)
			
        self.rModeNorm = plugRegistry.registerCommandPlugin(
            "RprPluginContainer.renderModeNormal",
            "Normal",
            renderModeNormal)
			
        self.rModeTC = plugRegistry.registerCommandPlugin(
            "RprPluginContainer.renderModeTexCoord",
            "Texture Coordinate",
            renderModeTexCoord)

        self.rModeAO = plugRegistry.registerCommandPlugin(
            "RprPluginContainer.renderModeAO",
            "ambient occlusion",
            renderModeAO)
			
        self.rModeDF = plugRegistry.registerCommandPlugin(
            "RprPluginContainer.renderModeDiffuse",
            "Diffuse",
            renderModeDiffuse)
			
			
			
			
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
        renderModeSubMenu = rprMenu.findOrCreateSubmenu("Render Mode")
        renderModeSubMenu.addItem(self.rModeGI)
        renderModeSubMenu.addItem(self.rModeDI)		
        renderModeSubMenu.addItem(self.rModeDINS)
        renderModeSubMenu.addItem(self.rModeWF)
        #renderModeSubMenu.addItem(self.rModeMat)		
        #renderModeSubMenu.addItem(self.rModePos)
        #renderModeSubMenu.addItem(self.rModeNorm)
        renderModeSubMenu.addItem(self.rModeTC)		
        renderModeSubMenu.addItem(self.rModeAO)
        renderModeSubMenu.addItem(self.rModeDF)
		

        renderDeviceSubMenu = rprMenu.findOrCreateSubmenu("Render Device")
        renderDeviceSubMenu.addItem(self.rDeviceCpu)
        renderDeviceSubMenu.addItem(self.rDeviceGpu)
		
		
Tf.Type.Define(RprPluginContainer)