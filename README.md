AMD Radeon ProRender delegate for USD (hdRpr)
===========================

This plugin allows fast GPU or CPU accelerated viewport rendering on all OpenCL 1.2 hardware for the open source USD and Hydra system

For more details on USD, please visit the web site [here](http://openusd.org).

Prerequisites
-----------------------------

Building this plugin depends on three things:

#### 1.  An existing USD build / tree
As many USD users get the USD libraries from different places, or compile their own, we tried to keep this as flexible as possible.
You can download USD to build yourself from [GitHub](https://www.github.com/PixarAnimationStudios/USD)


#### 2. Radeon ProRender SDK

Download RPR SDK as well as RIF library (Image processing) from here:  https://www.amd.com/en/technologies/sdk-agreement
License allows non-commercial use for developers.  Contact through the website for commercial distribution.


Building
-----------------------------

Build using cmake.  Here are the necessary variables to set

USD_ROOT - set this to the USD installed dir

RPR_LOCATION - set this to the Radeon Pro Render directory with include and lib dirs (version 1.3.20 or higher)

CMAKE_INSTALL_PREFIX - this is where the delegate will be installed, most likely you will set this to the same as USD_ROOT

(Variables below may be automatically detected and you can leave them as is)

BOOST_ROOT - These are necessary libraries to link the plugin.  If you installed USD, you already have these.  If you installed USD using the python build script they are the same as USD_ROOT

GLEW_LOCATION

TBBROOT

OPENVDB_LOCATION(optional) - path to OpenVDB directory with include and lib dirs

Building with RadeonProImageFilter (Denoiser):
PXR_ENABLE_RIF_SUPPORT - set this to 'ON' to enable Image filters
RIF_LOCATION - set this to directory of Image Filter library.  

example cmake building:
```
mkdir build 
cd build
cmake -DUSD_ROOT=/data/usd_build -DRPR_LOCATION=/data/RPR_SDK/RadeonProRender -DCMAKE_INSTALL_PREFIX=/data/usd_build ..
make
make install
```


Build with OpenVDB SDK
-----------------------------


 - Set "-DPXR_ENABLE_OPENVDB_SUPPORT" to "ON"

 - Set "-DOPENVDB_LOCATION", path to OpenVDB directory.
 
 - Build 
 
 example cmake building with OpenVDB:
 
```
mkdir build 
cd build
cmake -DUSD_ROOT=/data/usd_build -DRPR_LOCATION=/data/RPR_SDK/RadeonProRender -DCMAKE_INSTALL_PREFIX=/data/usd_build -DOPENVDB_LOCATION=C:/data/OpenVDB ..
make
make install
```

Supported platform: 
Windows
linux(experimental)
macOS(experimental)


Try it out
-----------------------------

Set the environment variables specified by the script when it finishes and 
launch ```usdview``` with a sample asset.

```
> usdview extras/usd/tutorials/convertingLayerFormats/Sphere.usda
```

And select RPR as the render delegate.

If you want the RPR menu added to USDView (allows selecting device, and view mode), set the environment variable:
```
PXR_PLUGINPATH_NAME=${USD_ROOT}/lib/python/rpr
```  
Where USD_ROOT is your USD install directory.
