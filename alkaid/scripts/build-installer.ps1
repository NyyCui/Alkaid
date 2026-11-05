<# 从已构建的 Windows x64 Release 生成安装程序。 #>
param(
  [Parameter(Mandatory)][string]$IsccPath,
  [Parameter(Mandatory)][string]$CrtDirectory
)
$ErrorActionPreference = 'Stop'
$project = Split-Path -Parent $PSScriptRoot
Push-Location $project
try {
  $compiler = (Resolve-Path -LiteralPath $IsccPath).Path
  $crt = (Resolve-Path -LiteralPath $CrtDirectory).Path
  $run = (Resolve-Path -LiteralPath 'build/host/run').Path
  $output = Join-Path $project 'build/installer'
  New-Item -ItemType Directory -Path $output -Force | Out-Null
  foreach ($required in @('alkaid_host.dll', 'libcef.dll', 'web/index.html', 'developer/alkaid_package_tool.exe')) {
    if (!(Test-Path -LiteralPath (Join-Path $run $required) -PathType Leaf)) { throw "Release 缺少 $required" }
  }
  foreach ($required in @('msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll')) {
    if (!(Test-Path -LiteralPath (Join-Path $crt $required) -PathType Leaf)) { throw "VC x64 运行库缺少 $required" }
  }
  $icon = (Resolve-Path -LiteralPath '../desktop/assets/alkaid.ico').Path
  & $compiler "/DPayload=$run" "/DCrt=$crt" "/DBrandIcon=$icon" "/O$output" 'tools/installer/alkaid.iss'
  if ($LASTEXITCODE) { throw "安装包编译失败，退出码 $LASTEXITCODE" }
  $installer = Join-Path $output 'Alkaid-Setup-x64.exe'
  $hash = (Get-FileHash -LiteralPath $installer -Algorithm SHA256).Hash
  "$hash  Alkaid-Setup-x64.exe" | Set-Content -LiteralPath "$installer.sha256" -Encoding ascii
  Get-Item -LiteralPath $installer | Select-Object FullName, Length
} finally {
  Pop-Location
}
