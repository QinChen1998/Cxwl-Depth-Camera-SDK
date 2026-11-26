param([string]$InputRoot='C:\CxwlInput',[string]$OutputRoot='C:\CxwlResults')
$ErrorActionPreference='Stop'
$checks=[ordered]@{}
$env:RV_TEST_OFFLINE_ONLY='1'
$env:PATH="$env:SystemRoot\System32;$env:SystemRoot"
$progressPath=Join-Path $env:TEMP 'cxwl-sdk-sandbox-progress.txt'
# Keep active logs on the guest disk. Host reads of a mapped file can deny
# a guest writer through the Sandbox shared-folder transport.
function Progress([string]$Text){([DateTime]::UtcNow.ToString('o')+' '+$Text) | Add-Content -LiteralPath $progressPath -Encoding UTF8}
function Check([string]$Name,[bool]$Passed) {
    $checks[$Name]=$Passed
    Progress "$Name=$Passed"
    if(-not $Passed){throw "Failed: $Name"}
}
function Run([string]$Name,[string]$File,[string]$Arguments='',[int]$Expected=0,[int]$Timeout=60,[string]$Directory='C:\CxwlTest') {
    Progress "Running $Name"
    $info=New-Object Diagnostics.ProcessStartInfo
    $info.FileName=$File;$info.Arguments=$Arguments;$info.WorkingDirectory=$Directory
    $info.UseShellExecute=$false;$info.CreateNoWindow=$true;$info.WindowStyle='Hidden'
    $info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
    $task=New-Object Diagnostics.Process
    $task.StartInfo=$info
    if(-not $task.Start()){throw "Cannot start $Name"}
    $stdout=$task.StandardOutput.ReadToEndAsync()
    $stderr=$task.StandardError.ReadToEndAsync()
    try {
      if(-not $task.WaitForExit($Timeout*1000)){$task.Kill();throw "Timed out: $Name"}
      # Refresh after redirected pipes have closed.
      $task.WaitForExit()
      $stdout.GetAwaiter().GetResult() | Set-Content -LiteralPath "$OutputRoot\$Name.stdout.txt" -Encoding UTF8
      $stderr.GetAwaiter().GetResult() | Set-Content -LiteralPath "$OutputRoot\$Name.stderr.txt" -Encoding UTF8
      Progress "$Name exit=$($task.ExitCode), expected=$Expected"
      Check $Name ($task.ExitCode -eq $Expected)
    } finally {$task.Dispose()}
}
try {
    New-Item -ItemType Directory -Force C:\CxwlTest | Out-Null
    Check 'no_qt_development_environment' (-not (Get-Command qmake.exe -ErrorAction SilentlyContinue))
    Check 'no_visual_studio_environment' (-not (Get-Command cl.exe -ErrorAction SilentlyContinue))
    Copy-Item -LiteralPath "$InputRoot\package" -Destination C:\CxwlTest\package -Recurse
    $package='C:\CxwlTest\package'
    Run 'manifest_strict' "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe" "-NoProfile -ExecutionPolicy Bypass -File $package\Script\verify-sdk.ps1 -Strict" 0 180
    Run 'launcher_selftest' "$package\Example\bin\CxwlDepthSelfTest.exe"
    Run 'launcher_bad_arguments' "$package\Example\bin\CxwlDepthQuickStart.exe" '"invalid mode with spaces"' 2
    $cloud='C:\CxwlTest\working directory'
    New-Item -ItemType Directory $cloud | Out-Null
    Run 'launcher_pointcloud' "$package\Example\bin\CxwlDepthPointCloud.exe" '--help' 0 60 $cloud
    Copy-Item -LiteralPath "$InputRoot\consumers" -Destination C:\CxwlTest\consumers -Recurse
    $fixture='C:\CxwlTest\fixture.json'
    '{"schema":1,"kind":"cxwl-depth-camera-devices","active":"","devices":[]}' | Set-Content -LiteralPath $fixture -Encoding ASCII
    foreach($profile in 'minimal','standard','full','relocated','conan','vcpkg') {
        $bin="C:\CxwlTest\consumers\$profile"
        Run "$profile-c" "$bin\c_consumer.exe"
        Run "$profile-cpp" "$bin\consumer.exe" $fixture
    }
    $minimal='C:\CxwlTest\consumers\minimal'
    Check 'minimal_excludes_capture_and_widgets' (-not (Test-Path "$minimal\avcodec-61.dll") -and -not (Test-Path "$minimal\Qt6Widgets.dll") -and -not (Test-Path "$minimal\platform-tools"))
    Check 'standard_excludes_widgets' (-not (Test-Path 'C:\CxwlTest\consumers\standard\Qt6Widgets.dll'))
    Check 'full_has_platform_plugin' (Test-Path 'C:\CxwlTest\consumers\full\platforms\qwindows.dll')
    $launcher=Start-Process -FilePath "$package\Example\bin\CxwlDepthViewer.exe" -WorkingDirectory C:\CxwlTest -WindowStyle Hidden -PassThru
    $launcherHandle=$launcher.Handle
    try {
        $deadline=[DateTime]::UtcNow.AddSeconds(45);$viewer=$null
        do {
            Start-Sleep -Milliseconds 500
            $viewer=Get-Process -Name CxwlDepthViewer -ErrorAction SilentlyContinue | Where-Object {$_.Id -ne $launcher.Id -and $_.Path -eq "$package\SDK\bin\CxwlDepthViewer.exe"} | Select-Object -First 1
            if($viewer){$viewer.Refresh()}
        } while(( -not $viewer -or $viewer.MainWindowHandle -eq 0) -and [DateTime]::UtcNow -lt $deadline)
        Check 'viewer_window_created' ($null -ne $viewer -and $viewer.MainWindowHandle -ne 0)
        $viewerHandle=$viewer.Handle
        Start-Sleep -Seconds 3
        Check 'viewer_responding' $viewer.Responding
        Check 'viewer_close_requested' $viewer.CloseMainWindow()
        Check 'viewer_exit_zero' ($viewer.WaitForExit(30000) -and $viewer.ExitCode -eq 0)
        Check 'viewer_launcher_exit_zero' ($launcher.WaitForExit(10000) -and $launcher.ExitCode -eq 0)
    } finally {
        if($viewer -and -not $viewer.HasExited){$viewer.Kill()}
        if(-not $launcher.HasExited){$launcher.Kill()}
    }
    # The original package must remain unmodified by executing examples.
    Run 'manifest_after_examples' "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe" "-NoProfile -ExecutionPolicy Bypass -File $package\Script\verify-sdk.ps1 -Strict" 0 180
    $checks.passed=$true
} catch {
    $checks.passed=$false
    $checks.error=$_.Exception.ToString()
} finally {
    $checks.environment='Windows Sandbox; networking and vGPU disabled; no Qt/VS development environment'
    $checks.hardware_tested=$false
    $checks.windows_version=[Environment]::OSVersion.Version.ToString()
    Progress 'Finished'
    Copy-Item -LiteralPath $progressPath -Destination "$OutputRoot\progress.txt"
    $checks | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath "$OutputRoot\sandbox-result.pending" -Encoding UTF8
    Move-Item -LiteralPath "$OutputRoot\sandbox-result.pending" -Destination "$OutputRoot\sandbox-result.json"
}

