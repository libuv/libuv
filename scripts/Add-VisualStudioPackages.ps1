# run this in PowerShell as Administrator
# it will launch Visual Studio Installer and prompt you to add
# all packages required to build libuv

if (-not (Get-Module -ListAvailable -Name VSSetup)) {
    Install-Module VSSetup -Scope CurrentUser
}
$vs = Get-VSSetupInstance

# to see what packages are installed on your system, you can do:
#$vs.Packages | sort Type, Id | select Id, Type, Version

# MSBuild
# Visual C++ 2017 v141 toolset (x86,x64)
# Standard Library Modules
# Windows 10 SDK (10.0.14393.0)

& "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vs_installershell.exe" modify `
    --installPath $vs.InstallationPath `
    --add Microsoft.Build `
    --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    --add Microsoft.VisualStudio.Component.VC.Modules.x86.x64 `
    --add Microsoft.VisualStudio.Component.Windows10SDK.14393