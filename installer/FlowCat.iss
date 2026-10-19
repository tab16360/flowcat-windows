#define AppVersion "0.2.0"
#ifndef ServiceBinary
#define ServiceBinary "..\build\Release\FlowCatService.exe"
#endif
#ifndef SetupName
#define SetupName "FlowCat-Windows-x64-Setup"
#endif
[Setup]
AppId={{31C8B769-174A-4135-92C9-489796E1D82B}
AppName=流量喵 Windows
AppVersion={#AppVersion}
AppPublisher=FlowCat
AppPublisherURL=https://github.com/tab16360/flowcat-windows
AppSupportURL=https://github.com/tab16360/flowcat-windows/issues
DefaultDirName={autopf}\FlowCat Windows
DefaultGroupName=流量喵 Windows
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.19045
OutputDir=..\dist
OutputBaseFilename={#SetupName}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
SetupIconFile=..\assets\FlowCat.ico
UninstallDisplayIcon={app}\FlowCat.exe
VersionInfoVersion={#AppVersion}.0
VersionInfoProductName=FlowCat Windows
CloseApplications=yes
RestartApplications=no
SetupLogging=yes
[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
[Files]
Source: "..\build\Release\FlowCat.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ServiceBinary}"; DestDir: "{app}"; DestName: "FlowCatService.exe"; Flags: ignoreversion
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\vendor\json-LICENSE.txt"; DestDir: "{app}\licenses"; Flags: ignoreversion
[Icons]
Name: "{group}\流量喵 Windows"; Filename: "{app}\FlowCat.exe"
Name: "{autodesktop}\流量喵 Windows"; Filename: "{app}\FlowCat.exe"; Tasks: desktopicon
[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; Flags: unchecked
[Run]
Filename: "{app}\FlowCat.exe"; Description: "Launch FlowCat"; Flags: nowait postinstall runasoriginaluser; Check: NotSilent
Filename: "{app}\FlowCat.exe"; Flags: nowait runasoriginaluser; Check: UpdateInstall
[UninstallDelete]
Type: filesandordirs; Name: "{app}\backup"
[Code]
var
  HadPrevious: Boolean;
  InstallFailed: Boolean;
function NotSilent(): Boolean;
begin Result := (not WizardSilent) and (not InstallFailed); end;
function UpdateInstall(): Boolean;
begin Result := (ExpandConstant('{param:UPDATE|0}') = '1') and (ExpandConstant('{param:NOAUTOSTART|0}') <> '1'); end;
procedure CloseTray();
var Window: HWND; Attempt: Integer;
begin
  Window := FindWindowByClassName('FlowCatWindowsPanel');
  if Window <> 0 then begin
    PostMessage(Window, $0111, 203, 0);
    for Attempt := 1 to 100 do begin
      if FindWindowByClassName('FlowCatWindowsPanel') = 0 then Break;
      Sleep(100);
    end;
  end;
end;
function RunService(Command: String): Boolean;
var Code: Integer;
begin
  Result := Exec(ExpandConstant('{app}\FlowCatService.exe'), Command, '', SW_HIDE, ewWaitUntilTerminated, Code) and (Code = 0);
end;
function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := '';
  CloseTray();
  HadPrevious := FileExists(ExpandConstant('{app}\FlowCatService.exe'));
  if HadPrevious then begin
    if not RunService('--stop-service') then begin Result := 'Could not stop the collector safely. Please retry.'; Exit; end;
    if not ForceDirectories(ExpandConstant('{app}\backup')) then begin Result := 'Could not create rollback directory.'; Exit; end;
    if not CopyFile(ExpandConstant('{app}\FlowCatService.exe'), ExpandConstant('{app}\backup\FlowCatService.exe'), False) then begin Result := 'Could not back up the collector.'; Exit; end;
    if not CopyFile(ExpandConstant('{app}\FlowCat.exe'), ExpandConstant('{app}\backup\FlowCat.exe'), False) then begin Result := 'Could not back up the application.'; Exit; end;
  end;
end;
procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then begin
    if not RunService('--install-service') then begin
      InstallFailed := True;
      RunService('--stop-service');
      if HadPrevious then begin
        CopyFile(ExpandConstant('{app}\backup\FlowCatService.exe'), ExpandConstant('{app}\FlowCatService.exe'), False);
        CopyFile(ExpandConstant('{app}\backup\FlowCat.exe'), ExpandConstant('{app}\FlowCat.exe'), False);
        RunService('--install-service');
      end;
      RaiseException('Collector installation failed. The previous version was restored when available. Your history was preserved.');
    end;
  end;
end;
function InitializeUninstall(): Boolean;
begin
  CloseTray();
  Result := RunService('--uninstall-service');
  if not Result then MsgBox('Could not stop the collector safely. Uninstall was cancelled.', mbError, MB_OK);
end;

function GetCustomSetupExitCode(): Integer;
begin
  if InstallFailed then Result := 8 else Result := 0;
end;
