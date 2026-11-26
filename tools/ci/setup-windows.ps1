param(
    [Parameter(Mandatory)][string]$ArchiveDirectory,
    [Parameter(Mandatory)][string]$Destination
)
$ErrorActionPreference = 'Stop'
$ArchiveDirectory = [IO.Path]::GetFullPath($ArchiveDirectory)
$Destination = [IO.Path]::GetFullPath($Destination)
New-Item -ItemType Directory -Path $ArchiveDirectory, $Destination -Force | Out-Null
$sevenZip = Join-Path $env:ProgramFiles '7-Zip/7z.exe'
if (!(Test-Path -LiteralPath $sevenZip -PathType Leaf)) {
    $sevenZip = (Get-Command 7z.exe -ErrorAction Stop).Source
}

function Get-VerifiedArchive([string]$Url, [string]$ExpectedHash) {
    $archive = Join-Path $ArchiveDirectory ([Uri]$Url).Segments[-1]
    if (!(Test-Path -LiteralPath $archive -PathType Leaf)) {
        $partial = "$archive.partial"
        $curlArguments = @('--fail', '--silent', '--show-error', '--location', '--retry', '3',
            '--connect-timeout', '15', '--max-time', '180', '--proto', '=https', '--proto-redir', '=https',
            '--output', $partial, $Url)
        & curl.exe @curlArguments
        if ($LASTEXITCODE) { throw "Download failed: $Url" }
        if ((Get-FileHash -LiteralPath $partial -Algorithm SHA256).Hash -ne $ExpectedHash) {
            throw "SHA-256 mismatch: $Url"
        }
        Move-Item -LiteralPath $partial -Destination $archive
    } elseif ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $ExpectedHash) {
        throw "SHA-256 mismatch in cached archive: $archive"
    }
    return $archive
}

function Expand-Dependency([string]$Archive, [string]$Target, [string[]]$Members = @()) {
    New-Item -ItemType Directory -Path $Target -Force | Out-Null
    & $sevenZip x -y -bso0 -bsp0 "-o$Target" $Archive @Members
    if ($LASTEXITCODE) { throw "Cannot extract $Archive" }
}

# Qt 6.11.1 MSVC 2022 x64 archive hashes come from the official Qt mirror metadata.
$feed = 'https://download.qt.io/online/qtsdkrepository/windows_x86/desktop/qt6_6111/qt6_6111_msvc2022_64'
$hashes = [ordered]@{
    qtbase = '7f97edc3937fec7383eb865e010ed5128155bf9c80a563abca450860f3e9bef5'
    qttools = '748f455851e0d81cd0e65232f6110840eda2bb980c4a4abf7e58ce97c2ff0f71'
    qtsvg = '1d75ccf1f1a8fc78612c2c78bc2448f8f68949096d38ff9afe21980e0cb3d9b5'
    qttranslations = '2da76e41de1ed46542b8648e5d92b18f916817fac1d76121a209d72a0123093c'
    qtimageformats = '8ba70600a3343fa0f1101fd304be32a58c3fe85fc6c6b316115e9d03a1be560a'
    qtmultimedia = 'e7255e7ed621c6e4fc5af9f5a1614b45260a960f03bcb4b1f37f8e910f6c8fd9'
}
$qt = Join-Path $Destination '6.11.1/msvc2022_64'
foreach ($part in $hashes.Keys) {
    $component = if ($part -in 'qtimageformats', 'qtmultimedia') { "addons.$part." } else { '' }
    $file = "6.11.1-0-202605090529$part-Windows-Windows_11_24H2-MSVC2022-Windows-Windows_11_24H2-X86_64.7z"
    $archive = Get-VerifiedArchive "$feed/qt.qt6.6111.$($component)win64_msvc2022_64/$file" $hashes[$part]
    if ($part -eq 'qtmultimedia') {
        $ffmpegFiles = @('bin/avcodec-61.dll', 'bin/avformat-61.dll', 'bin/avutil-59.dll',
            'bin/swscale-8.dll', 'bin/swresample-5.dll')
        Expand-Dependency $archive $qt -Members $ffmpegFiles
        foreach ($file in $ffmpegFiles) {
            if (!(Test-Path -LiteralPath (Join-Path $qt $file) -PathType Leaf)) {
                throw "FFmpeg archive did not provide $file"
            }
        }
    } else {
        Expand-Dependency $archive $qt
    }
}

$msbuildUrl = 'https://download.qt.io/official_releases/vsaddin/3.5.0/qt-vsaddin-msbuild-3.5.0.zip'
$msbuild = Get-VerifiedArchive $msbuildUrl 'e6398999d7e2bacf2cb93839ef24e7a1be851dfc7d6e3dc7dc3e57c8efdb4ed8'
Expand-Dependency $msbuild (Join-Path $Destination 'QtMsBuild')

$sourceUrl = 'https://download.qt.io/official_releases/qt/6.11/6.11.1/submodules/qtbase-everywhere-src-6.11.1.tar.xz'
$source = Get-VerifiedArchive $sourceUrl 'd9594a31228aa23ad6b531719a29b45f0f3989fe6c136d45767ea179f233c1ac'
$licenseFiles = @('qtbase-everywhere-src-6.11.1/LICENSES/GPL-3.0-only.txt',
    'qtbase-everywhere-src-6.11.1/LICENSES/LGPL-3.0-only.txt')
$sourceDirectory = Join-Path $Destination '6.11.1/Src'
Expand-Dependency $source $sourceDirectory
$sourceTar = Join-Path $sourceDirectory 'qtbase-everywhere-src-6.11.1.tar'
$qtBase = Join-Path $sourceDirectory 'qtbase'
New-Item -ItemType Directory -Path $qtBase -Force | Out-Null
& tar.exe -xf $sourceTar -C $qtBase --strip-components=1 @licenseFiles
if ($LASTEXITCODE) { throw 'Cannot extract Qt Base licenses.' }
Remove-Item -LiteralPath $sourceTar

$version = & (Join-Path $qt 'bin/qmake.exe') -query QT_VERSION
if ($LASTEXITCODE -or $version -ne '6.11.1') { throw 'Qt installation check failed.' }
& (Join-Path $qt 'bin/lrelease.exe') -version
if ($LASTEXITCODE) { throw 'Qt Linguist tools check failed.' }
