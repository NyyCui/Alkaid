<##
.SYNOPSIS
验证重构基础：契约同步、C++ 构建测试、SDK 类型与传输测试。
.DESCRIPTION
要求 MSVC 2022、CMake、Python、Node 24+。工具不在 PATH 时传入对应路径。
TypeScriptPath 指向本地 typescript/lib/tsc.js；默认使用 SDK 开发依赖。
##>
param(
  [string]$CMakePath = 'cmake',
  [string]$NodePath = 'node',
  [string]$PythonPath = 'python',
  [string]$TypeScriptPath = ''
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if (-not $TypeScriptPath) {
  $TypeScriptPath = Join-Path $projectRoot 'packages/sdk/node_modules/typescript/lib/tsc.js'
}
if (-not (Test-Path -LiteralPath $TypeScriptPath)) {
  throw '未找到 TypeScript，请在 packages/sdk 安装开发依赖，或传入 -TypeScriptPath。'
}

function Invoke-Checked {
  param([string]$Tool, [string[]]$Arguments)
  & $Tool @Arguments
  if ($LASTEXITCODE -ne 0) { throw "验证命令失败：$Tool，退出码 $LASTEXITCODE" }
}

Push-Location $projectRoot
try {
  Invoke-Checked $PythonPath @('scripts/generate-contracts.py', '--check')
  Invoke-Checked $CMakePath @('--preset', 'Core')
  Invoke-Checked $CMakePath @('--build', 'build/core', '--config', 'Release')
  $resolvedCMake = (Get-Command $CMakePath -ErrorAction Stop).Source
  $ctestPath = Join-Path (Split-Path -Parent $resolvedCMake) 'ctest.exe'
  Invoke-Checked $ctestPath @('--test-dir', 'build/core', '-C', 'Release', '--output-on-failure')
  Invoke-Checked $NodePath @($TypeScriptPath, '-p', 'packages/sdk/tsconfig.json')
  Invoke-Checked $NodePath @('--test', 'packages/sdk/test/client.test.ts', 'packages/sdk/test/streams.test.ts', 'tests/desktop/bridge.test.mjs')
  Write-Host '重构基础验证全部通过。此结果不包含 CEF 桌面运行验证。'
} finally {
  Pop-Location
}
