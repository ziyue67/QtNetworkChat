#ifndef AppVersion
  #error AppVersion must be provided
#endif
#ifndef StageDir
  #error StageDir must be provided
#endif
#ifndef OutputDir
  #error OutputDir must be provided
#endif

[Setup]
AppId=io.github.ziyue67.qtnetworkchat
AppName=QtNetworkChat
AppVersion={#AppVersion}
AppPublisher=ziyue67
AppPublisherURL=https://github.com/ziyue67/QtNetworkChat
DefaultDirName={localappdata}\Programs\QtNetworkChat
DefaultGroupName=QtNetworkChat
UninstallDisplayIcon={app}\QtNetworkChat.exe
OutputDir={#OutputDir}
OutputBaseFilename=QtNetworkChat-{#AppVersion}-win-x64-setup
Compression=lzma2
SolidCompression=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
WizardStyle=modern
CloseApplications=yes

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Excludes: "manifest.json"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\QtNetworkChat"; Filename: "{app}\QtNetworkChat.exe"
Name: "{autodesktop}\QtNetworkChat"; Filename: "{app}\QtNetworkChat.exe"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional shortcuts:"; Flags: unchecked

[Run]
Filename: "{app}\QtNetworkChat.exe"; Description: "Launch QtNetworkChat"; Flags: nowait postinstall skipifsilent
