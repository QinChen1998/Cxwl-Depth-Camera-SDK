[CmdletBinding(SupportsShouldProcess)]
param([Parameter(Mandatory)][ValidatePattern('^[0-9a-fA-F]{16}$')][string]$Serial)

$ErrorActionPreference = 'Stop'
$camera = '172.32.0.93'
$parent = "USB\VID_2207&PID_0019\$Serial"

function Get-CameraInterfaceIndex {
    $functions = @(Get-PnpDevice -PresentOnly -Class Net | Where-Object {
        $_.InstanceId -like 'USB\VID_2207&PID_0019&MI_*' -and $_.Status -eq 'OK' -and
        (Get-PnpDeviceProperty -InstanceId $_.InstanceId -KeyName 'DEVPKEY_Device_Parent' -WhatIf:$false).Data -eq $parent
    })
    if ($functions.Count -ne 1) {
        throw "Expected one enabled USB network adapter for camera $Serial. Check its data cable and RNDIS driver."
    }
    $adapters = @(Get-CimInstance Win32_NetworkAdapter | Where-Object {
        $_.PNPDeviceID -eq $functions[0].InstanceId -and $_.InterfaceIndex -gt 0
    })
    if ($adapters.Count -ne 1) { throw 'Cannot identify the camera network interface.' }
    return $adapters[0].InterfaceIndex
}

$locked = $false
$mutex = [Threading.Mutex]::new($false, 'Global\CxwlDepthCameraUsbIpv4')
try {
    try { $locked = $mutex.WaitOne([TimeSpan]::FromSeconds(15)) }
    catch [Threading.AbandonedMutexException] { $locked = $true }
    if (!$locked) { throw 'Another camera network configuration is in progress.' }

    $index = Get-CameraInterfaceIndex
    $addresses = @(Get-NetIPAddress -AddressFamily IPv4)
    $existing = $addresses | Where-Object {
        $_.InterfaceIndex -eq $index -and $_.IPAddress -like '172.32.*' -and $_.IPAddress -ne $camera -and
        $_.PrefixLength -eq 16 -and $_.AddressState -eq 'Preferred'
    } | Select-Object -First 1
    if ($existing) {
        "Camera $Serial on interface $index already has host address $($existing.IPAddress)/16. Camera address $camera."
        return
    }
    if ($addresses | Where-Object { $_.InterfaceIndex -eq $index -and $_.IPAddress -eq $camera }) {
        throw "The host interface is using $camera. Release that address on the camera's USB adapter first."
    }
    $address = 100..199 | ForEach-Object { "172.32.0.$_" } | Where-Object {
        $_ -notin $addresses.IPAddress
    } | Select-Object -First 1
    if (!$address) { throw 'No unassigned host address in 172.32.0.100 through 172.32.0.199.' }
    if ($index -ne (Get-CameraInterfaceIndex)) { throw 'The USB attachment changed.' }
    if ($PSCmdlet.ShouldProcess("camera $Serial, interface $index", "Assign static host IPv4 $address/16")) {
        $user = [Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
        if (!$user.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
            throw 'Run the USB IPv4 configuration from an administrator PowerShell.'
        }
        New-NetIPAddress -InterfaceIndex $index -IPAddress $address -PrefixLength 16 | Out-Null
        "Set host address $address/16 on interface $index for camera $Serial. Camera address $camera."
    }
} finally {
    if ($locked) { $mutex.ReleaseMutex() }
    $mutex.Dispose()
}
