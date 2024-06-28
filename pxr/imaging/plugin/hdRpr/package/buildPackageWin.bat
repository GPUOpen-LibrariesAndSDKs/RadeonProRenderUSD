@echo off
setlocal ENABLEDELAYEDEXPANSION

rem Set target Houdini version to be built
rem .\pxr\imaging\plugin\hdRpr\package\buildPackageWin.bat 19.5.805
set HOUDINI_VERSION=%~1

rem Clean up and prepare "build" directory
if exist "build" (
    rmdir /s /q build
)
mkdir build

rem Build Houdini plugin for the target version
set HFS=C:\Program Files\Side Effects Software\Houdini %HOUDINI_VERSION%
python pxr\imaging\plugin\hdRpr\package\generatePackage.py -i "." -o "build"

endlocal
