# Change Log
## 1.1 Release
	- Supports Houdini 18.0 and USD 19.11
	- Uses RPR SDK 1.34.5
	- Uses RIF 1.4.3
### New features
	- Added Centos 7 build
	- Added RPR Render Settings node for use in Houdini in addition to global per project settings.
	- Image Gamma supported for GL textures
	- Add support for alpha compositing render output.  This allows overlaying on the Houdini background and viewport gizmos
	- Support for UsdTransform2D nodes in shading (Only single level, not nested)
	- Per object subdivision.  Also added a HDA RPR Mesh Properties Node for specifying subdivision level in Houdini 
	- Support of doublesided meshes
	- Support of HdDisplayStyle::displacementEnabled attributes
	- Camera FStop, Focal Length, Aperature, Focus Distance and Clipping Range is supported on cameras
	- Support for tapered width curves
	- Add debugging environment variable.  Set HD_RPR_ERROR_OUTPUT_FILE=some_file.txt to dump errors when rendering with hdRPR.
	- 

### Fixes
	- Fixed finding Hybrid.so on Ubuntu
	- Fixed teardown of Denoiser filter when disabled
	- Fixed material attachment to instances in hybrid modes
	- Fixed AOV creation in CPU rendering
	- Houdini 18.0.311 changes orientation of Dome lights.  Match this corrected behavior, but for users on older versions of Houdini can set HDRPR_INVERT_DOME_LIGHT_Z_AXIS=0 in enviroment variables to get old behavior. 
	- Hide "Error unsupported" warnings on hybrid modes
	- Only Houdini 18 versions are shown in the installer script
	- Fixed handling of USDUvTexture scale and bias
	- Fixed an issue if an AOV was created but format changed
	- Fix compilation warnings on macOS
	- Subdivision is automatically enabled on objects with displacement
	- Fix crash when releasing displacement nodes
	- Render Quality setting is disabled on macOS
	- Viewport "Headlight" is now removed when the user adds a light.  


## 1.0 Release
	- Supports Houdini 18.0 and USD 19.11
	- Uses RPR SDK 1.34.4
	- Uses RIF 1.4.2
	- Denoiser support
	- Full Spectrum Rendering modes on Windows and Ubuntu
	- Support for USDPreviewSurface, USD lights, and Volumes