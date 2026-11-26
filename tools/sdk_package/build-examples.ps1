param(
    [string]$BuildDirectory = '',
    [ValidateSet('Release','Debug')][string]$Configuration = 'Release',
    [string]$CMake = '',
    [switch]$WithViewer,
    [string]$QtRoot = '',
    [switch]$RunOfflineTests
)
$ErrorActionPreference = 'Stop'
$package = Split-Path $PSScriptRoot -Parent
$manifest = Get-Content -LiteralPath (Join-Path $package 'sdk-manifest.json') -Raw | ConvertFrom-Json
if ($manifest.sdk_version -notmatch '^\d+\.\d+\.\d+$') { throw 'Invalid SDK version in sdk-manifest.json.' }
if (-not $BuildDirectory) { $BuildDirectory = Join-Path ([IO.Path]::GetTempPath()) "CXWLDepthSDK-$($manifest.sdk_version)-examples" }
$build = [IO.Path]::GetFullPath($BuildDirectory)
$packagePath = [IO.Path]::GetFullPath($package).TrimEnd('\')
if ($build.Equals($packagePath, [StringComparison]::OrdinalIgnoreCase) -or
    $build.StartsWith($packagePath + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Choose a build directory outside the SDK package to preserve its integrity manifest.'
}
if (-not $CMake) {
    $command = Get-Command cmake -ErrorAction SilentlyContinue
    if ($command) { $CMake = $command.Source }
    else {
        $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
        $vsRoot = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        $CMake = Join-Path $vsRoot 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
    }
}
if (-not (Test-Path -LiteralPath $CMake -PathType Leaf)) { throw 'Install CMake 3.21+ or pass -CMake.' }
$configure = @('-S', (Join-Path $package 'Example'), '-B', $build, '-G', 'Visual Studio 17 2022', '-A', 'x64', '-DCMAKE_SYSTEM_VERSION=10.0.22621.0')
if ($WithViewer) {
    if (-not $QtRoot) { throw '-WithViewer requires -QtRoot pointing to Qt 6.11.1 MSVC x64.' }
    $configure += @('-DCXWL_BUILD_QT_VIEWER=ON', "-DCMAKE_PREFIX_PATH=$QtRoot")
} else { $configure += '-DCXWL_BUILD_QT_VIEWER=OFF' }
$commands = @()
function Invoke-Checked([string[]]$Arguments) {
    & $CMake @Arguments
    if ($LASTEXITCODE) { throw "CMake failed ($LASTEXITCODE)." }
}
Invoke-Checked $configure
$commands += @{ program = 'cmake'; arguments = $configure; exit_code = 0 }
$buildArguments = @('--build', $build, '--config', $Configuration, '--parallel', '1', '--', '/nodeReuse:false')
Invoke-Checked $buildArguments
$commands += @{ program = 'cmake'; arguments = $buildArguments; exit_code = 0 }
$offlinePassed = $false
if ($RunOfflineTests) {
    $previousOffline = $env:RV_TEST_OFFLINE_ONLY
    $env:RV_TEST_OFFLINE_ONLY = '1'
    $offline = Join-Path $build 'offline-output'
    New-Item -ItemType Directory -Force -Path $offline | Out-Null
    Push-Location $offline
    try {
        $selfTestExe = Join-Path $build "bin/MINIMAL/$Configuration/CxwlDepthSelfTest.exe"
        $selfTestArgs = @('-Report', (Join-Path $offline 'self-test.json'))
        & $selfTestExe @selfTestArgs
        if ($LASTEXITCODE) { throw 'Offline self-test failed.' }
        $commands += @{ program = $selfTestExe; arguments = $selfTestArgs; exit_code = 0 }
        $selfTest = Get-Content -LiteralPath (Join-Path $offline 'self-test.json') -Raw | ConvertFrom-Json
        if (!$selfTest.passed -or $selfTest.sdk_version -ne $manifest.sdk_version -or $selfTest.abi_version -ne $manifest.abi_version) {
            throw 'Offline self-test report differs from sdk-manifest.json.'
        }
        $offlinePassed = $true
    } finally { Pop-Location; $env:RV_TEST_OFFLINE_ONLY = $previousOffline }
}
$report = [ordered]@{
    sdk_version = $manifest.sdk_version
    configuration = $Configuration; viewer_built = $WithViewer.IsPresent
    commands = $commands; offline_tests_requested = $RunOfflineTests.IsPresent
    offline_tests_passed = $offlinePassed; hardware_connected_for_validation = $false
}
[IO.File]::WriteAllText((Join-Path $build 'build-examples.json'), ($report | ConvertTo-Json -Depth 6), [Text.UTF8Encoding]::new($false))
Write-Output "Examples built: $build"
