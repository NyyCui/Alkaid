<## 使用本次隔离数据目录验证文件流、正常窗口关闭和跨进程重启。 ##>
param([string]$NodePath='node')
$ErrorActionPreference='Stop'
$project=Split-Path -Parent $PSScriptRoot
Push-Location $project
try {
  $exe=(Resolve-Path 'build/host/run/alkaid_host.exe').Path
  if (Get-CimInstance Win32_Process -Filter "Name='alkaid_host.exe'" | Where-Object { $_.ExecutablePath -eq $exe }) { throw '已有宿主运行，不能占用其调试端口' }
  $fixtures=Get-Content 'build/stream-ui-verification/fixtures.json' -Raw | ConvertFrom-Json
  $env:ALKAID_TEST_POWERSHELL=(Get-Command pwsh).Source
  $exits=@()
  foreach ($phase in @('flow','restart')) {
    $hostProcess=Start-Process -FilePath $exe -ArgumentList @('--alkaid-debug',('--alkaid-data-dir='+(Join-Path $fixtures.root 'data'))) -WorkingDirectory (Split-Path $exe) -WindowStyle Hidden -PassThru
    $processHandle=$hostProcess.Handle
    @{pid=$hostProcess.Id;root=$fixtures.root} | ConvertTo-Json | Set-Content 'build/stream-ui-verification/current.json' -Encoding utf8
    if ($phase -eq 'flow') { & $NodePath scripts/test-stream-ui.mjs }
    else { & $NodePath scripts/test-stream-ui.mjs --restart-check }
    $testExit=$LASTEXITCODE
    if (-not $hostProcess.WaitForExit(15000)) { throw '测试宿主未退出' }
    $exits+=@{phase=$phase;testExit=$testExit;hostExit=$hostProcess.ExitCode}
    if ($testExit -ne 0 -or $hostProcess.ExitCode -ne 0) { throw "验证失败：测试=$testExit 宿主=$($hostProcess.ExitCode)" }
    Write-Output "$phase 验证通过，宿主正常退出"
  }
  $exits | ConvertTo-Json | Set-Content (Join-Path $fixtures.root 'exit.json') -Encoding utf8
  Write-Output "证据目录：$($fixtures.root)"
} finally { Pop-Location }
