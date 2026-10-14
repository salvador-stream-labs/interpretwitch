; SPDX-License-Identifier: GPL-2.0-or-later
; Inno Setup script for the staged Windows plugin. Built by build-windows.ps1:
;   ISCC /DStage=<package>\awl-chat /DOutputDir=<dir> /DAppVersion=<version> installer-windows.iss
; Installs into OBS's system plugin directory. Cached models and settings in
; %APPDATA%\obs-studio\plugin_config\awl-chat are never touched.

#ifndef AppVersion
  #error Pass /DAppVersion=<version>
#endif
#define ObsVersion "32.2.2"

[Setup]
AppId={{6B0C1E7A-3F2D-4C55-9A61-5E2B7D4F8A13}
AppName=Awl Interpretwitch
AppVersion={#AppVersion}
AppPublisher=AWL contributors
DefaultDirName={commonappdata}\obs-studio\plugins\awl-chat
DisableDirPage=yes
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir={#OutputDir}
OutputBaseFilename=AwlInterpretwitch-{#AppVersion}-windows-x64-setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName=Awl Interpretwitch (OBS plugin)
CloseApplications=no

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"
Name: "ru"; MessagesFile: "compiler:Languages\Russian.isl"

[CustomMessages]
en.NoObs=OBS Studio was not found. Install OBS Studio {#ObsVersion} first.
ru.NoObs=OBS Studio не найден. Сначала установите OBS Studio {#ObsVersion}.
en.WrongObs=This development package is built for OBS {#ObsVersion}. Found OBS %1.
ru.WrongObs=Этот пакет собран для OBS {#ObsVersion}. Установлена версия OBS %1.
en.ObsRunning=Quit OBS completely, then click Retry.
ru.ObsRunning=Полностью закройте OBS и нажмите «Повтор».

[Files]
Source: "{#Stage}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[Messages]
en.FinishedLabel=Setup has installed Awl Interpretwitch.%n%nOpen OBS → Docks → Awl Interpretwitch. Complete Twitch setup, download the model, enter your channel, then Connect Twitch.
ru.FinishedLabel=Awl Interpretwitch установлен.%n%nОткройте OBS → Доки → Awl Interpretwitch. Настройте Twitch, загрузите модель, укажите канал и нажмите «Подключить Twitch».

[Code]
var
  Backup: String;
  Installed: Boolean;

function ObsDirectory(): String;
begin
  if not RegQueryStringValue(HKLM64, 'SOFTWARE\OBS Studio', '', Result) then
    Result := ExpandConstant('{commonpf64}\obs-studio');
end;

function ObsRunning(): Boolean;
var
  Locator, Service, Processes: Variant;
begin
  Result := False;
  try
    Locator := CreateOleObject('WbemScripting.SWbemLocator');
    Service := Locator.ConnectServer('.', 'root\CIMV2');
    Processes := Service.ExecQuery('SELECT ProcessId FROM Win32_Process WHERE Name = ''obs64.exe''');
    Result := Processes.Count > 0;
  except
    Result := False;
  end;
end;

function InitializeSetup(): Boolean;
var
  Exe, Version: String;
begin
  Result := False;
  Exe := ObsDirectory() + '\bin\64bit\obs64.exe';
  if not FileExists(Exe) then begin
    SuppressibleMsgBox(CustomMessage('NoObs'), mbCriticalError, MB_OK, IDOK);
    exit;
  end;
  if not GetVersionNumbersString(Exe, Version) then Version := '?';
  if (Version <> '{#ObsVersion}') and (Pos('{#ObsVersion}.', Version) <> 1) then begin
    SuppressibleMsgBox(FmtMessage(CustomMessage('WrongObs'), [Version]), mbCriticalError, MB_OK, IDOK);
    exit;
  end;
  while ObsRunning() do
    if SuppressibleMsgBox(CustomMessage('ObsRunning'), mbError, MB_RETRYCANCEL, IDCANCEL) <> IDRETRY then
      exit;
  Result := True;
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  Backups: String;
begin
  if CurStep = ssInstall then begin
    // Keep the previous plugin intact, as the Mac installer does.
    if DirExists(ExpandConstant('{app}')) then begin
      Backups := ExpandConstant('{commonappdata}\obs-studio\awl-plugin-backups');
      ForceDirectories(Backups);
      Backup := Backups + '\chat-' + GetDateTimeString('yyyymmddhhnnss', #0, #0);
      if not RenameFile(ExpandConstant('{app}'), Backup) then
        RaiseException('Could not back up the existing plugin to ' + Backup);
    end;
  end else if CurStep = ssPostInstall then
    Installed := True;
end;

procedure DeinitializeSetup();
begin
  // Restore the backup if installation did not complete.
  if (Backup <> '') and not Installed then begin
    DelTree(ExpandConstant('{app}'), True, True, True);
    RenameFile(Backup, ExpandConstant('{app}'));
  end;
end;
