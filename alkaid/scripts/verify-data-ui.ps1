<## 真实文件读写与宿主重启测试，使用独立 build 数据目录。 ##>
param([string]$NodePath='node',[switch]$ForceExitRecovery,[switch]$CleanupRetry,[switch]$ParallelApps)
$ErrorActionPreference='Stop'
$project = Split-Path -Parent $PSScriptRoot
Push-Location $project
try {
  $exe=(Resolve-Path 'build/host/run/alkaid_host.exe').Path
  if (Get-CimInstance Win32_Process -Filter "Name='alkaid_host.exe'" | Where-Object { $_.ExecutablePath -eq $exe }) { throw '已有宿主运行，不能占用其调试端口' }
  $prefix = if ($ParallelApps) { '并行 应用-' } else { 'run-' }
  $root=Join-Path $project ('build/data-ui-verification/'+$prefix+[DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds())
  New-Item -ItemType Directory -Path $root -Force | Out-Null
  $env:ALKAID_TEST_POWERSHELL=(Get-Command pwsh).Source
  if (@($ForceExitRecovery, $CleanupRetry, $ParallelApps).Where({ $_ }).Count -gt 1) { throw '请选择一种验收模式' }
  $phases = if ($ForceExitRecovery) { @('crash-seed','crash-check') }
    elseif ($CleanupRetry) { @('flow','restart','browser-blocked','browser-cleanup') }
    elseif ($ParallelApps) { @('parallel-seed','parallel-check') }
    else { @('flow','restart','browser-cleanup') }
  foreach ($phase in $phases) {
    $hostProcess=$null
    $lockedFile=$null
    try {
      if ($phase -eq 'browser-blocked') {
        $digest=[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes('com.example.notes'))).ToLowerInvariant()
        $profile=Join-Path $root ('data/profiles/a-'+$digest.Substring(0,32))
        $lockedFile=[IO.File]::Open((Join-Path $profile 'locked-test-file'),[IO.FileMode]::CreateNew,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
      }
      $dataDir=Join-Path $root 'data'
      $hostProcess=Start-Process -FilePath $exe -ArgumentList @('--alkaid-debug',('--alkaid-data-dir="'+$dataDir+'"')) -WorkingDirectory (Split-Path $exe) -WindowStyle Hidden -PassThru
      $processHandle=$hostProcess.Handle
      @{pid=$hostProcess.Id;root=$root} | ConvertTo-Json | Set-Content 'build/data-ui-verification/current.json' -Encoding utf8
      if ($phase -like 'crash-*' -or $phase -like 'parallel-*') { & $NodePath scripts/test-data-ui.mjs "--$phase" }
      elseif ($phase -eq 'browser-blocked') { & $NodePath scripts/test-data-ui.mjs --browser-blocked-check }
      elseif ($phase -eq 'flow') { & $NodePath scripts/test-data-ui.mjs }
      elseif ($phase -eq 'restart') { & $NodePath scripts/test-data-ui.mjs --restart-check }
      else { & $NodePath scripts/test-data-ui.mjs --browser-cleanup-check }
      $testExit=$LASTEXITCODE
      if ($phase -eq 'crash-seed' -and $testExit -eq 0) {
        if ($hostProcess.HasExited) { throw '宿主在注入强退前已退出' }
        $hostProcess.Kill()
        if (!$hostProcess.WaitForExit(15000) -or $hostProcess.ExitCode -eq 0) { throw '未确认宿主异常退出' }
        @{ forced = $true; exitCode = $hostProcess.ExitCode } | ConvertTo-Json |
          Set-Content (Join-Path $root 'evidence/forced-exit.json') -Encoding utf8
        Write-Output '已强制结束测试宿主'
        continue
      }
      if (-not $hostProcess.WaitForExit(15000)) { throw '测试宿主未退出' }
      if ($testExit -ne 0 -or $hostProcess.ExitCode -ne 0) { throw "验证失败：测试=$testExit 宿主=$($hostProcess.ExitCode)" }
      Write-Output "$phase 验证通过，宿主正常退出"
    } finally {
      if ($hostProcess -and !$hostProcess.HasExited) { $hostProcess.Kill() }
      if ($lockedFile) { $lockedFile.Dispose() }
    }
  }
  Write-Output "证据目录：$root"
} finally { Pop-Location }
