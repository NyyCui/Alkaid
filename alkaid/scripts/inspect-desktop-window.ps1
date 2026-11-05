<# 只检查指定的项目测试进程，验证窗口控制、图标和托盘回调。 #>
param([Parameter(Mandatory=$true)][int]$HostProcessId, [switch]$RestoreFromTray)
$ErrorActionPreference = 'Stop'
$expected = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../build/host/run/alkaid_host.exe'))
if ((Get-Process -Id $HostProcessId).Path -ne $expected) { throw '测试宿主路径不匹配' }
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class AlkaidWindowProbe {
 public delegate bool Callback(IntPtr window, IntPtr context);
 [StructLayout(LayoutKind.Sequential)] public struct Rect { public int left, top, right, bottom; }
 [StructLayout(LayoutKind.Sequential)] public struct Point { public int x, y; }
 [DllImport("user32.dll")] public static extern bool EnumWindows(Callback callback, IntPtr context);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr window, out uint pid);
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr window);
 [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr window);
 [DllImport("user32.dll")] public static extern bool IsZoomed(IntPtr window);
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr window, out Rect rect);
 [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr window, ref Point point);
 [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr window, uint message, IntPtr w, IntPtr l);
 [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string name, string title);
 public static IntPtr Find(uint process) {
   IntPtr result=IntPtr.Zero;
   EnumWindows((window,ctx)=>{ uint pid; GetWindowThreadProcessId(window,out pid);
     if(pid==process && IsWindowVisible(window)){result=window;return false;} return true;
   }, IntPtr.Zero); return result;
 }
}
'@
$tray = [AlkaidWindowProbe]::FindWindow('AlkaidTrayReceiver','')
[uint32]$trayProcess = 0
if ($tray -ne [IntPtr]::Zero) { [AlkaidWindowProbe]::GetWindowThreadProcessId($tray, [ref]$trayProcess) | Out-Null }
if ($RestoreFromTray) {
  if ($trayProcess -ne $HostProcessId) { throw '托盘接收窗口不属于测试宿主' }
  [AlkaidWindowProbe]::SendMessage($tray, 0x8029, [IntPtr]1, [IntPtr]0x0203) | Out-Null
}
$window = [AlkaidWindowProbe]::Find($HostProcessId)
if ($window -eq [IntPtr]::Zero) { throw '未找到测试桌面窗口' }
$rect = New-Object AlkaidWindowProbe+Rect
$point = New-Object AlkaidWindowProbe+Point
[AlkaidWindowProbe]::GetWindowRect($window,[ref]$rect) | Out-Null
[AlkaidWindowProbe]::ClientToScreen($window,[ref]$point) | Out-Null
$hitPosition = (($rect.top + 25) -shl 16) -bor (($rect.left + 550) -band 0xffff)
[ordered]@{
  minimized = [AlkaidWindowProbe]::IsIconic($window)
  maximized = [AlkaidWindowProbe]::IsZoomed($window)
  clientTopInset = $point.y - $rect.top
  smallIcon = [AlkaidWindowProbe]::SendMessage($window,0x007f,[IntPtr]0,[IntPtr]0).ToInt64()
  bigIcon = [AlkaidWindowProbe]::SendMessage($window,0x007f,[IntPtr]1,[IntPtr]0).ToInt64()
  titleHit = [AlkaidWindowProbe]::SendMessage($window,0x0084,[IntPtr]0,[IntPtr]$hitPosition).ToInt64()
  trayAttached = $trayProcess -eq $HostProcessId
} | ConvertTo-Json -Compress
