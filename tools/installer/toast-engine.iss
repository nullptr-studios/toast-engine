; Build with: iscc /DAppVersion=2.8.0 /DSourceDir=<staged out folder> toast-engine.iss

#ifndef AppVersion
  #error AppVersion not defined
#endif
#ifndef SourceDir
  #error SourceDir not defined
#endif

[Setup]
AppId={{6B0F2C1E-5D8A-4E3B-9C41-7A2F8E5D1B30}
AppName=Toast Engine
AppVersion={#AppVersion}
AppPublisher=nullptr Studios
DefaultDirName={localappdata}\Programs\Toast Engine
DefaultGroupName=Toast Engine
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
CloseApplications=yes
RestartApplications=no
ChangesAssociations=yes
ChangesEnvironment=yes
OutputBaseFilename=ToastEngine-{#AppVersion}-setup
Compression=lzma2
SolidCompression=yes
UninstallDisplayIcon={app}\editor\editor.exe
WizardStyle=modern

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; Flags: unchecked

[InstallDelete]
; Do not leave files of the previous version behind
Type: filesandordirs; Name: "{app}\editor"
Type: filesandordirs; Name: "{app}\toast_engine"
Type: filesandordirs; Name: "{app}\reflection_generator"
Type: filesandordirs; Name: "{app}\packer"
Type: filesandordirs; Name: "{app}\crash_reporter"
Type: filesandordirs; Name: "{app}\log_server"
Type: filesandordirs; Name: "{app}\player"

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
Name: "{autoprograms}\Toast Engine"; Filename: "{app}\editor\editor.exe"; WorkingDir: "{app}\editor"
Name: "{autodesktop}\Toast Engine"; Filename: "{app}\editor\editor.exe"; WorkingDir: "{app}\editor"; Tasks: desktopicon

[Registry]
Root: HKCU; Subkey: "Software\Classes\.toast"; ValueType: string; ValueData: "ToastEngine.Project"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\ToastEngine.Project"; ValueType: string; ValueData: "Toast Engine Project"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\ToastEngine.Project\DefaultIcon"; ValueType: string; ValueData: """{app}\editor\editor.exe"",0"
Root: HKCU; Subkey: "Software\Classes\ToastEngine.Project\shell\open\command"; ValueType: string; ValueData: """{app}\editor\editor.exe"" ""%1"""
Root: HKCU; Subkey: "Environment"; ValueType: string; ValueName: "TOAST_PATH"; ValueData: "{app}\toast_engine"; Flags: uninsdeletevalue

[Run]
; Interactive install
Filename: "{app}\editor\editor.exe"; WorkingDir: "{app}\editor"; Description: "Launch Toast Engine"; Flags: nowait postinstall skipifsilent
; Update started from the editor
Filename: "{app}\editor\editor.exe"; WorkingDir: "{app}\editor"; Flags: nowait; Check: WizardSilent

[Code]
function DotNetDesktop8Installed: Boolean;
begin
  Result := DirExists(ExpandConstant('{pf}\dotnet\shared\Microsoft.WindowsDesktop.App')) or
            DirExists(ExpandConstant('{localappdata}\Microsoft\dotnet\shared\Microsoft.WindowsDesktop.App'));
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  ErrorCode: Integer;
begin
  if (CurStep = ssPostInstall) and (not WizardSilent) and (not DotNetDesktop8Installed) then
    if MsgBox('Toast Engine needs the .NET 8 Desktop Runtime, which was not found.' + #13#10 +
              'Open the download page now?', mbConfirmation, MB_YESNO) = IDYES then
      ShellExec('open', 'https://dotnet.microsoft.com/download/dotnet/8.0', '', '', SW_SHOWNORMAL, ewNoWait, ErrorCode);
end;
