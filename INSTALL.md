## Installation from a package

### Automatic installation

Installed [**Python**](https://www.python.org/downloads/) with PATH environment variable setup is required for automatic installation.

> If you do not want to install python, but you have already installed Houdini with a working license, you can use hython from the Houdini package. Follow instructions under [Others](#others) subsection.

#### Windows

Run **install.bat**

#### Others

Use the installer script **install.py**.  From a terminal run:
```
python install.py 
```
And follow the instructions from the script

### Manual

Copy content of hdRpr*.tar.gz to houdini installation directory.
Default destination path looks like:  
* **Windows** `C:\Program Files\Side Effects Software\Houdini 18.0.287`  
* **Ubuntu** `/opt/hfs18.0`  
* **macOS** `/Applications/Houdini/Current/Frameworks/Houdini.framework/Versions/Current`

## Installation from sources

Run following command in configured cmake project:
```
cmake --build . --config Release --target INSTALL
```

## Feedback

In case you encounter any issues or would like to make a feature request, please post an "issue" on our official [GitHub repository](https://github.com/GPUOpen-LibrariesAndSDKs/RadeonProRenderUSD/issues)
