param([string]$ApplicationDirectory='', [string]$OutputDirectory='')
$ErrorActionPreference='Stop'
$ProgressPreference='SilentlyContinue'

if(-not $OutputDirectory){$OutputDirectory=[Environment]::GetFolderPath('Desktop')}
if(-not $OutputDirectory){$OutputDirectory=$PSScriptRoot}
[void][IO.Directory]::CreateDirectory($OutputDirectory)
$report=[ordered]@{schema='cxwl-depth-sdk-usb-host-diagnostics-v1';time_utc=[DateTime]::UtcNow.ToString('o');read_only=$true}
$os=Get-CimInstance Win32_OperatingSystem -ErrorAction SilentlyContinue
$report.os=[ordered]@{caption=$os.Caption;version=$os.Version;build=$os.BuildNumber;architecture=$os.OSArchitecture}

$report.network=@(Get-NetAdapter -Physical -ErrorAction SilentlyContinue | Where-Object InterfaceDescription -match 'RNDIS|Remote NDIS' | Select-Object Name,ifIndex,InterfaceDescription,Status,LinkSpeed)
try {
    $report.usb_devices=@(Get-CimInstance Win32_PnPEntity | Where-Object {
        $_.PNPDeviceID -like 'USB\VID_2207*'
    } | Select-Object Name,PNPClass,Status,ConfigManagerErrorCode,Service,PNPDeviceID,HardwareID,CompatibleID)
    $report.usb_drivers=@(Get-CimInstance Win32_PnPSignedDriver | Where-Object {
        $_.DeviceID -like 'USB\VID_2207*'
    } | Select-Object DeviceName,DeviceID,DriverProviderName,DriverVersion,InfName,IsSigned)
} catch {$report.usb_query_error=$_.Exception.Message}

$path=Join-Path $OutputDirectory ('cxwl-depth-camera-usb-check-'+[DateTime]::Now.ToString('yyyyMMdd-HHmmss')+'.txt')
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $path -Encoding UTF8
Write-Host ('Rockchip USB entries: '+@($report.usb_devices).Count)
foreach($device in $report.usb_devices){Write-Host ($device.Name+' | status='+$device.Status+' | problem='+$device.ConfigManagerErrorCode+' | service='+$device.Service)}
Write-Host ('Report saved: '+$path)
