
# gyp doesn't yet support Visual Studio 2017
# This file modifies a 2015 project file to work with 2017.
# vsbuild.bat creates the project file and builds it.
# You can have it only create the project files & then run this script.
# See appveyor.yml as an example.

$files = "libuv.vcxproj", "run-benchmarks.vcxproj", "run-tests.vcxproj"
ForEach ($file in $files) {
    $file = (Resolve-Path $file).Path # full path
    [xml] $proj = Get-Content $file
    $nm = New-Object System.Xml.XmlNamespaceManager $proj.NameTable
    $ns = $proj.Project.NamespaceURI
    $nm.AddNamespace("msbuild", $ns)
    
    # change PlatformToolset from v140 to v141
    $pt = $proj.SelectSingleNode("//msbuild:PlatformToolset", $nm)
    $pt.InnerText = "v141"

    # use Windows 10 SDK 10.0.14393.0
    $wtpv = $proj.CreateElement("WindowsTargetPlatformVersion", $ns)
    $wtpv.InnerText = "10.0.14393.0"
    $pg = $proj.SelectSingleNode("//msbuild:PropertyGroup[@Label='Globals']", $nm)
    $pg.AppendChild($wtpv)

    $proj.Save($file)
}