<## 只操作指定测试宿主及 build 目录中的文本夹具，验证真实系统选择器。 ##>
param(
  [Parameter(Mandatory=$true)][int]$HostProcessId,
  [Parameter(Mandatory=$true)][string]$Title,
  [string]$FilePath = '',
  [switch]$Cancel,
  [switch]$ConfirmOverwrite
)
$ErrorActionPreference = 'Stop'
$project = Split-Path -Parent $PSScriptRoot
$expected = Join-Path $project 'build/host/run/alkaid_host.exe'
if ((Get-Process -Id $HostProcessId).Path -ne $expected) { throw '测试宿主路径不匹配' }
if (-not $Cancel) {
  $selected = [IO.Path]::GetFullPath($FilePath)
  $allowed = [IO.Path]::GetFullPath((Join-Path $project 'build')) + [IO.Path]::DirectorySeparatorChar
  if (-not $selected.StartsWith($allowed,[StringComparison]::OrdinalIgnoreCase)) { throw '测试文件必须位于项目 build 目录内' }
}
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class AlkaidTextDialogInput {
 [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr SendMessage(IntPtr h,uint m,IntPtr w,string text);
 [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
}
'@
$condition = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::ProcessIdProperty,$HostProcessId)
function Find-Dialog([string]$Name) {
  $windows = [System.Windows.Automation.AutomationElement]::RootElement.FindAll([System.Windows.Automation.TreeScope]::Children,$condition)
  foreach ($window in $windows) {
    if ($window.Current.Name -eq $Name) { return $window }
    $child = $window.FindFirst([System.Windows.Automation.TreeScope]::Descendants,(New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::NameProperty,$Name)))
    if ($child) { return $child }
  }
  return $null
}
$dialog = $null
for ($attempt=0;$attempt -lt 60;$attempt++) {
  $dialog = Find-Dialog $Title
  if ($dialog) { break }
  Start-Sleep -Milliseconds 100
}
if (-not $dialog) { throw '未找到本次测试的文本文件对话框' }
if (-not $Cancel) {
  $fileEdit = $null
  for ($attempt=0;$attempt -lt 50;$attempt++) {
    $edits = $dialog.FindAll([System.Windows.Automation.TreeScope]::Descendants,(New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::ClassNameProperty,'Edit')))
    foreach ($edit in $edits) {
      if ($edit.Current.AutomationId -eq '1148' -or $edit.Current.Name -match '文件名|File name|文件夹|Folder') { $fileEdit = $edit; break }
    }
    if ($fileEdit) { break }
    Start-Sleep -Milliseconds 100
  }
  if (-not $fileEdit) { throw '未找到文件名输入框' }
  if (Test-Path -LiteralPath $selected -PathType Container) {
    $fileEdit.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern).SetValue($selected)
  } else {
    [AlkaidTextDialogInput]::SendMessage([IntPtr]$fileEdit.Current.NativeWindowHandle,0x000C,[IntPtr]::Zero,$selected) | Out-Null
  }
}
$buttons = $dialog.FindAll([System.Windows.Automation.TreeScope]::Descendants,(New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::ClassNameProperty,'Button')))
$target = $null
$buttonId = if ($Cancel) { '2' } else { '1' }
foreach ($button in $buttons) { if ($button.Current.AutomationId -eq $buttonId) { $target = $button; break } }
if (-not $target) { throw '未找到选择器确认或取消按钮' }
[AlkaidTextDialogInput]::PostMessage([IntPtr]$target.Current.NativeWindowHandle,0x00F5,[IntPtr]::Zero,[IntPtr]::Zero) | Out-Null
if ($ConfirmOverwrite) {
  $confirm = $null
  for ($attempt=0;$attempt -lt 50;$attempt++) {
    $confirm = Find-Dialog '确认另存为'
    if (-not $confirm) { $confirm = Find-Dialog 'Confirm Save As' }
    if ($confirm) { break }
    Start-Sleep -Milliseconds 100
  }
  if (-not $confirm) { throw '未出现测试文件覆盖确认' }
  $yes = $confirm.FindFirst([System.Windows.Automation.TreeScope]::Descendants,(New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::AutomationIdProperty,'CommandButton_6')))
  if (-not $yes) { $yes = $confirm.FindFirst([System.Windows.Automation.TreeScope]::Descendants,(New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::AutomationIdProperty,'6'))) }
  if (-not $yes) { throw '未找到覆盖确认按钮' }
  $invoke = $yes.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern)
  $invoke.Invoke()
}
Write-Output '已操作测试宿主的文本文件选择器'
