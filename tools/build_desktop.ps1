param(
    [ValidateSet('Release','Debug')][string]$Configuration='Release',
    [string]$QtRoot='',
    [string]$MSBuild='',
    [switch]$Validation,
    [string]$QtMsBuild=''
)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'resolve_vs2022_toolchain.ps1')
$MSBuild=(Resolve-Vs2022Toolchain -MSBuild $MSBuild -RequireMSBuild).MSBuild
$QtRoot=Resolve-Qt611Root -QtRoot $QtRoot -MSBuild $MSBuild
$qtOptions=@("/p:QtInstall=$QtRoot")
if($QtMsBuild){$qtOptions+="/p:QtMsBuild=$((Resolve-Path -LiteralPath $QtMsBuild).Path)"}
$out=Join-Path $projectRoot "x64/$Configuration"
New-Item -ItemType Directory -Force $out | Out-Null
& (Join-Path $QtRoot 'bin/lrelease.exe') (Join-Path $projectRoot 'translations/hand_zh_CN.ts') -qm (Join-Path $projectRoot 'translations/hand_zh_CN.qm')
if($LASTEXITCODE){throw 'lrelease failed'}
$desktop=if($Validation){'CxwlDesktopValidation.exe'}else{'Cxwl Depth Camera.exe'}
& $MSBuild (Join-Path $projectRoot 'Cxwl Depth Camera.sln') /t:Rebuild "/p:Configuration=$Configuration" /p:Platform=x64 @qtOptions "/p:CxwlDesktopValidation=$($Validation.IsPresent.ToString().ToLowerInvariant())" /m:2 /nologo /v:minimal *> (Join-Path $out 'build.log')
if($LASTEXITCODE){Get-Content (Join-Path $out 'build.log') -Tail 45;throw 'Desktop rebuild failed'}
& (Join-Path $QtRoot 'bin/windeployqt.exe') "--$($Configuration.ToLower())" --no-translations --compiler-runtime (Join-Path $out $desktop) (Join-Path $out 'CxwlDepthSdk.dll') *> (Join-Path $out 'deploy.log')
if($LASTEXITCODE){Get-Content (Join-Path $out 'deploy.log') -Tail 25;throw 'Qt runtime deployment failed'}
$translations=Join-Path $out 'translations'
New-Item -ItemType Directory -Force -Path $translations | Out-Null
Copy-Item -LiteralPath (Join-Path $QtRoot 'translations/qtbase_zh_CN.qm') -Destination $translations -Force
foreach ($name in 'avcodec-61.dll','avformat-61.dll','avutil-59.dll','swresample-5.dll','swscale-8.dll') {
    Copy-Item -LiteralPath (Join-Path $QtRoot "bin/$name") -Destination $out -Force
}
Write-Output "$Configuration rebuilt: $out"
