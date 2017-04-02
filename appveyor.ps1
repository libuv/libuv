# change PlatformToolset from v140 to v141
$files = ".\libuv.vcxproj", ".\run-benchmarks.vcxproj", ".\run-tests.vcxproj"
ForEach ($file in $files) {
    [xml] $proj = Get-Content $file
    $nm = New-Object System.Xml.XmlNamespaceManager $proj.NameTable
    $nm.AddNamespace("msbuild", $proj.Project.NamespaceURI)
    $pt = $proj.SelectSingleNode("//msbuild:PlatformToolset", $nm)
    $pt.InnerText = "v141"
    $proj.Save($file)
}