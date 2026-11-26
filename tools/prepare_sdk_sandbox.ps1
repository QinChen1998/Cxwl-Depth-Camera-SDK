param(
 [Parameter(Mandatory)][string]$Package,
 [Parameter(Mandatory)][string]$ValidationDirectory,
 [Parameter(Mandatory)][string]$IntegrationDirectory,
 [Parameter(Mandatory)][string]$ConanConsumer,
 [Parameter(Mandatory)][string]$VcpkgConsumer
)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$packageRoot=(Resolve-Path -LiteralPath $Package).Path
$work=(Resolve-Path -LiteralPath $ValidationDirectory).Path
$allowed=Join-Path $root 'x64\validation'
if(-not $work.StartsWith($allowed+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Use a validation directory inside this workspace.'}
$inputFolder=Join-Path $work 'sandbox-input'
$outputFolder=Join-Path $work 'sandbox-results'
if((Test-Path -LiteralPath $inputFolder) -or (Test-Path -LiteralPath $outputFolder)){throw 'Sandbox evidence already exists; choose a new validation directory.'}
New-Item -ItemType Directory -Path $inputFolder,$outputFolder | Out-Null
Copy-Item -LiteralPath $packageRoot -Destination (Join-Path $inputFolder 'package') -Recurse
$consumerRoot=Join-Path $inputFolder 'consumers'
New-Item -ItemType Directory $consumerRoot | Out-Null
$cases=[ordered]@{
 minimal=(Join-Path $IntegrationDirectory 'direct-minimal/Release')
 standard=(Join-Path $IntegrationDirectory 'direct-standard/Release')
 full=(Join-Path $IntegrationDirectory 'direct-full/Release')
 relocated=(Join-Path $IntegrationDirectory 'app-install/bin')
 conan=$ConanConsumer
 vcpkg=$VcpkgConsumer
}
$expected=(Get-FileHash -LiteralPath (Join-Path $packageRoot 'SDK/bin/CxwlDepthSdk.dll')).Hash
foreach($item in $cases.GetEnumerator()){
 $source=(Resolve-Path -LiteralPath $item.Value).Path
 if((Get-FileHash -LiteralPath (Join-Path $source 'CxwlDepthSdk.dll')).Hash -ne $expected){throw "Consumer uses a different SDK: $($item.Key)"}
 foreach($name in 'consumer.exe','c_consumer.exe'){if(-not (Test-Path -LiteralPath (Join-Path $source $name))){throw "Missing $name"}}
 Copy-Item -LiteralPath $source -Destination (Join-Path $consumerRoot $item.Key) -Recurse
}
[IO.File]::WriteAllText((Join-Path $inputFolder 'test-sandbox.ps1'),
 [IO.File]::ReadAllText((Join-Path $root 'tools/sdk_package/test-sandbox.ps1')),[Text.UTF8Encoding]::new($true))
$escapedInput=[Security.SecurityElement]::Escape($inputFolder)
$escapedOutput=[Security.SecurityElement]::Escape($outputFolder)
@"
<Configuration>
 <MemoryInMB>8192</MemoryInMB>
 <vGPU>Disable</vGPU>
 <Networking>Disable</Networking>
 <ClipboardRedirection>Disable</ClipboardRedirection>
 <MappedFolders>
  <MappedFolder><HostFolder>$escapedInput</HostFolder><SandboxFolder>C:\CxwlInput</SandboxFolder><ReadOnly>true</ReadOnly></MappedFolder>
  <MappedFolder><HostFolder>$escapedOutput</HostFolder><SandboxFolder>C:\CxwlResults</SandboxFolder><ReadOnly>false</ReadOnly></MappedFolder>
 </MappedFolders>
 <LogonCommand><Command>powershell.exe -NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass -File C:\CxwlInput\test-sandbox.ps1</Command></LogonCommand>
</Configuration>
"@ | Set-Content -LiteralPath (Join-Path $work 'CxwlSdkValidation.wsb') -Encoding utf8
Write-Output (Join-Path $work 'CxwlSdkValidation.wsb')

