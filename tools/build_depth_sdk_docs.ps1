# Generate the offline API reference from the public header and SDK guides.
param([string]$OutputDirectory = '', [string]$Doxygen = '', [string]$PdfPython = 'python')
$ErrorActionPreference = 'Stop'
$project = Split-Path $PSScriptRoot -Parent
if (!$OutputDirectory) { $OutputDirectory = Join-Path $project 'x64/validation/depth-sdk-doxygen' }
$output = [IO.Path]::GetFullPath($OutputDirectory)
$product = Get-Content -LiteralPath (Join-Path $project 'sdk/product.json') -Raw | ConvertFrom-Json
$edition = 'Windows 设备服务预览 · 1.0.0-preview.1'
& (Join-Path $PSScriptRoot 'generate_sdk_version.ps1') -Check
if (!$Doxygen) {
    $local = Join-Path $project 'x64/tools/doxygen-1.9.7/doxygen.exe'
    if (Test-Path -LiteralPath $local) { $Doxygen = $local }
    else { $Doxygen = (Get-Command doxygen -ErrorAction Stop).Source }
}
$version = (& $Doxygen --version).Trim()
if ($LASTEXITCODE -or $version -notmatch '^1\.9\.7(?:\s|$)') { throw 'Use Doxygen 1.9.7 for reproducible reference styling.' }
New-Item -ItemType Directory -Force -Path $output | Out-Null
$previousRoot = $env:CXWL_DOC_ROOT
$previousOutput = $env:CXWL_DOC_OUTPUT
$previousVersion = $env:CXWL_DOC_VERSION
try {
    $env:CXWL_DOC_ROOT = $project.Replace('\', '/')
    $env:CXWL_DOC_VERSION = $product.sdk_version
    $env:CXWL_DOC_OUTPUT = $output.Replace('\', '/')
    & $Doxygen (Join-Path $PSScriptRoot 'depth_sdk_docs/Doxyfile')
    if ($LASTEXITCODE) {
        Get-Content -LiteralPath (Join-Path $output 'warnings.log') -ErrorAction SilentlyContinue
        throw 'Doxygen failed.'
    }
    # Doxygen leaves pages from renamed Markdown inputs in an existing output.
    # Prune only obsolete generated Markdown pages, using the current XML index.
    [xml]$index = Get-Content -LiteralPath (Join-Path $output 'xml/index.xml') -Raw
    $activePages = @($index.SelectNodes('//compound[@kind="page"]') | ForEach-Object { $_.GetAttribute('refid') })
    foreach ($page in Get-ChildItem -LiteralPath $output -Filter 'md_*.html' -File) {
        if ($page.BaseName -notin $activePages -and
            (Get-Content -LiteralPath $page.FullName -Raw) -match 'name="generator" content="Doxygen ') {
            Remove-Item -LiteralPath $page.FullName
            $oldXml = Join-Path $output ('xml/' + $page.BaseName + '.xml')
            if (Test-Path -LiteralPath $oldXml -PathType Leaf) { Remove-Item -LiteralPath $oldXml }
        }
    }
    $header = Get-Content -LiteralPath (Join-Path $project 'sdk/DepthSdk.h') -Raw
    $functions = [regex]::Matches($header, 'CXWL_DEPTH_API\s+\w+\s+CXWL_DEPTH_CALL\s+(CxwlDepth_\w+)\s*\(') |
        ForEach-Object { $_.Groups[1].Value }
    [xml]$xml = Get-Content -LiteralPath (Join-Path $output 'xml/_depth_sdk_8h.xml') -Raw
    $documented = @($xml.SelectNodes('//memberdef[@kind="function"]/name') | ForEach-Object { $_.InnerText })
    if (Compare-Object ($functions | Sort-Object) ($documented | Sort-Object)) {
        throw 'Doxygen and the C API must contain the same function set.'
    }
    & python (Join-Path $PSScriptRoot 'export_sdk_markdown.py') --xml (Join-Path $output 'xml/_depth_sdk_8h.xml') --output (Join-Path $output 'API_REFERENCE.md') --edition $edition
    if ($LASTEXITCODE) { throw 'Markdown export failed.' }
    $reference = [IO.File]::ReadAllText((Join-Path $output 'API_REFERENCE.md')).Replace('](schemas/', '](sdk/schemas/').Replace('](images/', '](sdk/images/').Replace('](measurements/', '](sdk/measurements/')
    [IO.File]::WriteAllText((Join-Path $project 'docs/DEPTH_SDK_API_REFERENCE.md'), $reference, [Text.UTF8Encoding]::new($false))
    @{ generator = "Doxygen $version"; sdk_version = $product.sdk_version; abi_version = $product.abi_version; functions = $functions.Count } |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $output 'doxygen-build.json') -Encoding utf8NoBOM
    foreach ($directory in 'schemas','images') {
        Copy-Item -LiteralPath (Join-Path $project "docs/sdk/$directory") -Destination $output -Recurse -Force
    }
    & python (Join-Path $PSScriptRoot 'depth_sdk_docs/finish_html.py') $output (Join-Path $project 'sdk/product.json')
    if ($LASTEXITCODE) { throw 'Offline HTML search and navigation generation failed.' }
    & $PdfPython (Join-Path $PSScriptRoot 'build_sdk_pdf.py') --markdown (Join-Path $output 'API_REFERENCE.md') --output (Join-Path $output 'API_REFERENCE.pdf') --edition $edition
    if ($LASTEXITCODE) { throw 'PDF generation failed. Pass -PdfPython with a Python runtime containing reportlab.' }
    & $PdfPython (Join-Path $PSScriptRoot 'build_sdk_pdf.py') --handout --markdown (Join-Path $project 'docs/POINT_CLOUD_RECORDING.md') --output (Join-Path $output '点云录制数据读取说明.pdf')
    if ($LASTEXITCODE) { throw 'Recording guide PDF generation failed.' }
    Write-Output (Join-Path $output 'index.html')
} finally {
    $env:CXWL_DOC_ROOT = $previousRoot
    $env:CXWL_DOC_OUTPUT = $previousOutput
    $env:CXWL_DOC_VERSION = $previousVersion
}
