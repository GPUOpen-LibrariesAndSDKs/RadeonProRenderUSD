## Installation from a package

### Windows

Run **install.bat**

### macOS

Run **install.command**

### Others

Use the installer script **install.py**.  From a terminal run:
```
python install.py 
```
And follow the instructions from the script

> If you have no python installed, you can use hython from the Houdini package. (this would be extremely rare)

## Installation from sources

Run following command in configured cmake project:
```
cmake --build . --config Release --target INSTALL
```

## Feedback

In case you encounter any issues or would like to make a feature request, please post an "issue" on our official [GitHub repository](https://github.com/GPUOpen-LibrariesAndSDKs/RadeonProRenderUSD/issues)
