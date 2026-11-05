<##
.SYNOPSIS
只读查询本项目宿主 Renderer 的实际 Windows 令牌限制，不能以构建开关代替。
##>
param([string]$ExecutablePath)
$ErrorActionPreference = 'Stop'
if (-not $ExecutablePath) {
  $ExecutablePath = Join-Path (Split-Path -Parent $PSScriptRoot) 'build/host/run/alkaid_host.exe'
}
$resolvedExecutable = (Resolve-Path -LiteralPath $ExecutablePath).Path
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class AlkaidTokenProbe {
  [DllImport("kernel32.dll", SetLastError=true)] static extern IntPtr OpenProcess(uint access, bool inherit, uint pid);
  [DllImport("advapi32.dll", SetLastError=true)] static extern bool OpenProcessToken(IntPtr process, uint access, out IntPtr token);
  [DllImport("advapi32.dll", SetLastError=true)] static extern bool GetTokenInformation(IntPtr token, int kind, out int value, int size, out int returned);
  [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr handle);
  public static int Read(uint pid, int kind) {
    IntPtr process = OpenProcess(0x1000, false, pid);
    if (process == IntPtr.Zero) return -1;
    IntPtr token = IntPtr.Zero;
    try {
      if (!OpenProcessToken(process, 8, out token)) return -1;
      int value, size;
      return GetTokenInformation(token, kind, out value, 4, out size) ? value : -1;
    } finally { if (token != IntPtr.Zero) CloseHandle(token); CloseHandle(process); }
  }
}
'@
$renderers = @(Get-CimInstance Win32_Process -Filter "Name = 'alkaid_host.exe'" | Where-Object {
  $_.ExecutablePath -eq $resolvedExecutable -and $_.CommandLine -match '--type=renderer'
})
if (-not $renderers.Count) { throw '未找到本项目运行中的 Renderer，不能确认沙箱状态。' }
$records = @($renderers | ForEach-Object {
  [PSCustomObject]@{
    processId = $_.ProcessId
    appContainer = [AlkaidTokenProbe]::Read($_.ProcessId, 29)
    restricted = [AlkaidTokenProbe]::Read($_.ProcessId, 21)
    noSandboxArgument = $_.CommandLine -match '--no-sandbox'
  }
})
$records | ConvertTo-Json
foreach ($record in $records) {
  if ($record.noSandboxArgument -or ($record.appContainer -ne 1 -and $record.restricted -ne 1)) {
    throw '未能证实 Renderer 具有预期的 Windows 沙箱令牌限制。'
  }
}
