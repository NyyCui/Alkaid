#ifndef Payload
  #error "请通过 build-installer.ps1 准备 Release 文件"
#endif

[Setup]
AppId=Alkaid.Workspace
AppName=瑶光台
AppVersion=2.0.0-dev.6
AppPublisher=瑶光台
DefaultDirName={localappdata}\Programs\Alkaid
DefaultGroupName=瑶光台
DisableProgramGroupPage=yes
AllowNoIcons=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
WizardStyle=modern
SetupIconFile={#BrandIcon}
UninstallDisplayIcon={app}\alkaid_host.exe
Compression=lzma2/fast
SolidCompression=yes
OutputBaseFilename=Alkaid-Setup-x64
CloseApplications=yes
RestartApplications=no

[Files]
Source: "{#Payload}\alkaid_host.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Payload}\*.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Payload}\*.pak"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Payload}\icudtl.dat"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Payload}\v8_context_snapshot.bin"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Payload}\vk_swiftshader_icd.json"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Payload}\CEF-*"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Payload}\developer\*"; DestDir: "{app}\developer"; Flags: ignoreversion recursesubdirs
Source: "{#Payload}\licenses\*"; DestDir: "{app}\licenses"; Flags: ignoreversion recursesubdirs
Source: "{#Payload}\locales\*"; DestDir: "{app}\locales"; Flags: ignoreversion recursesubdirs
Source: "{#Payload}\web\*"; DestDir: "{app}\web"; Flags: ignoreversion recursesubdirs
Source: "{#Crt}\*.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Crt}\*.dll"; DestDir: "{app}\developer"; Flags: ignoreversion

[Icons]
Name: "{group}\瑶光台"; Filename: "{app}\alkaid_host.exe"; WorkingDir: "{app}"; AppUserModelID: Alkaid.Desktop
Name: "{userdesktop}\瑶光台"; Filename: "{app}\alkaid_host.exe"; WorkingDir: "{app}"; AppUserModelID: Alkaid.Desktop; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "创建桌面快捷方式"; Flags: unchecked

[UninstallDelete]
Type: files; Name: "{app}\data-location.txt"

[Messages]
WelcomeLabel1=欢迎安装瑶光台
WelcomeLabel2=安装向导会分别设置程序位置和数据位置。
SelectDirLabel3=请选择瑶光台程序的安装位置。
ButtonNext=下一步(&N) >
ButtonBack=< 上一步(&B)
ButtonBrowse=浏览(&R)…
ButtonInstall=安装(&I)
ButtonCancel=取消
ButtonFinish=完成(&F)

[Code]
var
  DataPage: TInputDirWizardPage;
  PreviousDataDir, SelectedDataDir: String;

function UnderPath(const Parent, Child: String): Boolean;
begin
  Result := (CompareText(Parent, Child) = 0) or
    (Pos(Lowercase(AddBackslash(Parent)), Lowercase(AddBackslash(Child))) = 1);
end;

function IsLocalFolder(const Path: String): Boolean;
begin
  Result := (Length(Path) > 3) and (Path[2] = ':') and (Path[3] = '\') and
    ((Uppercase(Copy(Path, 1, 1)) >= 'A') and (Uppercase(Copy(Path, 1, 1)) <= 'Z'));
end;

procedure InitializeWizard;
var
  Choice: String;
begin
  PreviousDataDir := GetPreviousData('DataDir', '');
  DataPage := CreateInputDirPage(wpSelectDir, '选择数据位置', '应用、插件和个人数据将存放在这里',
    '请选择当前 Windows 用户可写的独立目录。卸载瑶光台时会保留此目录及其中的数据。', False, 'Alkaid Data');
  DataPage.Add('数据目录');
  Choice := ExpandConstant('{param:DATADIR|}');
  if PreviousDataDir <> '' then
    DataPage.Values[0] := PreviousDataDir
  else if Choice <> '' then
    DataPage.Values[0] := Choice
  else
    DataPage.Values[0] := ExpandConstant('{localappdata}\Alkaid\Data');
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  Result := (PageID = DataPage.ID) and (PreviousDataDir <> '');
end;

function CheckPaths: String;
var
  AppDir: String;
begin
  Result := '';
  AppDir := RemoveBackslashUnlessRoot(ExpandFileName(WizardDirValue));
  SelectedDataDir := Trim(DataPage.Values[0]);
  if not IsLocalFolder(SelectedDataDir) then
  begin
    Result := '请选择本机磁盘上的数据文件夹，例如 D:\AlkaidData。';
    Exit;
  end;
  SelectedDataDir := RemoveBackslashUnlessRoot(ExpandFileName(SelectedDataDir));
  if not IsLocalFolder(SelectedDataDir) then
    Result := '请为瑶光台选择一个专用数据文件夹。'
  else if UnderPath(AppDir, SelectedDataDir) or UnderPath(SelectedDataDir, AppDir) then
    Result := '程序目录与数据目录必须分开，且不能互相包含。';
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var
  ErrorText: String;
begin
  Result := True;
  if CurPageID = DataPage.ID then
  begin
    ErrorText := CheckPaths;
    Result := ErrorText = '';
    if not Result then MsgBox(ErrorText, mbError, MB_OK);
  end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  Probe: String;
begin
  Result := CheckPaths;
  if Result <> '' then Exit;
  if not ForceDirectories(SelectedDataDir) then
  begin
    Result := '无法创建数据目录，请选择当前用户可写的位置。';
    Exit;
  end;
  Probe := AddBackslash(SelectedDataDir) + '.alkaid-write-' + IntToStr(Random(2147483647));
  if FileExists(Probe) or not SaveStringToFile(Probe, 'Alkaid', False) then
    Result := '无法写入数据目录，请选择当前用户可写的位置。'
  else
    DeleteFile(Probe);
end;

procedure RegisterPreviousData(PreviousDataKey: Integer);
begin
  SetPreviousData(PreviousDataKey, 'DataDir', SelectedDataDir);
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
    if not SaveStringToFile(ExpandConstant('{app}\data-location.txt'), UTF8Encode(SelectedDataDir), False) then
      RaiseException('无法保存瑶光台数据位置。');
end;
