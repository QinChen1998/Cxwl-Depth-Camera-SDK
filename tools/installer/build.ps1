param(
    [Parameter(Mandatory)][string]$BuildDirectory,
    [string]$QtRoot='',
    [string]$MSBuild='',
    [string]$Version='1.1.8',
    [switch]$StageOnly,
    [string]$Iscc='',
    [string]$ChineseMessages='',
    [switch]$UseStagedPayload
)
$ErrorActionPreference='Stop'
$project=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
. (Join-Path $project 'tools/resolve_vs2022_toolchain.ps1')
$work=[IO.Path]::GetFullPath($BuildDirectory)
$validation=Join-Path $project 'x64\validation'
if(-not $work.StartsWith($validation+'\',[StringComparison]::OrdinalIgnoreCase)){
    throw 'Use an isolated subdirectory of x64/validation.'
}
if($Version -notmatch '^\d+\.\d+\.\d+(\.\d+)?$'){throw 'Invalid installer version.'}
$payload=Join-Path $work 'payload'
$exe='Cxwl Depth Camera.exe'
$desktopFiles=@($exe,'CxwlDepthSdk.dll')
$imagingSchemes=(Get-Content -LiteralPath (Join-Path $project 'DCTS_APP.json') -Raw | ConvertFrom-Json).imaging_schemes
$expectedImagingSchemes='board_native_tof48','board_native_tof48_high_accuracy'
if(($imagingSchemes -join ',') -cne ($expectedImagingSchemes -join ',')){throw 'Unsupported desktop imaging menu.'}
$desktopReceipt=Join-Path $work 'desktop-imaging.json'

function Invoke-Native([string]$Program,[string[]]$Arguments){
    & $Program @Arguments
    if($LASTEXITCODE -ne 0){throw "$Program failed ($LASTEXITCODE)"}
}
function Digest([string]$Path){
    (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}
function Copy-Relative([string]$Source,[string]$Destination,[string]$Name){
    if($Name -notmatch '^[A-Za-z0-9_. /-]+$' -or ($Name -split '/') -contains '..' -or $Name.StartsWith('/')){
        throw "Unexpected manifest path $Name"
    }
    $target=Join-Path $Destination $Name
    New-Item -ItemType Directory -Path (Split-Path $target) -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $Source $Name) -Destination $target
}
function Write-Json([string]$Path,$Value){
    $Value | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $Path -Encoding utf8NoBOM
}

if($UseStagedPayload){
    if(-not (Test-Path -LiteralPath $desktopReceipt -PathType Leaf)){
        throw 'Stage the desktop from current source before reusing this payload.'
    }
    $receipt=Get-Content -LiteralPath $desktopReceipt -Raw | ConvertFrom-Json -AsHashtable
    if(($receipt.imaging_schemes -join ',') -cne ($imagingSchemes -join ',')){
        throw 'The staged desktop has a different imaging menu. Stage it again from current source.'
    }
    foreach($name in $desktopFiles){
        $file=Join-Path $payload $name
        if(-not (Test-Path -LiteralPath $file -PathType Leaf) -or (Digest $file) -ne $receipt.files[$name]){
            throw "Staged desktop fingerprint mismatch: $name"
        }
    }
}

if(-not $UseStagedPayload){
    if(Test-Path -LiteralPath $payload){throw 'The payload already exists. Choose a fresh build directory.'}
    $toolchain=Resolve-Vs2022Toolchain -MSBuild $MSBuild -RequireMSBuild -RequireRedist
    $qt=Resolve-Qt611Root -QtRoot $QtRoot -MSBuild $toolchain.MSBuild
    $desktop=Join-Path $work 'desktop'
    New-Item -ItemType Directory -Force -Path $desktop | Out-Null

    Invoke-Native (Join-Path $qt 'bin/lrelease.exe') @(
        (Join-Path $project 'translations/hand_zh_CN.ts'),'-qm',(Join-Path $project 'translations/hand_zh_CN.qm'))
    Invoke-Native $toolchain.MSBuild @(
        (Join-Path $project 'Cxwl Depth Camera.sln'),'/t:Build','/p:Configuration=Release','/p:Platform=x64',
        "/p:QtInstall=$qt",'/p:CxwlInstallerBuild=true',"/p:OutDir=$desktop/","/p:CxwlIntermediateRoot=$work/obj",
        '/m:2','/nologo','/v:minimal',"/flp:logfile=$work/build.log;verbosity=normal")

    New-Item -ItemType Directory -Path $payload | Out-Null
    Copy-Item -LiteralPath (Join-Path $desktop $exe) -Destination $payload
    Copy-Relative $desktop $payload 'CxwlDepthSdk.dll'
    Invoke-Native (Join-Path $qt 'bin/windeployqt.exe') @(
        '--release','--no-translations','--no-compiler-runtime',
        (Join-Path $payload $exe),(Join-Path $payload 'CxwlDepthSdk.dll'))
    Copy-Relative $qt $payload 'translations/qtbase_zh_CN.qm'
    Get-ChildItem -LiteralPath $toolchain.RedistDirectories[0] -Filter '*.dll' | Copy-Item -Destination $payload
    foreach($name in 'avcodec-61.dll','avformat-61.dll','avutil-59.dll','swresample-5.dll','swscale-8.dll'){
        Copy-Relative (Join-Path $qt "bin") $payload $name
    }

    $notices=Join-Path $payload 'licenses'
    Copy-Relative (Join-Path $project 'third_party/ffmpeg_7_1_3') $notices 'COPYING.LGPLv2.1'
    Copy-Relative $PSScriptRoot $notices 'Third-Party-Notices.txt'
    $qtSource=Join-Path (Split-Path $qt) 'Src'
    foreach($license in 'LGPL-3.0-only.txt','GPL-3.0-only.txt') {
        Copy-Relative (Join-Path $qtSource 'qtbase/LICENSES') $notices $license
    }
    $sbom=Join-Path $qt 'sbom'
    foreach($module in 'qtbase','qtsvg','qtimageformats'){
        Copy-Relative $sbom (Join-Path $notices 'qt-sbom') "$module-6.11.1.spdx"
    }
}

foreach($name in $desktopFiles){
    if(-not (Test-Path -LiteralPath (Join-Path $payload $name) -PathType Leaf)){
        throw "Missing application executable in staged payload ($name)."
    }
}
foreach($name in 'Qt6Widgets.dll','Qt6Gui.dll'){
    if(-not (Test-Path -LiteralPath (Join-Path $payload $name) -PathType Leaf)){
        throw "Missing viewer runtime dependency in staged payload ($name)."
    }
}

$unexpected=Get-ChildItem -LiteralPath $payload -File -Recurse | Where-Object {
    $_.Name -match '^(README|AGENTS)(\..*)?$|^PackModelCopy\.exe$|model_key_material' -or
    $_.Name -match '^(DctsInferenceWorker|DctsModelRuntime|onnxruntime.*|directml|openvino.*|nvinfer.*|cudnn.*|cublas.*|cufft.*|nvrtc.*|Qt6SerialPort)\.(dll|exe)$' -or
    $_.Extension -match '^\.(onnx|ovm|rknn|dctsm|pt|pth|pdb|ilk|lib|exp|vcxproj|py)$'
}
if($unexpected){throw "Unexpected payload content: $($unexpected.FullName -join ', ')"}
if(-not $UseStagedPayload){
    $desktopHashes=@{}
    foreach($name in $desktopFiles){$desktopHashes[$name]=Digest (Join-Path $payload $name)}
    Write-Json $desktopReceipt @{imaging_schemes=$imagingSchemes;files=$desktopHashes}
}
if($StageOnly){return}
if(-not $Iscc -or -not $ChineseMessages){throw 'Provide -Iscc and -ChineseMessages for installer compilation.'}
$out=Join-Path $work 'delivery'
New-Item -ItemType Directory -Force $out | Out-Null
$icon=(Resolve-Path -LiteralPath (Join-Path $project 'assets/cx.ico')).Path
Invoke-Native (Resolve-Path -LiteralPath $Iscc).Path @(
    "/DVersion=$Version","/DOutput=$out","/DPayload=$payload","/DIconPath=$icon",
    "/DChineseMessages=$((Resolve-Path -LiteralPath $ChineseMessages).Path)",(Join-Path $PSScriptRoot 'Dcts.iss'))
$installer=Join-Path $out "Cxwl Depth Camera-内测版-v$Version-x64.exe"
"$(Digest $installer) *$(Split-Path $installer -Leaf)" | Set-Content -LiteralPath "$installer.sha256" -Encoding utf8NoBOM
Get-Item -LiteralPath $installer
