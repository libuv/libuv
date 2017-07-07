@echo off && setlocal

cd %~dp0

if /i "%1"=="help" goto help
if /i "%1"=="--help" goto help
if /i "%1"=="-help" goto help
if /i "%1"=="/help" goto help
if /i "%1"=="?" goto help
if /i "%1"=="-?" goto help
if /i "%1"=="--?" goto help
if /i "%1"=="/?" goto help

@rem Process arguments.
set config=
set target=Build
set target_arch=ia32
set target_env=
set noprojgen=
set nobuild=
set run=
set vs_toolset=x86
set msbuild_platform=WIN32
set library=static_library

:next-arg
if "%1"=="" goto args-done
if /i "%1"=="debug"        set config=Debug&goto arg-ok
if /i "%1"=="release"      set config=Release&goto arg-ok
if /i "%1"=="test"         set run=run-tests.exe&goto arg-ok
if /i "%1"=="bench"        set run=run-benchmarks.exe&goto arg-ok
if /i "%1"=="clean"        set target=Clean&goto arg-ok
if /i "%1"=="vs2008"       set target_env=%1&goto arg-ok
if /i "%1"=="vs2010"       set target_env=%1&goto arg-ok
if /i "%1"=="vs2012"       set target_env=%1&goto arg-ok
if /i "%1"=="vs2013"       set target_env=%1&goto arg-ok
if /i "%1"=="vs2015"       set target_env=%1&goto arg-ok
if /i "%1"=="vs2017"       set target_env=%1&goto arg-ok
if /i "%1"=="noprojgen"    set noprojgen=1&goto arg-ok
if /i "%1"=="nobuild"      set nobuild=1&goto arg-ok
if /i "%1"=="x86"          set target_arch=ia32&set msbuild_platform=WIN32&set vs_toolset=x86&goto arg-ok
if /i "%1"=="ia32"         set target_arch=ia32&set msbuild_platform=WIN32&set vs_toolset=x86&goto arg-ok
if /i "%1"=="x64"          set target_arch=x64&set msbuild_platform=x64&set vs_toolset=x64&goto arg-ok
if /i "%1"=="shared"       set library=shared_library&goto arg-ok
if /i "%1"=="static"       set library=static_library&goto arg-ok
:arg-ok
shift
goto next-arg
:args-done

@rem Try detect current environment

set MSVS_VERSION=

if "%VisualStudioVersion%" EQU "14.0" (
  set MSVS_VERSION=2015
  goto :vs-detected
)

if "%VisualStudioVersion%" EQU "15.0" (
  set MSVS_VERSION=2017
  goto :vs-detected
)

@rem MSVS 2008/2010 do not define VisualStudioVersion
@rem Try detect MSVS based on compiler version
set VERSION_STRING=
for /f "delims=" %%i in ('cl 2^>^&1 ^| findstr "Compiler Version"') do (
  set VERSION_STRING=%%i
)

if "%VERSION_STRING%" EQU "" goto :vs-not-detected

if "%VERSION_STRING%" NEQ "%VERSION_STRING:Compiler Version 15=%" (
  set MSVS_VERSION=2008
  goto :vs-detected
)

if "%VERSION_STRING%" NEQ "%VERSION_STRING:Compiler Version 16=%" (
  set MSVS_VERSION=2010
  goto :vs-detected
)

if "%VERSION_STRING%" NEQ "%VERSION_STRING:Compiler Version 17=%" (
  set MSVS_VERSION=2012
  goto :vs-detected
)

if "%VERSION_STRING%" NEQ "%VERSION_STRING:Compiler Version 18=%" (
  set MSVS_VERSION=2013
  goto :vs-detected
)

if "%VERSION_STRING%" NEQ "%VERSION_STRING:Compiler Version 19.00=%" (
  set MSVS_VERSION=2015
  goto :vs-detected
)

if "%VERSION_STRING%" NEQ "%VERSION_STRING:Compiler Version 19.10=%" (
  set MSVS_VERSION=2017
  goto :vs-detected
)

:vs-detected

@rem We have no preference so use what we get
if "%target_env%" EQU "" (
  echo Found Visual Studio %MSVS_VERSION%
  set GYP_MSVS_VERSION=%MSVS_VERSION%
  goto select-target
)

@rem Env already set what we want so just use it
if "%target_env%" EQU "vs%MSVS_VERSION%" (
  echo Using Visual Studio %MSVS_VERSION%
  set GYP_MSVS_VERSION=%MSVS_VERSION%
  goto select-target
)

:vs-not-detected

@rem If target_env specified in command line then try find only this version and ignore any other

if "%target_env%" NEQ "" (
  echo Trying find %target_env% installation...
) else (
  echo Trying find MSVS installation...
)

:vs-implicit-version
@rem Look for Visual Studio 2017 only if explicitly requested.
if "%target_env%" NEQ "vs2017" goto vs-set-2015

:vs-set-2017
call tools\vswhere_usability_wrapper.cmd
if "_%VCINSTALLDIR%_" == "__" goto vs-set-2015
@rem Need to clear VSINSTALLDIR for vcvarsall to work as expected.
set vcvars_call="%VCINSTALLDIR%\Auxiliary\Build\vcvarsall.bat" %vs_toolset%
echo calling: %vcvars_call%
call %vcvars_call%
if %VSCMD_ARG_TGT_ARCH%==x64 set target_arch=x64&set msbuild_platform=x64&set vs_toolset=x64
set GYP_MSVS_VERSION=2017
echo Using Visual Studio 2017
goto select-target

if target_env == '' or target_env == 'vs'

:vs-set-2015
@rem Look for Visual Studio 2015
if not "%target_env%" EQU "" if not "%target_env%" EQU "vs2015" goto vc-set-2013
if not defined VS140COMNTOOLS goto vc-set-2013
if not exist "%VS140COMNTOOLS%\..\..\vc\vcvarsall.bat" goto vc-set-2013
call "%VS140COMNTOOLS%\..\..\vc\vcvarsall.bat" %vs_toolset%
set GYP_MSVS_VERSION=2015
echo Using Visual Studio 2015
goto select-target


:vc-set-2013
@rem Look for Visual Studio 2013
if not "%target_env%" EQU "" if not "%target_env%" EQU "vs2013" goto vc-set-2012
if not defined VS120COMNTOOLS goto vc-set-2012
if not exist "%VS120COMNTOOLS%\..\..\vc\vcvarsall.bat" goto vc-set-2012
call "%VS120COMNTOOLS%\..\..\vc\vcvarsall.bat" %vs_toolset%
set GYP_MSVS_VERSION=2013
echo Using Visual Studio 2013
goto select-target

:vc-set-2012
@rem Look for Visual Studio 2012
if not "%target_env%" EQU "" if not "%target_env%" EQU "vs2012" goto vc-set-2010
if not defined VS110COMNTOOLS goto vc-set-2010
if not exist "%VS110COMNTOOLS%\..\..\vc\vcvarsall.bat" goto vc-set-2010
call "%VS110COMNTOOLS%\..\..\vc\vcvarsall.bat" %vs_toolset%
set GYP_MSVS_VERSION=2012
echo Using Visual Studio 2012
goto select-target

:vc-set-2010
@rem Look for Visual Studio 2010
if not "%target_env%" EQU "" if not "%target_env%" EQU "vs2010" goto vc-set-2008
if not defined VS100COMNTOOLS goto vc-set-2008
if not exist "%VS100COMNTOOLS%\..\..\vc\vcvarsall.bat" goto vc-set-2008
call "%VS100COMNTOOLS%\..\..\vc\vcvarsall.bat" %vs_toolset%
set GYP_MSVS_VERSION=2010
echo Using Visual Studio 2010
goto select-target

:vc-set-2008
@rem Look for Visual Studio 2008
if not "%target_env%" EQU "" if not "%target_env%" EQU "vs2008" goto vc-set-notfound
if not defined VS90COMNTOOLS goto vc-set-notfound
if not exist "%VS90COMNTOOLS%\..\..\vc\vcvarsall.bat" goto vc-set-notfound
call "%VS90COMNTOOLS%\..\..\vc\vcvarsall.bat" %vs_toolset%
set GYP_MSVS_VERSION=2008
echo Using Visual Studio 2008
goto select-target

:vc-set-notfound
if "%target_env%" EQU "" (
  echo Warning: Visual Studio not found
) else (
  echo Warning: Visual Studio of version %target_env% not found
  exit /b -1
)

:select-target
if not "%config%"=="" goto project-gen
if "%run%"=="run-tests.exe" set config=Debug& goto project-gen
if "%run%"=="run-benchmarks.exe" set config=Release& goto project-gen
set config=Debug

:project-gen
@rem Skip project generation if requested.
if defined noprojgen goto msbuild

@rem Generate the VS project.
if exist build\gyp goto have_gyp
echo git clone https://chromium.googlesource.com/external/gyp build/gyp
git clone https://chromium.googlesource.com/external/gyp build/gyp
if errorlevel 1 goto gyp_install_failed
goto have_gyp

:gyp_install_failed
echo Failed to download gyp. Make sure you have git installed, or
echo manually install gyp into %~dp0build\gyp.
exit /b 1

:have_gyp
if not defined PYTHON set PYTHON=python
"%PYTHON%" gyp_uv.py -Dtarget_arch=%target_arch% -Duv_library=%library%
if errorlevel 1 goto create-msvs-files-failed
if not exist uv.sln goto create-msvs-files-failed
echo Project files generated.

:msbuild
@rem Skip project generation if requested.
if defined nobuild goto run

@rem Check if VS build env is available
if defined VCINSTALLDIR goto msbuild-found
if defined WindowsSDKDir goto msbuild-found
echo Build skipped. To build, this file needs to run from VS cmd prompt.
goto run

@rem Build the sln with msbuild.
:msbuild-found
msbuild uv.sln /t:%target% /p:Configuration=%config% /p:Platform="%msbuild_platform%" /clp:NoSummary;NoItemAndPropertyList;Verbosity=minimal /nologo
if errorlevel 1 exit /b 1

:run
@rem Run tests if requested.
if "%run%"=="" goto exit
if not exist %config%\%run% goto exit
echo running '%config%\%run%'
%config%\%run%
goto exit

:create-msvs-files-failed
echo Failed to create vc project files.
exit /b 1

:help

echo "vcbuild.bat [debug/release] [test/bench] [clean] [noprojgen] [nobuild] [vs2017] [x86/x64] [static/shared]"

echo Examples:
echo   vcbuild.bat              : builds debug build
echo   vcbuild.bat test         : builds debug build and runs tests
echo   vcbuild.bat release bench: builds release build and runs benchmarks
goto exit

:exit
