<# 只读取或移动指定测试宿主的原生窗口。 #>
param([Parameter(Mandatory)][int]$HostProcessId, [switch]$Move, [switch]$Maximize)
$ErrorActionPreference = 'Stop'
$expected = Join-Path (Split-Path -Parent $PSScriptRoot) 'build/host/run/alkaid_host.exe'
if ((Get-Process -Id $HostProcessId).Path -ne $expected) { throw '测试宿主路径不匹配' }
Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class AlkaidWindowProbe {
    [StructLayout(LayoutKind.Sequential)]
    public struct Rect { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)]
    public struct Monitor { public int Size; public Rect Bounds, Work; public int Flags; }
    private delegate bool EnumCallback(IntPtr h, IntPtr unused);
    [DllImport("user32.dll")] private static extern bool EnumWindows(EnumCallback callback, IntPtr unused);
    [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] private static extern int GetWindowText(IntPtr h, StringBuilder text, int size);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out Rect bounds);
    [DllImport("user32.dll")] public static extern IntPtr MonitorFromWindow(IntPtr h, uint flags);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern bool GetMonitorInfo(IntPtr h, ref Monitor info);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr order, int x, int y, int w, int height, uint flags);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int command);
    [DllImport("user32.dll")] public static extern bool IsZoomed(IntPtr h);
    [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
    public static IntPtr Find(uint pid, string title) {
        IntPtr match = IntPtr.Zero;
        EnumWindows((h, unused) => {
            uint owner;
            GetWindowThreadProcessId(h, out owner);
            if (owner != pid) return true;
            var buffer = new StringBuilder(256);
            GetWindowText(h, buffer, buffer.Capacity);
            if (buffer.ToString() != title) return true;
            match = h;
            return false;
        }, IntPtr.Zero);
        return match;
    }
}
'@
[void][AlkaidWindowProbe]::SetThreadDpiAwarenessContext([IntPtr](-4))
$window = [AlkaidWindowProbe]::Find($HostProcessId, '能力查看器')
if ($window -eq [IntPtr]::Zero) { throw '未找到测试应用窗口' }
$monitor = [AlkaidWindowProbe+Monitor]::new()
$monitor.Size = [Runtime.InteropServices.Marshal]::SizeOf($monitor)
if (![AlkaidWindowProbe]::GetMonitorInfo([AlkaidWindowProbe]::MonitorFromWindow($window, 2), [ref]$monitor)) {
  throw '无法读取显示器工作区'
}
$rect = [AlkaidWindowProbe+Rect]::new()
if (![AlkaidWindowProbe]::GetWindowRect($window, [ref]$rect)) { throw '无法读取窗口坐标' }
if ($Move) {
  $width = [Math]::Max(1, $rect.Right - $rect.Left - 110)
  $height = [Math]::Max(1, $rect.Bottom - $rect.Top - 70)
  if (![AlkaidWindowProbe]::SetWindowPos($window, [IntPtr]::Zero, $monitor.Work.Left + 47, $monitor.Work.Top + 61, $width, $height, 0x14)) {
    throw '无法移动测试窗口'
  }
}
if ($Maximize) { [void][AlkaidWindowProbe]::ShowWindow($window, 3) }
if (![AlkaidWindowProbe]::GetWindowRect($window, [ref]$rect)) { throw '无法读取窗口坐标' }
@{
  x = $rect.Left; y = $rect.Top; width = $rect.Right - $rect.Left; height = $rect.Bottom - $rect.Top
  maximized = [AlkaidWindowProbe]::IsZoomed($window)
  work = @{ left = $monitor.Work.Left; top = $monitor.Work.Top; right = $monitor.Work.Right; bottom = $monitor.Work.Bottom }
} | ConvertTo-Json -Compress
