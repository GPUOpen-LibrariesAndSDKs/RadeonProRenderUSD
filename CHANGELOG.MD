# Change Log

## Version 2.4
### New Features:
- The RPR Final mode now uses AMD’s HIP for running on the GPU. This increases download package size for now, but allows faster first renders as the kernels are pre-compiled.
- The RPR Interactive mode has been improved with the following developments:
 - Displacement support;
 - Better MaterialX support;
 - Lookup UV nodes support;
 - Support for GPUs with less than 8 GB VRAM;
 - Support for GPUs without hardware-accelerated ray tracing.
- The Interactive mode settings to use RESTIR-based direct lighting and FSR2 upscaling have been added. These by default should give excellent interactivity when rendering in the viewport.
- Support for Houdini .rat textures has been added.
- Support for USD version 23.02 has been added.

### Bugs Fixed:
- A bug on Nvidia caused by a driver update has been fixed. (We now use the HIP feature to compile kernels to CUDA, avoiding buggy OpenCL implementations).
- The AOV “scTransparentBackground” has been renamed to “ColorWithTransparency” for clarity.
- Artifacts with the Interactive mode (Hybrid Pro) using MaterialX have been fixed.
- Tiled rendering with orthographic cameras has been fixed.
- Dome light rotations were incorrect — fixed.
- Incorrect renders with per-face materials have been fixed.
- More UI and settings have been added for Toon Materials.
- The UV AOV was showing incorrectly in the Houdini MPlay viewer — fixed.
- IES lights now correctly use the Z direction. They were using the Y direction.
- Default material can now be applied to points for rendering. Sphere points now look more rounded with smooth normals.
- Support for light linking of Toon shaders has been added.
- OpenVDB volumes have been fixed to work again.
- A random seed setting has been added to jitter the noise in a render.


## Version 2.3
### New Features:
- Support for Houdini 19.5 has been added.
- Support for USD 22.05 has been added.
- The RPR Interactive mode has been improved with the following developments:
  - Better support for more shader node types;
  - Even faster rendering with optimized settings out of the box;
  - Hair rendering support;
  - More AOVs supported;
  - Support for OpenColorIO texture spaces;
  - Now supported on Linux.
- Instance and Object ID AOVs are now supported.

### Bugs Fixed:
- The RPR Interactive mode’s ray depths settings are now correctly applied when rendering. 
- Files that have relative paths to textures are now processed correctly.
- A setting for Pixel Filter (antialiasing) has been added.
- The USD setting for scene units is now supported correctly This is applied to transforms and the Depth AOV. In Houdini the compositing with the background grid in the viewport is now correct.
- Vertex color primitive variables are now supported, in particular the “Display Color” setting.
- The first UV channel (even if not named “st”) is now exported and used.
- The speed of exporting MaterialX files has been improved greatly.
- Tiled rendering with Perspective Cameras now works correctly.


## Version 2.2
### New Features:
- Support of Houdini 19.  Houdini 19 adds MaterialX nodes which Radeon ProRender supports through the plug-in.
- Updates to the Render Quality modes: 
    - RPR Interactive is a new mode supporting GPUs which use the Vulkan ray tracing extension, and is optimized for fast viewport rendering;
    - RPR Final, previously Full” mode. It is intended “” for final rendering the utmost physical correctness and image quality;
    - Both modes render similar images;
    - Both modes support full MaterialX shader networks.

### Bugs Fixed
- Support of camera aperture offset has been added.
- Multiple render devices as well as CPU + GPU rendering can now be selected.
- Single color VDB volumes now work using the “gain” parameter.
- The performance of rendering in the RPR Final mode using adaptive sampling has been improved by optimizing the active pixels check.
- The new Gamma Tonemap filter has been added.
- A setting for using UV to generate Outline renders has been added.
- An issue of incorrect Subsurface Scattering on Vega GPUS has been added.
- Black rendering of toon shaders on macOS has been fixed.
- A crash that could occur when processing emission shaders objects with subdivision added has been eliminated.
- The startup time for CPU rendering has been reduced.
- Low utilization on macOS with CPU + GPU rendering has been eliminated.
- Performance in scenes with multiple transparent materials has been improved.
- An issue with artifacts in alpha texture masks has been fixed.
- The render performance on Vega and Polaris GPUs has been improved.
- Particle motion blur on GPUs has been fixed.
- A crash that could occur when using .tif textures with zip compression has been eliminated.
- An issue with emission shaders disappearing in volume objects has been eliminated.


## Version 2.1
### New Features:
- Subsurface Scattering and Volume shaders now work in RPR 2.0.  This allows the rendering of organic materials, such as skin, which absorb light into their interior.
- Deformation motion blur gives accurate motion blur to objects which are being deformed, for example, a flag flapping in the wind or a ball squashing.  Besides, motion blur export has been optimized, and a setting for disabling deformation motion blur has been added.
- The new RPR Toon Shader has been added.  This enables cartoon style shading for a non-photorealistic look.  Toon shaders can be used in a “simple” mode for just setting a color or a gradient of different colors for shadow vs lit areas of the object.
- Support for USD 21.05 has been added.
- Cryptomatte AOVs can now be exported from Houdini.


### Issues Fixed:
- Overbright edges of objects with Uber shaders in metalness mode ― fixed.
- Shaders with high roughness could have artifacts with reflection or refraction ― fixed.
- Users can now override the environment light background color in Houdini.
- The render’s “percent done” statistics could be incorrect if the minimum number of samples had not yet been reached ― fixed.
- An issue with the visibility of distant lights not being respected has been fixed.
- A crash was occurring on macOS in some cases when using the RPR Material Library for the first time ― fixed.
- RPR Materials nodes were not present in Houdini on Linux ― fixed.
- The speed of rendering via Houdini’s Husk utility with multiple render products has been increased.
- .rpr export fixes:
    - .rpr export now exports USD depth AOV correctly;
    - An issue with creating temporary directories when exporting .rpr files has been fixed;
    - The $OS variable can now be used in the output path to change the path based on the operating system.
- Developer changes to hdRPR:
    - The hdRPR render delegate can now override the kernel cache path with an environment variable;
    - The batch or interactive render mode can now be switched on dynamically.
- MaterialX fixes:
    - Logging with the MaterialX loader has been improved;
    - Support for the Tangent node has been added;
    - Support for the View Direction node has been added;
    - Support for the RPR Emissive node has been added;
    - The displacement scale has been fixed;
    - An issue with the use of the mix node to mix surface shaders has been fixed.

### Known Issues:
- In RPR 2.0, heterogenous volumes, smoke and fire simulations or VDB files are not yet supported.
- Subsurface scattering and volume shader are currently disabled on macOS due to long compile times.
- Some AOVs may have artifacts on AMD cards with drivers earlier than 21.6.1


## Version 2.0.27
### New Features:
-   Support for AMD Radeon™ RX 6700 XT graphics cards has been added.
-   Colorized Object ID and Material ID AOVs have been added for human viewing.
-   Support for the RPR Material Library in Houdini has been added.  Users should install the Material Library and then look for the Import Material button in the RPR Menu.

### Issues Fixed:
-   The displayColor value is now used for objects.
-   UsdPreviewSurface displacement when a constant value is used — fixed.
-   Smooth normals for a non-refined mesh can now be generated.
-   Crashing when updating a material to an invisible mesh has been fixed.
-   Crashing when Houdini’s graph is in an erroneous state while using the RPR Material Properties node has been fixed.
-   Crashing which could occur when saving .hip files in Houdini has been fixed.
-   Environment Lights and various overrides can now be rotated independently. 
-   Area lights were shadowing too aggressively — fixed.
-   Switching to contour rendering could fail on an NVidia GPU — fixed.
-   Possible crashing when rendering AOVs in the IPR has been fixed.
-   Textures could be too blurry due to incorrect MIP mapping, particularly textures in a plane perpendicular to the camera direction.
-   Adaptive sampling now works with RPR 2.
-   Spiral artifacts when rendering with both CPU and GPU have been fixed.
-   The Opacity AOV was not taking the maximum ray depth into account — fixed.
-   Artifacts in the Depth AOV have been fixed.
-   Compiling the BVH geometry BVH has been improved in large scenes.

### Known Issues:
-   RPR 2.0 has some forthcoming features.  If these are needed, use the Legacy render mode:
    - Heterogenous volumes;
-   The first render on macOS® with RPR 2.0 can take a few minutes to start, while the kernels are being compiled.


## Version 2.0.13
- Improved interactivity with Interactive Renders.
- Some issues with texture compression present in version 3.0.1 are fixed.
- Issues with normal maps (related to texture compression) are also fixed.
- Fixed issues with .rpr file export:
    - Fixed the AOV names in config.json file.
    - Fixed the contour rendering option in config.json file
    - Export of .rpr file is made faster and textures are reused between animation frames.
- Users no longer need to manually restart the render when switching render mode in Houdini.
- Time-to-first-pixel is made faster when not using denoisers.
- Added an ID parameter to Render Geometry Settings Node in Houdini for setting an explicit Object ID.
- Add a Houdini Node RPR Material Properties for explicitly setting the Material ID to be used in the corresponding AOV.
- Single and two-channel textures are handled correctly now.
- Fixed USDPreviewSurface Normal Maps.


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
