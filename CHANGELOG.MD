# Change Log

## Version 2.0
### New Features:
-   The new plug-in version incorporates version 2.0 of our Radeon™ ProRender system and brings about the significant changes and enhancements:
Hardware-accelerated ray tracing on AMD Radeon™ RX 6000 series GPUs.
    - Better scaling across multiple devices: the use of two or more GPUs gives a better performance improvement with Radeon ProRender 2.0 than with Radeon ProRender 1.0 in most cases.
    - Less noise for a given amount of render samples.  Using the same number of samples as with Radeon ProRender 1.0 may be slower in some scenes, but noise will be significantly lower.  
    - RPR 2.0 is the default render mode called “Full”.  Users who wish to use RPR 1.0 can set the render quality mode to “Legacy”.
    - A new setting for texture cache has been added to the RPR menu in Houdini.  The specified folder will cache textures for rendering, and can be cleaned up by the user if it becomes too large.
    - MaterialX networks can now be attached to an object. To use them, we have added an RPR Material Library LOP node.  Add this node and select a MaterialX file and specify the surface shader to use.
    - User customizable LPE outputs have been added.
    - OpenColorIO support has been added, allowing to control textures colorspace and the rendering colorspace.
-   Support of Houdini 18.5 and USD 20.11 has been added.
-   The installer for Houdini is now an executable `activateHoudiniPlugin`.
-   A rendering setting has been added to change noise “seed” to vary noise across frames.
-   Support of Varying or Vertex primitive variables has been added.
-   An “RPR Standard Render Vars” node in Houdini for AOVs has been added in the tab menu for nodes.


### Issues Fixed:
-   Using the Uber shader with “Metalness” reflection mode now matches the Disney shader PBR standard more closely.  Among other improvements, the USD Preview Surface node respects the “Specular” parameter better.
-   Motion Blur, particularly for rotation, is now more correct.
-   Depth of field was not respecting camera sensor size and focal length correctly — fixed.
-   UVs with USD meshes using geomSubset — fixed.
-   Some issues with textures embedded in USDZ files have been fixed.
-   The visibility of mesh instances has been fixed.
-   A mesh primitive was not always marked as dirty when the material changes or the geomSubset changes — fixed.
-   .rpr export node to work with non-ASCII characters — fixed.
-   An issue of locking with no lights in the scene has been fixed.
-   Various issues with checking and preventing memory leaks have been fixed.
-   Selection of instances in the viewport has been fixed: the objectID AOV is correct now.
-   A crash when AOVs are resized in Houdini has been fixed.
-   A crash when getting volume data directly from Houdini has been fixed.
-   .rpr file export has been improved, and a HDA node to enable it in Houdini has been added.
-   Rendered .rpr files exported from Houdini were flipped vertically — fixed.
-   Issues have been fixed to compile on Ubuntu 20.04.
-   Motion Blur was changed in USD 19.11 leading to incorrect (to little) motion blur effect.  This is fixed to match the USD behavior.

### Known Issues:
-   RPR 2.0 has some forthcoming features.  If these are needed, use the Legacy render mode:
    - Heterogenous volumes;
    - Adaptive sampling;
    - Adaptive subdivision.
-   The first render on macOS® with RPR 2.0 can take a few minutes to start, while the kernels are being compiled.


## 1.3 Release

hdRpr installation scheme was changed - there is no more need to copy hdRpr package directly into Houdini. More info [here](https://github.com/GPUOpen-LibrariesAndSDKs/RadeonProRenderUSD/pull/285).

### New features
* Parallelize texture loading ([PR #307](https://github.com/GPUOpen-LibrariesAndSDKs/RadeonProRenderUSD/pull/307))
* Expose RPR-native materials ([PR #301](https://github.com/GPUOpen-LibrariesAndSDKs/RadeonProRenderUSD/pull/301))
* Add RPR 2.0 (Northstar) support ([PR #297](https://github.com/GPUOpen-LibrariesAndSDKs/RadeonProRenderUSD/pull/297))
* Add UsdRenderVar support (raw AOVs only) ([PR #300](https://github.com/GPUOpen-LibrariesAndSDKs/RadeonProRenderUSD/pull/300))
* Add UDIM support (Northstar only) ([PR #298](https://github.com/GPUOpen-LibrariesAndSDKs/RadeonProRenderUSD/pull/298))
* Improve AOV system ([PR #295](https://github.com/GPUOpen-LibrariesAndSDKs/RadeonProRenderUSD/pull/295))
* Expose shadow and reflection catcher controls ([PR #294](https://github.com/GPUOpen-LibrariesAndSDKs/RadeonProRenderUSD/pull/294))
* Expose caustics control ([PR #291](https://github.com/GPUOpen-LibrariesAndSDKs/RadeonProRenderUSD/pull/291))
* Add render mode render setting ([PR #289](https://github.com/GPUOpen-LibrariesAndSDKs/RadeonProRenderUSD/pull/289))
* Optimize batch rendering ([PR #288](https://github.com/GPUOpen-LibrariesAndSDKs/RadeonProRenderUSD/pull/288))
* BasisCurves bezier support ([PR #282](https://github.com/GPUOpen-LibrariesAndSDKs/RadeonProRenderUSD/pull/282))
* Speed up basis curves creation ([PR #302](https://github.com/GPUOpen-LibrariesAndSDKs/RadeonProRenderUSD/pull/302))
* Add RIF-based tone mapping filter ([PR #283](https://github.com/GPUOpen-LibrariesAndSDKs/RadeonProRenderUSD/pull/283))

### Fixes
* Fixed depth of field ([PR #319](https://github.com/GPUOpen-LibrariesAndSDKs/RadeonProRenderUSD/pull/319))
* Fixed normal input of AI denoise filter ([PR #305](https://github.com/GPUOpen-LibrariesAndSDKs/RadeonProRenderUSD/pull/305))
* Fixed dome light visibility ([PR #292](https://github.com/GPUOpen-LibrariesAndSDKs/RadeonProRenderUSD/pull/292))
* Now UV primvar name is queried from the bound material ([PR #296](https://github.com/GPUOpen-LibrariesAndSDKs/RadeonProRenderUSD/pull/296))
* Use specular workflow on refractive materials for UsdPreviewSurface ([PR #299](https://github.com/GPUOpen-LibrariesAndSDKs/RadeonProRenderUSD/pull/299))


## 1.2 Release
### New features
- Volume rework ([PR #268](https://github.com/GPUOpen-LibrariesAndSDKs/RadeonProRenderUSD/pull/268))
- Points support ([PR #270](https://github.com/GPUOpen-LibrariesAndSDKs/RadeonProRenderUSD/pull/270))
- Camera Motion Blur ([PR #273](https://github.com/GPUOpen-LibrariesAndSDKs/RadeonProRenderUSD/pull/273))
- Xform Motion Blur ([PR #256](https://github.com/GPUOpen-LibrariesAndSDKs/RadeonProRenderUSD/pull/256))
- UsdSkel ([PR #254](https://github.com/GPUOpen-LibrariesAndSDKs/RadeonProRenderUSD/pull/254))
- UsdLuxShapingAPI - Point, Spot and IES lights ([PR #238](https://github.com/GPUOpen-LibrariesAndSDKs/RadeonProRenderUSD/pull/238))
- Visibility flags ([PR #261](https://github.com/GPUOpen-LibrariesAndSDKs/RadeonProRenderUSD/pull/261))
- Support Houdini's principled shader ([PR #234](https://github.com/GPUOpen-LibrariesAndSDKs/RadeonProRenderUSD/pull/234))
- Update to USD 20.05 with USD 19.11 and USD 20.02 compatibility ([PR #274](https://github.com/GPUOpen-LibrariesAndSDKs/RadeonProRenderUSD/pull/274), [PR #236](https://github.com/GPUOpen-LibrariesAndSDKs/RadeonProRenderUSD/pull/236))
- Unlock medium quality ([PR #222](https://github.com/GPUOpen-LibrariesAndSDKs/RadeonProRenderUSD/pull/222))
- Enable ML Denoiser on macOS ([PR #279](https://github.com/GPUOpen-LibrariesAndSDKs/RadeonProRenderUSD/pull/279))


### Fixes
- Fixed normal map appearance ([PR #271](https://github.com/GPUOpen-LibrariesAndSDKs/RadeonProRenderUSD/pull/271))
- Fixed mesh subdivision when it has geomSubset ([PR #263](https://github.com/GPUOpen-LibrariesAndSDKs/RadeonProRenderUSD/pull/263))
- Fixed mistakenly enabled interactive mode while in husk or usdrecord ([PR #262](https://github.com/GPUOpen-LibrariesAndSDKs/RadeonProRenderUSD/pull/262))
- macOS and Linux builds are not bound to Houdini installation directory anymore ([PR #253](https://github.com/GPUOpen-LibrariesAndSDKs/RadeonProRenderUSD/pull/253), [PR #240](https://github.com/GPUOpen-LibrariesAndSDKs/RadeonProRenderUSD/pull/240))
- Fixed macOS crash (#251)
- Fixed render settings parsing in husk ([PR #246](https://github.com/GPUOpen-LibrariesAndSDKs/RadeonProRenderUSD/pull/246))
- Fixed area lights in Low and Medium qualities ([PR #238](https://github.com/GPUOpen-LibrariesAndSDKs/RadeonProRenderUSD/pull/238))
- Fixed adaptive sampling ([PR #237](https://github.com/GPUOpen-LibrariesAndSDKs/RadeonProRenderUSD/pull/237))
- Fixed compilation when python3 used ([Issue #213](https://github.com/GPUOpen-LibrariesAndSDKs/RadeonProRenderUSD/issues/213))
- Mark bound AOVs as converged even in case of error ([PR #224](https://github.com/GPUOpen-LibrariesAndSDKs/RadeonProRenderUSD/pull/224))
- Remove brute-force search of the working plugin when context with the requested plugin cannot be created ([PR #223](https://github.com/GPUOpen-LibrariesAndSDKs/RadeonProRenderUSD/pull/223))
- Fixed render quality switching ([PR #221](https://github.com/GPUOpen-LibrariesAndSDKs/RadeonProRenderUSD/pull/221))
- Fixed AI denoiser on Linux ([PR #219](https://github.com/GPUOpen-LibrariesAndSDKs/RadeonProRenderUSD/pull/219))
- Fixed UsdPreviewSurface refractive materials ([PR #217](https://github.com/GPUOpen-LibrariesAndSDKs/RadeonProRenderUSD/pull/217))


## 1.1 Release
### New features
- Added Centos 7 build
- RPR Render Settings moved to tab in "Render Settings" LOP node for use in Houdini.
- Image Gamma supported for GL textures
- Add support for alpha compositing render output.  This allows overlaying on the Houdini background and viewport gizmos
- Support for UsdTransform2D nodes in shading (Only single level, not nested)
- Per object subdivision.  Also added a HDA RPR Mesh Properties Node for specifying subdivision level in Houdini 
- Support of doublesided meshes
- Support of HdDisplayStyle::displacementEnabled attributes
- Camera FStop, Focal Length, Aperture, Focus Distance and Clipping Range is supported on cameras
- Support for tapered width curves (linear curves only)
- Add debugging environment variable.  Set HD_RPR_ERROR_OUTPUT_FILE=some_file.txt to dump USD errors when rendering with hdRPR in Houdini.


### Fixes
- Fixed finding Hybrid.so on Ubuntu
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
- Denoiser support
- Full Spectrum Rendering modes on Windows and Ubuntu
- Support for USDPreviewSurface, USD lights, and Volumes
