<## 向本次测试进程中唯一匹配的原生窗口发送正常关闭请求。 ##>
param([Parameter(Mandatory=$true)][int]$HostProcessId,[Parameter(Mandatory=$true)][string]$Title)
$ErrorActionPreference='Stop'
$expected=Join-Path (Split-Path -Parent $PSScriptRoot) 'build/host/run/alkaid_host.exe'
if ((Get-Process -Id $HostProcessId).Path -ne $expected) { throw '测试宿主路径不匹配' }
Add-Type @'
using System;
using System.Text;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public static class AlkaidCloseTest {
 public delegate bool EnumProc(IntPtr h,IntPtr data);
 [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc callback,IntPtr data);
 [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h,out uint pid);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int GetWindowText(IntPtr h,StringBuilder text,int max);
 [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 public static Dictionary<IntPtr,string> Windows(uint target) {
   var result=new Dictionary<IntPtr,string>();
   EnumWindows((h,data)=>{uint pid;GetWindowThreadProcessId(h,out pid);if(pid==target){var title=new StringBuilder(1024);GetWindowText(h,title,1024);result[h]=title.ToString();}return true;},IntPtr.Zero);
   return result;
 }
}
'@
$windows=[AlkaidCloseTest]::Windows($HostProcessId)
$matches=@($windows.GetEnumerator() | Where-Object { $_.Value -eq $Title })
if ($matches.Count -ne 1) { throw ('未唯一定位本次测试窗口，当前标题：'+($windows.Values -join '、')) }
if (-not [AlkaidCloseTest]::PostMessage($matches[0].Key,0x0010,[IntPtr]::Zero,[IntPtr]::Zero)) { throw '发送关闭请求失败' }
