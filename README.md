AMD Radeon ProRender USD Hydra delegate
===========================

This plugin allows fast GPU or CPU accelerated viewport rendering on all OpenCL 1.2 hardware for the open source USD and Hydra system

You can build this plugin as usdview plugin or as houdini plugin.

For more details on USD, please visit the web site [here](http://openusd.org).

Prerequisites
-----------------------------

#### Common
* **AMD Radeon™ ProRender SDK** [link](https://www.amd.com/en/technologies/sdk-agreement)		
* **AMD Radeon™ Image Filter Library** [link](https://www.amd.com/en/technologies/sdk-agreement)	  
License allows non-commercial use for developers.  Contact through the website for commercial distribution.

#### For UsdView plugin 
* **USD build / tree**		  
As many USD users get the USD libraries from different places, or compile their own, we tried to keep this as flexible as possible.
You can download USD to build yourself from [GitHub](https://www.github.com/PixarAnimationStudios/USD)

#### For Houdini plugin
* **Houdini 18**	  
You can download Houdini installer from [Daily Builds | SideFX](https://www.sidefx.com/download/daily-builds/#category-gold)

Building
-----------------------------

Build using cmake.

#### Required Components

##### Radeon Pro Render

| Dependency Name            | Description                                                             | Version          |
| ------------------         |-----------------------------------------------------------------------  | -------          |
| RPR_LOCATION               | Radeon Pro Render directory with include and lib dirs                   | 1.3.20 or higher |

##### UsdView plugin Components

UsdView plugin is build by default (```PXR_BUILD_AS_HOUDINI_PLUGIN=FALSE```).

| Dependency Name            | Description                                                             | Version          |
| ------------------         |-----------------------------------------------------------------------  | -------          |
| USD_ROOT                   | USD directory with include and lib dirs                                 | 19.07            |

##### Houdini plugin Components

To build houdini plugin set cmake flag ```PXR_BUILD_AS_HOUDINI_PLUGIN=TRUE```.

| Dependency Name            | Description                                                             | Version          |
| ------------------         |-----------------------------------------------------------------------  | -------          |
| HOUDINI_ROOT               | Houdini installation directory                                          | 18               |

#### Optional Components

##### Denoise

Support for image filters is disabled by default, and can optionally be enabled by
specifying the cmake flag ```PXR_ENABLE_RIF_SUPPORT=TRUE```.

| Dependency Name            | Description                                                             | Version          |
| ------------------         |-----------------------------------------------------------------------  | -------          |
| RIF_LOCATION               | Radeon Image Filter Library directory with include and lib dirs         | 1.2.0 or higher  |

##### OpenVDB

Support for OpenVDB is disabled by default, and can optionally be enabled by
specifying the cmake flag ```PXR_ENABLE_OPENVDB_SUPPORT=TRUE```.

**Following dependency required only for usdview plugin, houdini is shipped with own build of openvdb**

| Dependency Name            | Description                                                             | Version          |
| ------------------         |-----------------------------------------------------------------------  | -------          |
| OPENVDB_LOCATION           | OpenVDB directory with include and lib dirs                             |                  |

#### Example

```
mkdir build 
cd build
cmake -DUSD_ROOT=/data/usd_build -DRPR_LOCATION=/data/RPR_SDK/RadeonProRender -DCMAKE_INSTALL_PREFIX=/data/usd_build ..
make
make install
```

Supported Platforms
-----------------------------
* Windows
* linux(experimental)
* macOS(experimental)

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
