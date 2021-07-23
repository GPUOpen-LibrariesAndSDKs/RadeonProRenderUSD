## Installation

### Automatic

Run `activateMatlibPlugin`. It will find your houdini preference dir and add material library package that will point to the current directory.

### Manual

Add a new Houdini package with such configuration json:
```
{
    "env":[
        {
            "RPR_MTLX_MATERIAL_LIBRARY_PATH":"path-to-the-package"
        }
    ]
}
```
where `path-to-the-package` depends on where do you unzip the material library package and should point to the directory that contains INSTALL.md (this file)

More info here https://www.sidefx.com/docs/houdini/ref/plugins.html

## Feedback

In case you encounter any issues or would like to make a feature request, please post an "issue" on our official [GitHub repository](https://github.com/GPUOpen-LibrariesAndSDKs/RadeonProRenderUSD/issues)
