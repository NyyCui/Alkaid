<##
.SYNOPSIS
只对指定瑶光台测试进程的原生文件选择器填写测试包路径。
##>
param([Parameter(Mandatory=$true)][int]$HostProcessId, [Parameter(Mandatory=$true)][string]$PackagePath)
$ErrorActionPreference = 'Stop'
$expected = Join-Path (Split-Path -Parent $PSScriptRoot) 'build/host/run/alkaid_host.exe'
if ((Get-Process -Id $HostProcessId).Path -ne $expected) { throw '测试宿主路径不匹配' }
$selected = (Resolve-Path -LiteralPath $PackagePath).Path
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
$condition = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::ProcessIdProperty, $HostProcessId)
$dialog = $null
for ($attempt=0; $attempt -lt 50; $attempt++) {
  $windows = [System.Windows.Automation.AutomationElement]::RootElement.FindAll([System.Windows.Automation.TreeScope]::Children,$condition)
  foreach ($window in $windows) {
    if ($window.Current.Name -eq '选择 YPKG 应用包') { $dialog = $window; break }
    $child = $window.FindFirst([System.Windows.Automation.TreeScope]::Descendants,(New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::NameProperty,'选择 YPKG 应用包')))
    if ($child) { $dialog = $child; break }
  }
  if ($dialog) { break }
  Start-Sleep -Milliseconds 100
}
if (-not $dialog) { throw '未找到本次测试的文件选择器' }
$editCondition = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::ClassNameProperty,'Edit')
$fileEdit = $null
for ($attempt=0; $attempt -lt 50; $attempt++) {
  $edits = $dialog.FindAll([System.Windows.Automation.TreeScope]::Descendants,$editCondition)
  foreach ($edit in $edits) {
    if ($edit.Current.AutomationId -eq '1148' -or $edit.Current.Name -match '文件名|File name') { $fileEdit = $edit; break }
  }
  if ($fileEdit) { break }
  Start-Sleep -Milliseconds 100
}
if (-not $fileEdit) { throw '未定位文件名输入框' }
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class AlkaidFileDialogInput {
 [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr SendMessage(IntPtr h,uint m,IntPtr w,string text);
 [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
}
'@
$editHandle = [IntPtr]$fileEdit.Current.NativeWindowHandle
if ($editHandle -eq [IntPtr]::Zero) { throw '文件名输入框缺少原生句柄' }
[AlkaidFileDialogInput]::SendMessage($editHandle,0x000C,[IntPtr]::Zero,$selected) | Out-Null
$buttonCondition = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::ClassNameProperty,'Button')
$buttons = $dialog.FindAll([System.Windows.Automation.TreeScope]::Descendants,$buttonCondition)
$openButton = $null
foreach ($button in $buttons) {
  if ($button.Current.AutomationId -eq '1' -and $button.Current.Name -match '打开|Open') { $openButton = $button; break }
}
if (-not $openButton) { throw '未定位打开按钮' }
if (-not [AlkaidFileDialogInput]::PostMessage([IntPtr]$openButton.Current.NativeWindowHandle,0x00F5,[IntPtr]::Zero,[IntPtr]::Zero)) { throw '无法点击文件选择器打开按钮' }
Write-Output '已通过原生选择器选中测试包'
