; ============================================================================
;  NoSteamWebHelper - Inno Setup installer
;
;  - Bundles the PREBUILT umpdc.dll (built separately; not compiled here).
;  - Detects the Steam folder, closes Steam, and drops the DLL next to steam.exe.
;  - Always REPLACES any existing umpdc.dll (old build or the original project's).
;  - Ships a built-in uninstaller that removes the DLL and the private registry
;    override key, and refuses to run while Steam is open (so the file isn't locked).
;
;  Build this installer with Inno Setup 6:  iscc installer\NoSteamWebHelper.iss
;  (Make sure src\bin\umpdc.dll exists first - build it via build.bat / the README.)
; ============================================================================

#define MyAppName "NoSteamWebHelper"
#define MyAppVersion "1.1.0"
#define MyAppPublisher "NoSteamWebHelper"
#define MyAppURL "https://github.com/Aetopia/NoSteamWebHelper"
#define DllSource "..\src\bin\umpdc.dll"

[Setup]
; A stable, unique AppId is what lets a new run detect and upgrade an existing
; install (and keeps a single Add/Remove Programs entry). Do not change it.
AppId={{45079A74-DAC0-444E-89ED-7F98D4F45B50}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppSupportURL={#MyAppURL}
VersionInfoVersion={#MyAppVersion}

; Install target: C:\Program Files (x86)\Steam
; {commonpf32} is the "Program Files (x86)" folder on 64-bit Windows, so this
; resolves to exactly C:\Program Files (x86)\Steam. The user can still change it
; on the directory page if their Steam lives elsewhere.
DefaultDirName={commonpf32}\Steam
DirExistsWarning=no
DisableProgramGroupPage=yes

; Writing into Program Files\Steam and replacing a DLL needs elevation.
PrivilegesRequired=admin

LicenseFile=..\LICENSE
SetupIconFile=..\src\res\icon_on.ico
UninstallDisplayIcon={app}\umpdc.dll
UninstallDisplayName={#MyAppName}

OutputDir=Output
OutputBaseFilename=Setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern

[Messages]
; Inno's defaults are "Setup" / "Setup - %1" / "%1 Uninstall", where %1 expands
; to "NoSteamWebHelper version 1.0.0". Override all three so every window,
; taskbar button and message box just reads "NoSteamWebHelper".
SetupAppTitle=NoSteamWebHelper
SetupWindowTitle=NoSteamWebHelper
UninstallAppTitle=NoSteamWebHelper
UninstallAppFullTitle=NoSteamWebHelper

[Files]
; DestName pins the hijack filename; ignoreversion + overwritereadonly guarantee
; the old DLL is replaced regardless of its version stamp or read-only attribute.
; Steam is closed first (PrepareToInstall), so the file is never locked here.
Source: "{#DllSource}"; DestDir: "{app}"; DestName: "umpdc.dll"; \
    Flags: ignoreversion overwritereadonly

[Registry]
; The DLL stores the tray override under this key. uninsdeletekey wipes it on
; uninstall so nothing is left behind.
Root: HKCU; Subkey: "SOFTWARE\NoSteamWebHelper"; Flags: uninsdeletekey

[Run]
Filename: "{app}\steam.exe"; Description: "Launch Steam now"; \
    Flags: postinstall nowait skipifsilent unchecked

[Code]
{ ---- Dark title bar (Windows 10 2004+) ----------------------------------- }
{ Inno has no native dark wizard theme; this darkens the caption/title bar via
  DWM. The wizard body stays in Inno's default light VCL theme (full dark would
  need the external VCL Styles plugin). On older Windows the attribute is simply
  ignored, so this degrades gracefully. }
const
  DWMWA_USE_IMMERSIVE_DARK_MODE = 20;
  DWMWA_USE_IMMERSIVE_DARK_MODE_OLD = 19;

function DwmSetWindowAttribute(Wnd: HWND; Attr: Integer; var Value: Integer; Size: Integer): Integer;
  external 'DwmSetWindowAttribute@dwmapi.dll stdcall';

procedure ApplyDarkTitleBar(Wnd: HWND);
var
  V: Integer;
begin
  V := 1;
  try
    if DwmSetWindowAttribute(Wnd, DWMWA_USE_IMMERSIVE_DARK_MODE, V, SizeOf(V)) <> 0 then
      DwmSetWindowAttribute(Wnd, DWMWA_USE_IMMERSIVE_DARK_MODE_OLD, V, SizeOf(V));
  except
    { dwmapi/attribute unavailable (pre-Win10): leave the title bar default. }
  end;
end;

procedure InitializeWizard();
begin
  ApplyDarkTitleBar(WizardForm.Handle);
end;

procedure InitializeUninstallProgressForm();
begin
  ApplyDarkTitleBar(UninstallProgressForm.Handle);
end;

{ ---- Is steam.exe currently running? ------------------------------------- }
function IsSteamRunning(): Boolean;
var
  Rc: Integer;
  TmpFile: String;
  Content: AnsiString;
begin
  Result := False;
  TmpFile := ExpandConstant('{tmp}\nswh_tasklist.txt');
  if Exec(ExpandConstant('{cmd}'),
          '/C tasklist /FI "IMAGENAME eq steam.exe" /NH > "' + TmpFile + '"',
          '', SW_HIDE, ewWaitUntilTerminated, Rc) then
  begin
    if LoadStringFromFile(TmpFile, Content) then
      Result := Pos('steam.exe', Lowercase(Content)) > 0;
    DeleteFile(TmpFile);
  end;
end;

{ ---- Close Steam: try a graceful shutdown, then force it ----------------- }
procedure ForceCloseSteam();
var
  Rc, I: Integer;
  SteamExe: String;
begin
  SteamExe := ExpandConstant('{app}\steam.exe');
  if FileExists(SteamExe) then
    Exec(SteamExe, '-shutdown', '', SW_HIDE, ewNoWait, Rc);

  { Give Steam up to ~12s to exit cleanly. }
  for I := 1 to 12 do
  begin
    if not IsSteamRunning() then
      Exit;
    Sleep(1000);
  end;

  { Still up: terminate the webhelper children first, then steam.exe. }
  Exec(ExpandConstant('{cmd}'),
       '/C taskkill /F /IM steamwebhelper.exe & taskkill /F /IM steam.exe',
       '', SW_HIDE, ewWaitUntilTerminated, Rc);
  Sleep(1500);
end;

{ ---- Validate the chosen folder actually looks like Steam ---------------- }
function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if CurPageID = wpSelectDir then
    if not FileExists(AddBackslash(WizardDirValue) + 'steam.exe') then
      if MsgBox('steam.exe was not found in:' + #13#10#13#10 + WizardDirValue + #13#10#13#10 +
                'This may not be your Steam folder. Install here anyway?',
                mbConfirmation, MB_YESNO) <> IDYES then
        Result := False;
end;

{ ---- Ensure Steam is closed before we copy/replace the DLL --------------- }
function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := '';
  if IsSteamRunning() then
  begin
    if MsgBox('Steam is running and must be closed so the DLL can be installed / replaced.'#13#10#13#10
              'Close Steam automatically now?', mbConfirmation, MB_YESNO) = IDYES then
    begin
      ForceCloseSteam();
      if IsSteamRunning() then
        Result := 'Steam could not be closed. Please close it manually and run Setup again.';
    end
    else
      Result := 'Setup was cancelled. Close Steam and run Setup again to continue.';
  end;
end;

{ ---- Uninstall: also require Steam closed so the DLL can be deleted ------- }
function InitializeUninstall(): Boolean;
begin
  Result := True;
  if IsSteamRunning() then
  begin
    if MsgBox('Steam is running and must be closed to remove the DLL.' + #13#10#13#10 +
              'Close Steam automatically now?', mbConfirmation, MB_YESNO) = IDYES then
    begin
      ForceCloseSteam();
      if IsSteamRunning() then
      begin
        MsgBox('Steam is still running. Please close it and run the uninstaller again.',
               mbError, MB_OK);
        Result := False;
      end;
    end
    else
    begin
      MsgBox('Uninstall cancelled. Close Steam and try again.', mbInformation, MB_OK);
      Result := False;
    end;
  end;
end;
