#ifndef AppVersion
  #define AppVersion "2.2.5"
#endif
#ifndef StageDir
  #define StageDir "..\build\installer\payload"
#endif
#ifndef OutputDir
  #define OutputDir "..\build\installer"
#endif
#ifndef AppIcon
  #define AppIcon "..\resources\LumenPlayer.ico"
#endif
#ifndef WizardSmallImage
  #define WizardSmallImage "wizard-small.bmp"
#endif

[Setup]
AppId={{E1EAFEDB-81C9-4D10-983B-2DDFE2DA9F15}
AppName=Lumen Player
AppVersion={#AppVersion}
AppVerName=Lumen Player {#AppVersion}
AppPublisher=Lumen Player
AppComments=Native Windows video and audio player
DefaultDirName={localappdata}\Programs\LumenPlayer
DefaultGroupName=Lumen Player
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.19045
OutputDir={#OutputDir}
OutputBaseFilename=LumenPlayer-{#AppVersion}-x64-setup
SetupIconFile={#AppIcon}
UninstallDisplayIcon={app}\LumenPlayer.exe
UninstallDisplayName=Lumen Player
Compression=lzma2/max
SolidCompression=yes
LZMAUseSeparateProcess=yes
WizardStyle=modern
WizardSmallImageFile={#WizardSmallImage}
ShowLanguageDialog=no
SetupLogging=yes
CloseApplications=yes
RestartApplications=no
UsePreviousAppDir=yes
VersionInfoCompany=Lumen Player
VersionInfoDescription=Lumen Player Setup
VersionInfoProductName=Lumen Player
VersionInfoProductVersion={#AppVersion}
VersionInfoVersion={#AppVersion}.0

[Languages]
Name: "chinesesimp"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: checkedonce

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\Lumen Player"; Filename: "{app}\LumenPlayer.exe"; WorkingDir: "{app}"; Comment: "Lumen Player"
Name: "{autodesktop}\Lumen Player"; Filename: "{app}\LumenPlayer.exe"; WorkingDir: "{app}"; Tasks: desktopicon; Comment: "Lumen Player"

[Run]
Filename: "{app}\LumenPlayer.exe"; Description: "{cm:LaunchProgram,Lumen Player}"; Flags: nowait postinstall skipifsilent

[Code]
var
  UserDataBackup: String;
  SettingsSaved, PlaylistsSaved: Boolean;

procedure PreserveUserData;
var
  Folder: String;
begin
  if not FileExists(ExpandConstant('{app}\LumenPlayer.ini')) and
     not FileExists(ExpandConstant('{app}\LumenPlaylists.dat')) then Exit;
  Folder := ExpandConstant('{app}\.lumen-preserved-') + GetDateTimeString('yyyymmddhhnnss', #0, #0);
  while DirExists(Folder) or FileExists(Folder) do Folder := Folder + '-';
  if not ForceDirectories(Folder) then
    RaiseException('无法备份用户设置，已取消卸载。请检查目录权限。');
  UserDataBackup := Folder;
  if FileExists(ExpandConstant('{app}\LumenPlayer.ini')) then begin
    if not FileCopy(ExpandConstant('{app}\LumenPlayer.ini'), Folder + '\LumenPlayer.ini', True) then
      RaiseException('无法备份设置，已取消卸载。请检查文件占用。');
    SettingsSaved := True;
  end;
  if FileExists(ExpandConstant('{app}\LumenPlaylists.dat')) then begin
    if not FileCopy(ExpandConstant('{app}\LumenPlaylists.dat'), Folder + '\LumenPlaylists.dat', True) then
      RaiseException('无法备份播放列表，已取消卸载。请检查文件占用。');
    PlaylistsSaved := True;
  end;
end;

procedure RestoreUserData;
begin
  if UserDataBackup = '' then Exit;
  { Prior installers retain delete records. Keep a later user write if present. }
  if SettingsSaved and not FileExists(ExpandConstant('{app}\LumenPlayer.ini')) then
    if not FileCopy(UserDataBackup + '\LumenPlayer.ini', ExpandConstant('{app}\LumenPlayer.ini'), True) then
      RaiseException('设置恢复失败，备份已保留：' + UserDataBackup);
  if PlaylistsSaved and not FileExists(ExpandConstant('{app}\LumenPlaylists.dat')) then
    if not FileCopy(UserDataBackup + '\LumenPlaylists.dat', ExpandConstant('{app}\LumenPlaylists.dat'), True) then
      RaiseException('播放列表恢复失败，备份已保留：' + UserDataBackup);
  if SettingsSaved then DeleteFile(UserDataBackup + '\LumenPlayer.ini');
  if PlaylistsSaved then DeleteFile(UserDataBackup + '\LumenPlaylists.dat');
  RemoveDir(UserDataBackup);
  UserDataBackup := '';
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then PreserveUserData;
  if CurUninstallStep = usPostUninstall then RestoreUserData;
end;

procedure DeinitializeUninstall;
begin
  RestoreUserData;
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  ResultCode: Integer;
  Parameters: String;
begin
  if CurStep = ssPostInstall then
  begin
    { Only immutable application assets. Unsupported filesystems keep ordinary
      files; playback does not depend on compression succeeding. }
    Parameters := '/C /EXE:XPRESS8K /I /Q "' + ExpandConstant('{app}\LumenPlayer.exe') +
      '" "' + ExpandConstant('{app}\*.dll') + '"';
    if Exec(ExpandConstant('{sys}\compact.exe'), Parameters, '', SW_HIDE,
      ewWaitUntilTerminated, ResultCode) then
      Log(Format('Runtime storage compression finished: %d', [ResultCode]))
    else
      Log('Runtime storage compression unavailable; ordinary files retained.');
  end;
end;

[UninstallDelete]
; User-created settings, playlists and model directories survive uninstall.
Type: files; Name: "{app}\LumenPlayer.log"
Type: files; Name: "{app}\LumenPlayer.log.previous"
Type: filesandordirs; Name: "{app}\cache"
