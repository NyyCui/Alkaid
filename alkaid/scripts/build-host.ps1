<##
.SYNOPSIS
构建重构版宿主，依赖需先准备；不会隐式联网下载。
##>
param([string]$CMakePath = 'cmake', [string]$NodePath = 'node')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$nodeExecutable = (Get-Command $NodePath -ErrorAction Stop).Source
function Invoke-Checked {
  param([string]$Tool, [string[]]$Arguments)
  & $Tool @Arguments
  if ($LASTEXITCODE -ne 0) { throw "构建命令失败：$Tool，退出码 $LASTEXITCODE" }
}
Push-Location $projectRoot
try {
  Invoke-Checked $CMakePath @('-S', '.', '-B', 'build/host', '-G', 'Visual Studio 17 2022', '-A', 'x64',
    '-DALKAID_BUILD_LEGACY_HOST=OFF', '-DALKAID_BUILD_HOST=ON', "-DALKAID_NODE_EXECUTABLE=$nodeExecutable", '-DBUILD_TESTING=ON')
  Invoke-Checked $CMakePath @('--build', 'build/host', '--config', 'Release', '--parallel', '1', '--', '/nodeReuse:false')
  Write-Host "可运行程序：$(Join-Path $projectRoot 'build/host/run/alkaid_host.exe')"
} finally { Pop-Location }
