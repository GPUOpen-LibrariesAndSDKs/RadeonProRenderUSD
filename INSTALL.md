## Installation from a package

### Houdini plugin

#### Manual

Add a new Houdini package with such configuration json:
```
{
    "env":[
        {
            "RPR":"path-to-the-package"
        },
        {
            "HOUDINI_PATH":"$RPR/houdini"
        },
        {
            "PATH":"$RPR/lib"
        },
        {
            "PYTHONPATH":"$RPR/lib/python"
        }
    ]
}
```
where `path-to-the-package` depends on where do you unzip hdRpr package and should point to the directory that contains INSTALL.md (this file)

More info here https://www.sidefx.com/docs/houdini/ref/plugins.html

#### Automatic

`activateHoudiniPlugin` executable can do the same for you automatically - it will try to find your houdini preference dir and add hdRpr package that will point to the current directory.

### Usdview plugin

Here and next, HDRPR_PACKAGE_DIR is a path to the root of the package.

* Set `RPR` environment variable to `HDRPR_PACKAGE_DIR` 
* Add `HDRPR_PACKAGE_DIR/plugin` path entry to the `PXR_PLUGINPATH_NAME` environment variable
* Add `HDRPR_PACKAGE_DIR/lib/python` path entry to the `PYTHONPATH` environment variable
* Windows only: add the `HDRPR_PACKAGE_DIR/lib` path entry to the `PATH` environment variable

OR

You can copy hdRpr package directories (lib and plugin) directly to the root of your USD package.

## Installation from sources

1. Configure cmake project (more info about it in README.md)

```
cd RadeonProRenderUSD
mkdir build
cd build
cmake -DCMAKE_INSTALL_PREFIX=package ..
```

2. Build cmake project
```
cmake --build . --config Release --target install
```

3. Follow instructions from "Installation from package" for either "Houdini plugin" or "Usdview plugin" depending on how you configured the project on step 1.

## Feedback

In case you encounter any issues or would like to make a feature request, please post an "issue" on our official [GitHub repository](https://github.com/GPUOpen-LibrariesAndSDKs/RadeonProRenderUSD/issues)
