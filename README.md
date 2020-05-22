AMD Radeon ProRender USD Hydra delegate
===========================

This plugin allows fast GPU or CPU accelerated viewport rendering on all OpenCL 1.2 hardware for the open source USD and Hydra system

You can build this plugin as usdview plugin or as houdini plugin.

For more details on USD, please visit the web site [here](http://openusd.org).

Getting and Building the Code
-----------------------------

#### 1. Install prerequisites

- Required:
    - C++ compiler:
        - gcc
        - Xcode
        - Microsoft Visual Studio
    - CMake
    - Python

#### 2. Download the hdRpr source code

You can use ```git``` to clone the repository.

```
> git clone --recurse-submodules -j2 https://github.com/GPUOpen-LibrariesAndSDKs/RadeonProRenderUSD
Cloning into 'RadeonProRenderUSD'...
```

or

```
> git clone https://github.com/GPUOpen-LibrariesAndSDKs/RadeonProRenderUSD
Cloning into 'RadeonProRenderUSD'...
> cd RadeonProRenderUSD
> git submodule update --init
```

#### 3. Configure project using cmake

##### Required Components

##### USD Component

Provide USD in one of two ways:

* An installation of USD. Define pxr_DIR to point to it when running cmake, if required. You can download USD to build yourself from [GitHub](https://www.github.com/PixarAnimationStudios/USD).
* The USD which is provided with Houdini. The HFS environment variable should point to the Houdini installation (the correct way is to run cmake from Houdini's `Command Line Tools` or by sourcing `houdini_setup`). You can download Houdini installer from [Downloads | SideFX](https://www.sidefx.com/download).

##### Optional Components

##### OpenVDB

**Following dependency required only for usdview plugin, houdini is shipped with own build of openvdb**

| Dependency Name            | Description                                                             | Version          |
| ------------------         |-----------------------------------------------------------------------  | -------          |
| OPENVDB_LOCATION           | OpenVDB directory with include and lib dirs                             |                  |

##### Utility cmake options

##### `RPR_SDK_PLATFORM` - Forcing build against specific platform libraries

Let's say you are on centos 7 and want to force it to use the centos6 build,
then you need to specify ```RPR_SDK_PLATFORM=centos6``` cmake flag

##### Example

```
mkdir build
cd build
cmake -Dpxr_DIR=/data/usd_build -DCMAKE_INSTALL_PREFIX=/data/usd_build ..
cmake --build . --config Release --target install
```

Supported Platforms
-----------------------------
* Windows
* linux(experimental)
* macOS(experimental)

Try it out
-----------------------------

Follow instruction from INSTALL.md to activate the plugin.
Launch either usdview or Houdini's Solaris viewport and select RPR as the render delegate.

#### Environment Variables

*   `HDRPR_ENABLE_TRACING`

    Instruct Radeon ProRender to generate trace files for debugging purposes. The tracing will record all RPR commands with a memory dump of the data used. By default, RPR tracing is disabled. To enable it set `HDRPR_ENABLE_TRACING` to 1.

    When tracing is enabled, the trace files are recorded by default in the following directory depending on the OS (In the case of multiple directories first existing will be used):

    - `C:\ProgramData\hdRPR` for Windows
    - `$TMPDIR/hdRPR`, `$P_tmpdir/hdRPR`, `/tmp/hdRPR` for Linux and macOS

*   `HDRPR_TRACING_DIR`

    To change the default directory, add the environment variable `HDRPR_TRACING_DIR` pointing to the location in which you wish the trace files to be recorded. For example, set `HDRPR_TRACING_DIR=C:\folder\` to activate the tracing in `C:\folder\`.
