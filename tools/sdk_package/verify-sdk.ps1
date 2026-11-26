param([string]$Root = (Split-Path $PSScriptRoot -Parent), [switch]$Strict, [switch]$Json)
$ErrorActionPreference = 'Stop'
try {
    $rootPath = (Resolve-Path -LiteralPath $Root).Path
    $manifestPath = Join-Path $rootPath 'sdk-manifest.json'
    $manifest = Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
    if (!$manifest.sha256 -or !$manifest.sdk_version) { throw 'Invalid SDK manifest.' }
    $failures = [Collections.Generic.List[string]]::new()
    $count = 0
    $known = @{}
    foreach ($entry in $manifest.sha256.PSObject.Properties) {
        $relative = $entry.Name
        if ([IO.Path]::IsPathRooted($relative) -or $relative.Contains(':') -or $relative.Contains('\') -or
            ($relative -split '/') -contains '..' -or $entry.Value -notmatch '^[0-9a-f]{64}$') {
            throw "Invalid manifest entry: $relative"
        }
        $path = [IO.Path]::GetFullPath((Join-Path $rootPath $relative))
        if (!$path.StartsWith($rootPath + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Path escapes SDK root: $relative"
        }
        $known[$relative] = $true
        if (!(Test-Path -LiteralPath $path -PathType Leaf)) { $failures.Add("MISSING $relative") }
        elseif ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -cne $entry.Value) {
            $failures.Add("CHANGED $relative")
        }
        $count++
    }
    if (!$count) { throw 'Empty SDK manifest.' }
    if ($Strict) {
        foreach ($file in Get-ChildItem -LiteralPath $rootPath -Recurse -File -Force) {
            $relative = $file.FullName.Substring($rootPath.Length + 1).Replace('\','/')
            if ($relative -ne 'sdk-manifest.json' -and !$known.ContainsKey($relative)) { $failures.Add("EXTRA $relative") }
        }
    }
    $code = if ($failures.Count) { 1 } else { 0 }
    if ($Json) {
        [ordered]@{ exit_code = $code; sdk_version = $manifest.sdk_version; checked = $count
            strict = $Strict.IsPresent; failures = @($failures.ToArray()) } | ConvertTo-Json
    } elseif ($code) { $failures | Write-Output }
    else { Write-Output "PASS SDK $($manifest.sdk_version): $count files match SHA256." }
    exit $code
} catch {
    if ($Json) { [ordered]@{ exit_code = 2; error = $_.Exception.Message } | ConvertTo-Json }
    else { Write-Error $_ -ErrorAction Continue }
    exit 2
}
