<# 使用可视化界面编辑 YPKG v3 清单，调用同一打包器和原生验签工具。 #>
[CmdletBinding(DefaultParameterSetName='Window')]
param(
  [Parameter(Mandatory, ParameterSetName='Batch')][string]$ManifestPath,
  [Parameter(Mandatory, ParameterSetName='Batch')][string]$AssetsPath,
  [Parameter(Mandatory, ParameterSetName='Batch')][string]$SigningKeyPath,
  [Parameter(Mandatory, ParameterSetName='Batch')][string]$OutputPath,
  [string]$NodePath = 'node',
  [string]$VerifierPath,
  [string]$ErrorPath,
  [switch]$LocalPublisher
)
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
$project = Split-Path -Parent $PSScriptRoot
$verifier = if ($VerifierPath) { $VerifierPath } else { Join-Path $project 'build/host/infra/package/Release/alkaid_package_tool.exe' }

function New-VerifiedPackage([string]$Manifest, [string]$Assets, [string]$Key, [string]$Output) {
  $node = (Get-Command $NodePath -CommandType Application | Select-Object -First 1).Source
  if (!(Test-Path -LiteralPath $verifier -PathType Leaf)) { throw '请先构建 Release 原生验签工具。' }
  $assetsDirectory = (Get-Item -LiteralPath $Assets).FullName
  if (!(Test-Path -LiteralPath $assetsDirectory -PathType Container)) { throw '请选择资源目录。' }
  $keyFile = (Get-Item -LiteralPath $Key).FullName
  $outputFile = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Output)
  $assetPrefix = $assetsDirectory.TrimEnd('\') + '\'
  if ($outputFile.StartsWith($assetPrefix, [StringComparison]::OrdinalIgnoreCase) -or
      $keyFile.StartsWith($assetPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw '输出包和签名私钥必须位于资源目录之外。'
  }
  if ([IO.Path]::GetExtension($outputFile) -ine '.ypkg') { throw '输出扩展名必须为 .ypkg。' }
  if (Test-Path -LiteralPath $outputFile) { throw '输出已存在，请选择新文件名。' }
  $stage = Join-Path (Split-Path -Parent $outputFile) ('.ypkg-' + [guid]::NewGuid().ToString('N'))
  [IO.Directory]::CreateDirectory($stage) | Out-Null
  $tempManifest = Join-Path $stage 'manifest.json'
  $tempPackage = Join-Path $stage 'package.ypkg'
  try {
    [IO.File]::WriteAllText($tempManifest, $Manifest)
    $packer = Join-Path $PSScriptRoot 'ypkg.mjs'
    if ($LocalPublisher) {
      Add-Type -AssemblyName System.Security
      $publisher = Get-Content -LiteralPath $keyFile -Raw -Encoding UTF8 | ConvertFrom-Json
      $encrypted = [Convert]::FromBase64String($publisher.protectedKey)
      $der = [Security.Cryptography.ProtectedData]::Unprotect($encrypted, $null, [Security.Cryptography.DataProtectionScope]::CurrentUser)
      try {
        $pem = "-----BEGIN EC PRIVATE KEY-----`n" + [Convert]::ToBase64String($der) + "`n-----END EC PRIVATE KEY-----"
        $buildLog = $pem | & $node $packer pack-local $tempManifest $assetsDirectory $keyFile $tempPackage 2>&1
      } finally { [Array]::Clear($der, 0, $der.Length) }
    } else {
      $buildLog = & $node $packer pack $tempManifest $assetsDirectory $keyFile $tempPackage 2>&1
    }
    if ($LASTEXITCODE -ne 0) { throw ($buildLog -join [Environment]::NewLine) }
    $verification = & $verifier verify $tempPackage
    $verifyExit = $LASTEXITCODE
    $details = $verification | ConvertFrom-Json
    if ($verifyExit -ne 0) { throw $details.error.message }
    [IO.File]::Move($tempPackage, $outputFile)
    return $details
  } finally {
    [IO.File]::Delete($tempManifest)
    [IO.File]::Delete($tempPackage)
    [IO.Directory]::Delete($stage)
  }
}

if ($PSCmdlet.ParameterSetName -eq 'Batch') {
  try {
    New-VerifiedPackage (Get-Content -LiteralPath $ManifestPath -Raw -Encoding UTF8) $AssetsPath $SigningKeyPath $OutputPath |
      ConvertTo-Json -Depth 12
  } catch {
    if ($ErrorPath) { [IO.File]::WriteAllText($ErrorPath, $_.Exception.Message) }
    throw
  }
  return
}

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
[Windows.Forms.Application]::EnableVisualStyles()
$form = [Windows.Forms.Form]@{
  Text = '瑶光台 · YPKG 打包'; Width = 820; Height = 690
  MinimumSize = [Drawing.Size]::new(680, 540); StartPosition = 'CenterScreen'
}
$layout = [Windows.Forms.TableLayoutPanel]@{ Dock = 'Fill'; ColumnCount = 3; RowCount = 7; Padding = [Windows.Forms.Padding]::new(14) }
[void]$layout.ColumnStyles.Add([Windows.Forms.ColumnStyle]::new('Absolute', 100))
[void]$layout.ColumnStyles.Add([Windows.Forms.ColumnStyle]::new('Percent', 100))
[void]$layout.ColumnStyles.Add([Windows.Forms.ColumnStyle]::new('Absolute', 108))
foreach ($height in @(32, 32, 32, 38)) {
  [void]$layout.RowStyles.Add([Windows.Forms.RowStyle]::new('Absolute', $height))
}
[void]$layout.RowStyles.Add([Windows.Forms.RowStyle]::new('Percent', 100))
[void]$layout.RowStyles.Add([Windows.Forms.RowStyle]::new('Absolute', 70))
[void]$layout.RowStyles.Add([Windows.Forms.RowStyle]::new('Absolute', 38))
$form.Controls.Add($layout)

function Add-PathRow([int]$Row, [string]$Label, [Windows.Forms.CommonDialog]$Dialog) {
  $field = [Windows.Forms.TextBox]@{ Dock = 'Fill'; AccessibleName = $Label }
  $button = [Windows.Forms.Button]@{ Text = '选择…'; Dock = 'Fill'; AccessibleName = "选择$Label" }
  $button.Add_Click({
    if ($Dialog.ShowDialog() -eq 'OK') {
      $field.Text = if ($Dialog -is [Windows.Forms.FolderBrowserDialog]) { $Dialog.SelectedPath } else { $Dialog.FileName }
    }
  }.GetNewClosure())
  $layout.Controls.Add([Windows.Forms.Label]@{ Text = $Label; Dock = 'Fill'; TextAlign = 'MiddleLeft' }, 0, $Row)
  $layout.Controls.Add($field, 1, $Row)
  $layout.Controls.Add($button, 2, $Row)
  return $field
}

$assets = Add-PathRow 0 '资源目录' ([Windows.Forms.FolderBrowserDialog]::new())
$key = Add-PathRow 1 '签名私钥' ([Windows.Forms.OpenFileDialog]@{ Filter = 'PEM 私钥 (*.pem)|*.pem|所有文件 (*.*)|*.*' })
$output = Add-PathRow 2 '输出包' ([Windows.Forms.SaveFileDialog]@{
  Filter = 'YPKG 包 (*.ypkg)|*.ypkg'; DefaultExt = 'ypkg'; OverwritePrompt = $false
})
$editor = [Windows.Forms.TextBox]@{
  Multiline = $true; Dock = 'Fill'; ScrollBars = 'Both'; WordWrap = $false
  Font = [Drawing.Font]::new('Consolas', 10); AccessibleName = '项目清单 JSON'
  Text = Get-Content -LiteralPath (Join-Path $project 'samples/inspector/manifest.json') -Raw -Encoding UTF8
}
$open = [Windows.Forms.Button]@{ Text = '打开清单…'; AutoSize = $true }
$save = [Windows.Forms.Button]@{ Text = '保存清单…'; AutoSize = $true }
$actions = [Windows.Forms.FlowLayoutPanel]@{ Dock = 'Fill' }
$actions.Controls.AddRange(@($open, $save))
$layout.Controls.Add([Windows.Forms.Label]@{ Text = '项目清单'; Dock = 'Fill'; TextAlign = 'MiddleLeft' }, 0, 3)
$layout.Controls.Add($actions, 1, 3)
$layout.SetColumnSpan($actions, 2)
$layout.Controls.Add($editor, 0, 4)
$layout.SetColumnSpan($editor, 3)
$status = [Windows.Forms.TextBox]@{ Dock = 'Fill'; Multiline = $true; ReadOnly = $true; ScrollBars = 'Vertical'; AccessibleName = '打包结果'; Text = '编辑或打开 JSON 清单，然后选择资源目录、签名私钥和输出路径。' }
$layout.Controls.Add($status, 0, 5)
$layout.SetColumnSpan($status, 3)
$submit = [Windows.Forms.Button]@{ Text = '打包并验签'; Dock = 'Fill' }
$layout.Controls.Add($submit, 2, 6)

$open.Add_Click({
  $dialog = [Windows.Forms.OpenFileDialog]@{ Filter = 'JSON 清单 (*.json)|*.json' }
  try {
    if ($dialog.ShowDialog($form) -eq 'OK') { $editor.Text = Get-Content -LiteralPath $dialog.FileName -Raw -Encoding UTF8 }
  } catch { $status.Text = $_.Exception.Message }
  finally { $dialog.Dispose() }
})
$save.Add_Click({
  $dialog = [Windows.Forms.SaveFileDialog]@{ Filter = 'JSON 清单 (*.json)|*.json'; DefaultExt = 'json' }
  try {
    if ($dialog.ShowDialog($form) -eq 'OK') {
      $null = ConvertFrom-Json -InputObject $editor.Text
      [IO.File]::WriteAllText($dialog.FileName, $editor.Text)
    }
  } catch { $status.Text = $_.Exception.Message }
  finally { $dialog.Dispose() }
})
$submit.Add_Click({
  $submit.Enabled = $false
  try {
    if ([string]::IsNullOrWhiteSpace($assets.Text) -or [string]::IsNullOrWhiteSpace($key.Text) -or
        [string]::IsNullOrWhiteSpace($output.Text)) { throw '请填写资源目录、签名私钥和输出路径。' }
    $status.Text = '正在打包并验签…'
    $status.Refresh()
    $info = New-VerifiedPackage $editor.Text $assets.Text $key.Text $output.Text
    $status.Text = "验签通过 · $($info.name) $($info.version)`r`n$($output.Text)`r`n$($info.publisherKeyId)"
  } catch { $status.Text = "打包失败：$($_.Exception.Message)" }
  finally { $submit.Enabled = $true }
})
try { [void]$form.ShowDialog() }
finally { $form.Dispose() }
