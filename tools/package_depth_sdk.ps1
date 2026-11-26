param(
    [Parameter(Mandatory)][string]$BinaryDirectory,
    [Parameter(Mandatory)][string]$RuntimePayload,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [string]$DocumentationDirectory = '',
    [string]$QtRoot = '',
    [string]$ExampleBuildDirectory = '',
    [switch]$ValidateExamplesOnly
)
$ErrorActionPreference = 'Stop'
$project = Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'resolve_vs2022_toolchain.ps1')
$vs = Resolve-Vs2022Toolchain -RequireMSBuild -RequireDumpBin
$QtRoot = Resolve-Qt611Root -QtRoot $QtRoot -MSBuild $vs.MSBuild
$product = Get-Content -LiteralPath (Join-Path $project 'sdk/product.json') -Raw | ConvertFrom-Json
& (Join-Path $PSScriptRoot 'generate_sdk_version.ps1') -Check
$binaries = (Resolve-Path -LiteralPath $BinaryDirectory).Path
$runtime = (Resolve-Path -LiteralPath $RuntimePayload).Path
$output = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $output) { throw 'Choose a new SDK output directory.' }
$manifestPath = Join-Path $output 'sdk-manifest.json'
$manifest = [ordered]@{
    sdk_version = $product.sdk_version
    display_version = $product.display_version
    release_date = $product.release_date
    product = $product.product
    abi_version = $product.abi_version
    platform = 'windows-x64'
    channel = 'windows-device-service-preview'
    device_service = '1.0.0-preview.1'
    package_revision = 2
}

$sdkBinaries = @('CxwlDepthSdk.dll', 'CxwlDepthSdkExample.exe')
$qtLibraries = @('Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll', 'Qt6Network.dll')
$codecLibraries = @('avcodec-61.dll', 'avformat-61.dll', 'avutil-59.dll', 'swresample-5.dll', 'swscale-8.dll')
foreach ($name in $sdkBinaries + @('CxwlDepthSdk.lib')) {
    if (-not (Test-Path -LiteralPath (Join-Path $binaries $name) -PathType Leaf)) { throw "Missing Release SDK build output $name" }
}
foreach ($name in $qtLibraries + $codecLibraries + @('vcruntime140.dll', 'msvcp140.dll')) {
    if (-not (Test-Path -LiteralPath (Join-Path $runtime $name) -PathType Leaf)) { throw "Missing staged runtime file $name" }
}
foreach ($name in @('licenses')) {
    if (-not (Test-Path -LiteralPath (Join-Path $runtime $name) -PathType Container)) { throw "Missing staged runtime directory $name" }
}
$bin = Join-Path $output 'SDK/bin'
$include = Join-Path $output 'SDK/include'
$lib = Join-Path $output 'SDK/lib'
$examples = Join-Path $output 'Example/c/QuickStart'
New-Item -ItemType Directory -Path $bin, $include, $lib, $examples | Out-Null
Copy-Item -LiteralPath (Join-Path $binaries 'CxwlDepthSdk.dll') -Destination $bin
Copy-Item -LiteralPath (Join-Path $binaries 'CxwlDepthSdk.lib') -Destination $lib
foreach ($name in 'DepthSdk.h','DepthSdk.hpp','DepthSdkVersion.h') {
    Copy-Item -LiteralPath (Join-Path $project ('sdk/' + $name)) -Destination $include
}
Copy-Item -LiteralPath (Join-Path $project 'sdk/DepthSdkExample.c') -Destination (Join-Path $examples 'QuickStart.c')
Copy-Item -LiteralPath (Join-Path $project 'tools/sdk_package/README.md') -Destination (Join-Path $output 'README.md')
Copy-Item -LiteralPath (Join-Path $project 'LICENSE') -Destination $output
Copy-Item -LiteralPath (Join-Path $runtime 'licenses') -Destination (Join-Path $output 'Licenses') -Recurse
Get-ChildItem -LiteralPath $runtime | Where-Object {
    $_.Name -notin 'Cxwl Depth Camera.exe','触想工作台.exe','CxwlDepthViewer.exe','opengl32sw.dll','D3Dcompiler_47.dll','dxcompiler.dll','dxil.dll'
} | Copy-Item -Destination $bin -Recurse -Force
Copy-Item -LiteralPath (Join-Path $project 'LICENSE') -Destination (Join-Path $bin 'licenses/ProductLicense.txt')
if ((Get-FileHash (Join-Path $binaries 'CxwlDepthSdk.dll')).Hash -ne (Get-FileHash (Join-Path $bin 'CxwlDepthSdk.dll')).Hash) {
    throw 'Viewer and SDK must come from the same build.'
}
$exampleRoot = Join-Path $output 'Example'
$exampleBin = Join-Path $exampleRoot 'bin'
New-Item -ItemType Directory -Path $exampleBin | Out-Null
# Example/bin contains small launchers; all applications and dependencies live once in SDK/bin.
foreach ($name in 'DepthQuality','DeviceManagement','PointCloud') {
    $directory = Join-Path $exampleRoot "c/$name"
    New-Item -ItemType Directory -Path $directory | Out-Null
    Copy-Item -LiteralPath (Join-Path $project "sdk/examples/$name.c") -Destination $directory
}
$template = Join-Path $project 'tools/sdk_package'
$common = Join-Path $exampleRoot 'common'
New-Item -ItemType Directory -Path $common | Out-Null
foreach ($name in 'SampleSupport.h','SampleJson.h','ExpectedExports.h') {
    Copy-Item -LiteralPath (Join-Path $project "sdk/examples/$name") -Destination $common
}
foreach ($name in 'ProductVersion.rc','DepthSdkVersion.h') {
    Copy-Item -LiteralPath (Join-Path $project "sdk/$name") -Destination $common
}
foreach ($pair in @(@('cpp/QuickStart','CppQuickStart.cpp'),@('cpp/ThreadedCapture','ThreadedCapture.cpp'),@('c/SelfTest','SelfTest.c'))) {
    $destination = Join-Path $exampleRoot $pair[0]
    New-Item -ItemType Directory -Path $destination | Out-Null
    Copy-Item -LiteralPath (Join-Path $project ('sdk/examples/' + $pair[1])) -Destination $destination
}
Copy-Item -LiteralPath (Join-Path $template 'Example.LICENSE') -Destination (Join-Path $exampleRoot 'LICENSE')
Copy-Item -LiteralPath (Join-Path $template 'Example.README.md') -Destination (Join-Path $exampleRoot 'README.md')

Copy-Item -LiteralPath (Join-Path $template 'Examples.CMakeLists.txt') -Destination (Join-Path $exampleRoot 'CMakeLists.txt')
$cmake = Join-Path $output 'SDK/cmake'
New-Item -ItemType Directory -Path $cmake | Out-Null
Copy-Item -LiteralPath (Join-Path $template 'CxwlDepthSdkConfig.cmake'),(Join-Path $template 'DeployRuntime.cmake'),(Join-Path $template 'RuntimeFunctions.cmake') -Destination $cmake
Copy-Item -LiteralPath (Join-Path $template 'SDK.CMakeLists.txt') -Destination (Join-Path $output 'SDK/CMakeLists.txt')
$launchers = Join-Path $exampleRoot 'Launchers'
New-Item -ItemType Directory -Path $launchers | Out-Null
Copy-Item -LiteralPath (Join-Path $template 'Launcher.cpp') -Destination $launchers
Copy-Item -LiteralPath (Join-Path $template 'Launchers.CMakeLists.txt') -Destination (Join-Path $launchers 'CMakeLists.txt')
$script = Join-Path $output 'Script'
New-Item -ItemType Directory -Path $script | Out-Null
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'generate_runtime_components.py') -Destination $script
& python (Join-Path $script 'generate_runtime_components.py')
if ($LASTEXITCODE) { throw 'Cannot generate runtime components.' }
(Get-Content -LiteralPath (Join-Path $template 'CxwlDepthSdkConfigVersion.cmake.in') -Raw).Replace('@SDK_VERSION@', $product.sdk_version) |
    Set-Content -LiteralPath (Join-Path $cmake 'CxwlDepthSdkConfigVersion.cmake') -Encoding utf8NoBOM
Copy-Item -LiteralPath (Join-Path $template 'CHANGELOG.md') -Destination $output
Copy-Item -LiteralPath (Join-Path $template 'integration') -Destination (Join-Path $output 'Integration') -Recurse

# Assemble only public GUI consumers from the non-validation desktop file list.
$viewer = Join-Path $exampleRoot 'QtViewer'
New-Item -ItemType Directory -Path $viewer | Out-Null
[xml]$desktopProject = Get-Content -LiteralPath (Join-Path $project 'Cxwl Depth Camera.vcxproj') -Raw
$ns = [Xml.XmlNamespaceManager]::new($desktopProject.NameTable)
$ns.AddNamespace('m', 'http://schemas.microsoft.com/developer/msbuild/2003')
$nodes = $desktopProject.SelectNodes('/m:Project/m:ItemGroup[not(@Condition)]/*[@Include]', $ns)
$sources = [Collections.Generic.List[string]]::new()
function Copy-ViewerFile([string]$Name) {
    if ([IO.Path]::IsPathRooted($Name) -or ($Name -split '[/\\]') -contains '..') { throw "Invalid viewer source: $Name" }
    $destination = Join-Path $viewer $Name
    New-Item -ItemType Directory -Force -Path (Split-Path $destination -Parent) | Out-Null
    Copy-Item -LiteralPath (Join-Path $project $Name) -Destination $destination
}
foreach ($node in $nodes) {
    if ($node.LocalName -notin 'ClCompile','ClInclude','QtMoc','QtRcc','ResourceCompile','None') { continue }
    $name = ([string]$node.Include).Replace('\', '/')
    if ($name.StartsWith('sdk/') -and $name -notin 'sdk/DepthSdkVersion.h','sdk/ProductVersion.rc') { continue }
    if ($name -eq 'main.cpp') {
        Copy-Item -LiteralPath (Join-Path $template 'QtViewer.main.cpp') -Destination (Join-Path $viewer 'main.cpp')
        $sources.Add($name)
        continue
    }
    Copy-ViewerFile $name
    if ($node.LocalName -ne 'None') { $sources.Add($name) }
    if ($node.LocalName -eq 'QtRcc') {
        [xml]$qrc = Get-Content -LiteralPath (Join-Path $project $name) -Raw
        foreach ($resource in $qrc.SelectNodes('//file')) { Copy-ViewerFile $resource.InnerText }
    }
}
$sourceLines = @('set(CXWL_VIEWER_SOURCES') + @($sources | Sort-Object -Unique | ForEach-Object { '  "' + $_ + '"' }) + @(')')
$sourceLines | Set-Content -LiteralPath (Join-Path $viewer 'sources.cmake') -Encoding utf8NoBOM
Copy-Item -LiteralPath (Join-Path $template 'QtViewer.CMakeLists.txt') -Destination (Join-Path $viewer 'CMakeLists.txt')
Copy-Item -LiteralPath (Join-Path $template 'QtViewer.README.md') -Destination (Join-Path $viewer 'README.md')
if (Test-Path -LiteralPath (Join-Path $project 'docs/sdk/images')) {
    Copy-Item -LiteralPath (Join-Path $project 'docs/sdk/images') -Destination $viewer -Recurse
}

if (!$ValidateExamplesOnly) {
    if (!$DocumentationDirectory) {
        $DocumentationDirectory = Join-Path $project 'x64/validation/depth-sdk-doxygen'
        & (Join-Path $PSScriptRoot 'build_depth_sdk_docs.ps1') -OutputDirectory $DocumentationDirectory
    }
    $doc = Join-Path $output 'Doc/api/Chinese'
    New-Item -ItemType Directory -Path $doc | Out-Null
    Get-ChildItem -LiteralPath $DocumentationDirectory | Where-Object { $_.Name -notin 'xml','warnings.log' } |
        Copy-Item -Destination $doc -Recurse
    if (!(Test-Path -LiteralPath (Join-Path $doc 'index.html'))) { throw 'Doxygen reference is missing.' }
    if (!(Test-Path -LiteralPath (Join-Path $doc '点云录制数据读取说明.pdf') -PathType Leaf)) { throw 'Customer point cloud recording guide is missing.' }
}
Copy-Item -LiteralPath (Join-Path $project 'tools/diagnose_cxwl_depth_camera_usb.ps1'),(Join-Path $template 'verify-sdk.ps1'),(Join-Path $template 'build-examples.ps1') -Destination $script
& python (Join-Path $PSScriptRoot 'audit_sdk_runtime.py') --root $runtime --output (Join-Path $output 'Licenses/runtime-license-audit.json')
if ($LASTEXITCODE) { throw 'Runtime license/module audit failed.' }

# Compile every C/C++ example from the assembled customer package, outside the archive.
$cmakeExecutable = Join-Path $vs.VisualStudioRoot 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
if (!(Test-Path -LiteralPath $cmakeExecutable)) { $cmakeExecutable = (Get-Command cmake -ErrorAction Stop).Source }
$sampleBuild = if ($ExampleBuildDirectory) { [IO.Path]::GetFullPath($ExampleBuildDirectory) }
    else { Join-Path $project ('x64/validation/sdk-samples-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff')) }
$manifest | ConvertTo-Json | Set-Content -LiteralPath $manifestPath -Encoding utf8NoBOM
& (Join-Path $script 'build-examples.ps1') -BuildDirectory $sampleBuild -CMake $cmakeExecutable -WithViewer -QtRoot $QtRoot -RunOfflineTests
# The CI staging directory is used only to build and run these consumer examples.
if ($ValidateExamplesOnly) { return }
foreach ($name in 'QuickStart','Quality','DeviceManagement','PointCloud','CppQuickStart','ThreadedCapture','SelfTest') {
    $profile = if ($name -eq 'SelfTest') { 'MINIMAL' } else { 'STANDARD' }
    Copy-Item -LiteralPath (Join-Path $sampleBuild "bin/$profile/Release/CxwlDepth$name.exe") -Destination $bin -Force
}
Copy-Item -LiteralPath (Join-Path $sampleBuild 'bin/FULL/Release/CxwlDepthViewer.exe') -Destination $bin -Force
& python (Join-Path $script 'generate_runtime_components.py') --check
if ($LASTEXITCODE) { throw 'Cannot reproduce both runtime component manifests.' }
$exampleReport = Get-Content -LiteralPath (Join-Path $sampleBuild 'build-examples.json') -Raw | ConvertFrom-Json -AsHashtable
# Record commands relative to the extracted package, including the tested example executables.
function Convert-ExamplePath([string]$Value) {
    $Value.Replace('\', '/').Replace($output.Replace('\', '/'), '.').Replace(
        $sampleBuild.Replace('\', '/'), '../cxwl-build').Replace($QtRoot.Replace('\', '/'), '$env:CXWL_QT_ROOT')
}
foreach ($command in $exampleReport.commands) {
    $command.program = Convert-ExamplePath $command.program
    $command.arguments = @($command.arguments | ForEach-Object { Convert-ExamplePath $_ })
}
$exampleReport.working_directory = 'Extracted SDK root'
$exampleReport.qt_root = 'Set CXWL_QT_ROOT to the Qt 6.11.1 MSVC x64 installation directory.'
$exampleReport | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $output 'Doc/example-build-validation.json') -Encoding utf8NoBOM
$selfTestReport = Get-Content -LiteralPath (Join-Path $sampleBuild 'offline-output/self-test.json') -Raw | ConvertFrom-Json
[ordered]@{
    sdk_version = $selfTestReport.sdk_version
    abi_version = $selfTestReport.abi_version
    configuration = 'Release'
    runtime_profile = 'MINIMAL'
    scenario = 'SelfTest: process creation to offline projection and JSON report sampling'
    sampled_at_utc = (Get-Item (Join-Path $sampleBuild 'offline-output/self-test.json')).LastWriteTimeUtc.ToString('o')
    sample_count = 1
    cache_state = 'uncontrolled'
    environment = @{ platform = $selfTestReport.environment.platform; windows_version = $selfTestReport.environment.windows_version }
    process = $selfTestReport.process
    hardware_connected_for_validation = $false
} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $output 'Doc/host-profile.json') -Encoding utf8NoBOM
$launcherBuild = Join-Path $sampleBuild 'launchers'
& $cmakeExecutable -S $launchers -B $launcherBuild -G 'Visual Studio 17 2022' -A x64
if ($LASTEXITCODE) { throw 'Launcher configuration failed.' }
& $cmakeExecutable --build $launcherBuild --config Release
if ($LASTEXITCODE) { throw 'Launcher build failed.' }
Get-ChildItem -LiteralPath (Join-Path $launcherBuild 'Release') -Filter '*.exe' | Copy-Item -Destination $exampleBin

# Customer DLLs must not accidentally expose development-only Qt diagnostics.
$dumpbin = $vs.DumpBin
$exports = @(& $dumpbin /nologo /exports (Join-Path $bin 'CxwlDepthSdk.dll'))
if ($LASTEXITCODE) { throw 'Cannot inspect SDK exports.' }
$actual = @($exports | ForEach-Object { if ($_ -match '^\s+\d+\s+[0-9A-F]+\s+[0-9A-F]+\s+(\S+)') { $Matches[1] } })
$header = Get-Content -LiteralPath (Join-Path $include 'DepthSdk.h') -Raw
$expected = @([regex]::Matches($header, 'CXWL_DEPTH_API\s+\w+\s+CXWL_DEPTH_CALL\s+(CxwlDepth_\w+)\s*\(') | ForEach-Object { $_.Groups[1].Value })
if (@(Compare-Object ($expected | Sort-Object) ($actual | Sort-Object)).Count) { throw 'SDK public export set differs from the header. Use CxwlInstallerBuild=true.' }
$declaredExports = @([regex]::Matches((Get-Content -LiteralPath (Join-Path $common 'ExpectedExports.h') -Raw), '"(CxwlDepth_\w+)"') | ForEach-Object { $_.Groups[1].Value })
if (@(Compare-Object ($expected | Sort-Object) ($declaredExports | Sort-Object)).Count) { throw 'SelfTest export list differs from the public header.' }
$verifiedResources = [Collections.Generic.List[string]]::new()
foreach ($binary in @((Join-Path $bin 'CxwlDepthSdk.dll')) +
    @(Get-ChildItem -LiteralPath $exampleBin,$bin -Filter 'CxwlDepth*.exe' | ForEach-Object FullName)) {
    $verifiedResources.Add([IO.Path]::GetRelativePath($output, $binary).Replace('\','/'))
    $version = (Get-Item -LiteralPath $binary).VersionInfo
    if ($version.ProductVersion -ne $product.sdk_version -or $version.CompanyName -or
        $version.ProductName -ne $product.product) { throw "Binary version metadata mismatch: $binary" }
}
& (Join-Path $exampleBin 'CxwlDepthSelfTest.exe')
if ($LASTEXITCODE) { throw 'Packaged SDK offline self-test failed.' }
& python -E -B (Join-Path $project 'tests/test_depth_sdk_abi.py') --dll (Join-Path $bin 'CxwlDepthSdk.dll') --output (Join-Path $sampleBuild 'sdk-abi')
if ($LASTEXITCODE) { throw 'Packaged SDK ABI and legacy device registry checks failed.' }
$dllHash = (Get-FileHash -LiteralPath (Join-Path $bin 'CxwlDepthSdk.dll')).Hash.ToLowerInvariant()
$liveEvidence = 'Doc/api/Chinese/measurements/sdk-' + $product.sdk_version + '-outputs.json'
$livePath = Join-Path $output $liveEvidence
$verifiedOnCamera = $false
if (Test-Path -LiteralPath $livePath -PathType Leaf) {
    $live = Get-Content -LiteralPath $livePath -Raw | ConvertFrom-Json
    $modes = @($live.modes)
    $components = Get-Content -LiteralPath (Join-Path $output 'SDK/runtime-components.json') -Raw | ConvertFrom-Json
    [string[]]$runtimeNames = $components.profiles.STANDARD.files
    [array]::Sort($runtimeNames, [StringComparer]::Ordinal)
    $digestRows = foreach ($name in $runtimeNames) {
        $digest = (Get-FileHash -LiteralPath (Join-Path $bin $name)).Hash.ToLowerInvariant()
        "$name`t$digest`n"
    }
    $runtimeHash = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes(($digestRows -join '')))).ToLowerInvariant()
    $verifiedOnCamera = $live.standard_runtime_sha256 -ceq $runtimeHash -and $live.dll_sha256 -ceq $dllHash -and $live.sdk_version -eq $product.sdk_version -and
        $live.abi_version -eq $product.abi_version -and $modes.Count -eq 6 -and
        @('board','high','8x8','8x8-high','16x16','16x16-high' | Where-Object { $_ -notin $modes.mode }).Count -eq 0 -and
        @($modes | Where-Object { !$_.stopped -or !$_.released -or !$_.single_generation -or !$_.ply_matches_board_bytes -or
            $_.rgb_frames -le 0 -or $_.depth_frames -le 0 -or $_.quality_frames -le 0 -or $_.board_clouds -ne $_.depth_frames }).Count -eq 0
}
[ordered]@{
    sdk_version = $product.sdk_version
    platform = 'windows-x64'
    tested_windows_version = [Environment]::OSVersion.Version.ToString()
    compiler = 'Visual Studio 2022 v143 x64'
    configuration = 'Release'
    examples_built = @('QuickStart','Quality','DeviceManagement','PointCloud','CppQuickStart','ThreadedCapture','SelfTest')
    viewer_built_from_packaged_sources = $true
    reproduce = 'powershell -File Script/build-examples.ps1 -BuildDirectory ../cxwl-build -WithViewer -QtRoot $env:CXWL_QT_ROOT -RunOfflineTests'
    build_commands = 'Doc/example-build-validation.json'
    public_exports = $expected.Count
    evidence = [ordered]@{
        dll_sha256 = $dllHash
        hardware_output_report = if ($verifiedOnCamera) { $liveEvidence } else { $null }
        exported_names = @($actual | Sort-Object)
        version_checked_files = $verifiedResources.ToArray()
        runtime_audit = 'Licenses/runtime-license-audit.json'
        example_results = 'Doc/example-build-validation.json'
    }
    export_header_dll_match = $true
    binary_version_resources_match = $true
    runtime_license_module_audit_passed = $true
    offline_self_test_passed = $true
    offline_abi_and_legacy_registry_passed = $true
    hardware_connected_during_package_build = $false
    hardware_connected_for_validation = $verifiedOnCamera
    validation_scope = 'api/Chinese/sdk_specifications.html#sdk_validation_scope'
    scope = 'Offline package build and SelfTest. Hardware output verification uses the separate report for the matching DLL hash.'
} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $output 'Doc/build-validation.json') -Encoding utf8NoBOM
$runtimeSizes = Get-Content -LiteralPath (Join-Path $output 'SDK/runtime-components.json') -Raw | ConvertFrom-Json
[ordered]@{
    sdk_version = $product.sdk_version
    file_bytes_excluding_final_manifests = (Get-ChildItem -LiteralPath $output -File -Recurse |
        Where-Object { $_.FullName -ne $manifestPath } | Measure-Object Length -Sum).Sum
    profiles = $runtimeSizes.profiles
    recommended_free_space_bytes = 314572800
    recommendation_basis = 'Room for the extracted package and temporary extraction/upgrade copies; example builds and captured data need additional space.'
} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $output 'Doc/package-size.json') -Encoding utf8NoBOM
$hashes = [ordered]@{}
foreach ($file in Get-ChildItem -LiteralPath $output -File -Recurse | Sort-Object FullName) {
    if ($file.FullName -eq $manifestPath) { continue }
    $name = [IO.Path]::GetRelativePath($output, $file.FullName).Replace('\', '/')
    $hashes[$name] = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
}
$manifest.exported_functions = $expected.Count
$manifest.sha256 = $hashes
$manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $manifestPath -Encoding utf8NoBOM
# Run the customer entry point on the finalized package. Its exit must not terminate this script's caller.
& (Get-Process -Id $PID).Path -NoProfile -File (Join-Path $script 'verify-sdk.ps1') -Root $output -Strict
if ($LASTEXITCODE) { throw "Final strict package verification failed ($LASTEXITCODE)." }
Write-Output $output
