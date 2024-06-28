@echo off
setlocal ENABLEDELAYEDEXPANSION

rem Set target Houdini version to be built
rem .\pxr\imaging\plugin\hdRpr\package\buildPackageWin.bat 19.5.805
if "%~1"=="" (
    echo Target Houdini version is required. ^(e.g. buildPackageWin.bat 19.5.805^)
    pause
    exit /b
)
set HOUDINI_VERSION=%~1

rem Clean up and prepare "build" directory
if exist "build" (
    rmdir /s /q build
)
if exist "build_generatePackage_tmp_dir" (
    rmdir /s /q build_generatePackage_tmp_dir
)
mkdir build

rem Build Houdini plugin for the target version
set HFS=C:\Program Files\Side Effects Software\Houdini %HOUDINI_VERSION%
if not exist "%HFS%" (
    echo Houdini does not exist in %HFS%
    pause
    exit /b
)

python pxr\imaging\plugin\hdRpr\package\generatePackage.py -i "." -o "build"

endlocal
