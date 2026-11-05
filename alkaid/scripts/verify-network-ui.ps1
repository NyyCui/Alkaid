<# 使用开发签名和独立数据目录验证本机或公网请求台。 #>
param([switch]$Public)
Push-Location (Split-Path -Parent $PSScriptRoot)
try {
  $ErrorActionPreference='Stop'
  $mode=if($Public){'public'}else{'local'}
  $root=Join-Path (Get-Location) "build/network-ui-$mode-$PID"
  New-Item -ItemType Directory $root | Out-Null
  $file=Join-Path $root fixture.json
  $exe=(Resolve-Path build/host/run/alkaid_host.exe).Path
  if(Get-CimInstance Win32_Process -Filter "Name='alkaid_host.exe'" | Where-Object ExecutablePath -eq $exe){throw '已有宿主运行'}
  $fixture=$null
  try {
   $manifest=Get-Content samples/network/manifest.json | ConvertFrom-Json
   if(!$Public){
    $fixture=Start-Process node -ArgumentList @('scripts/network-fixture.mjs',$file) -WindowStyle Hidden -PassThru
    for($i=0;$i -lt 30 -and !(Test-Path $file);$i++){Start-Sleep -Milliseconds 250}
    $f=Get-Content $file | ConvertFrom-Json
    if(!$f.port){throw '测试端口缺失'}
    $manifest.networkEndpoints[0].baseUrl="http://127.0.0.1:$($f.port)/"
    $manifest.networkEndpoints[0].allowPrivateNetwork=$true
   }
   $manifest | ConvertTo-Json -Depth 8 | Set-Content "$root/manifest.json"
   node scripts/ypkg.mjs pack "$root/manifest.json" build/host/web/samples/network build/dev-keys/example.private.pem "$root/network.ypkg"
   if($LASTEXITCODE){throw '打包失败'}
   $installed=& build/host/infra/package/Release/alkaid_package_tool.exe install "$root/data/packages" "$root/network.ypkg" trust | ConvertFrom-Json
   if($LASTEXITCODE -or !$installed.apps[0].originKey){throw '安装失败'}
   $hostProcess=Start-Process $exe -ArgumentList @('--alkaid-debug',("--alkaid-data-dir="+(Join-Path $root "data"))) -WorkingDirectory (Split-Path $exe) -WindowStyle Hidden -PassThru
   node scripts/test-network-ui.mjs "alkaid://$($installed.apps[0].originKey)/" "$root/report.json" $mode
   if($LASTEXITCODE){throw '页面回归失败'}
   if(!$hostProcess.WaitForExit(15000) -or $hostProcess.ExitCode -ne 0){throw '宿主未正常退出'}
   Write-Output "$root/report.json"
  } finally {
   if($hostProcess -and !$hostProcess.HasExited){Stop-Process -Id $hostProcess.Id}
   if($fixture -and !$fixture.HasExited){Stop-Process -Id $fixture.Id -ErrorAction SilentlyContinue}
  }
} finally { Pop-Location }
