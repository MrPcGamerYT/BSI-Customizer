; Inno Setup script for BSI Customizer.
;
; The released download is an installer, not a loose EXE: the tool has to sit
; in a folder the user keeps, and its self-update has to run a setup instead of
; overwriting a file Windows currently has open. The setup installs the same
; single-file build CI already tested.
;
; Built by .github/workflows/release.yml:
;   ISCC.exe /DAppVersion=2.5.1 /DSourceExe=<exe> /DOutputDir=<dir> BSI-Customizer.iss
;
; Nothing here is required to run the app: the installed EXE is still portable,
; a copy of it runs from anywhere.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef SourceExe
  #define SourceExe "..\..\build\artifacts\BSI-Customizer.exe"
#endif
#ifndef OutputDir
  #define OutputDir "..\..\build\artifacts"
#endif

[Setup]
; A stable AppId: this is what makes an update install over the old copy
; instead of becoming a second entry in "Programs and features".
AppId={{7C4C1E2B-2F61-4E1A-9F6B-5B6C0D7A3E21}
AppName=BSI Customizer
AppVersion={#AppVersion}
AppVerName=BSI Customizer {#AppVersion}
AppPublisher=Mr.Pc Gamer
AppPublisherURL=https://github.com/MrPcGamerYT/BSI-Customizer
AppSupportURL=https://github.com/MrPcGamerYT/BSI-Customizer/issues
AppUpdatesURL=https://github.com/MrPcGamerYT/BSI-Customizer/releases
DefaultDirName={autopf}\BSI Customizer
DefaultGroupName=BSI Customizer
DisableProgramGroupPage=yes
AllowNoIcons=yes
OutputDir={#OutputDir}
OutputBaseFilename=BSI-Customizer-{#AppVersion}-setup
SetupIconFile=..\..\BSI CUSTOMIZER.ico
UninstallDisplayIcon={app}\BSI-Customizer.exe
UninstallDisplayName=BSI Customizer {#AppVersion}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
; The tool patches files inside the BlueStacks install, so it runs elevated
; anyway; installing to Program Files keeps the uninstaller honest.
PrivilegesRequired=admin
ArchitecturesInstallIn64BitMode=x64
; A self-update starts the setup while the old build is still closing: let the
; Restart Manager take care of it instead of failing on a locked file.
CloseApplications=yes
RestartApplications=no
MinVersion=10.0

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#SourceExe}"; DestDir: "{app}"; DestName: "BSI-Customizer.exe"; Flags: ignoreversion

[Icons]
Name: "{group}\BSI Customizer"; Filename: "{app}\BSI-Customizer.exe"
Name: "{group}\{cm:UninstallProgram,BSI Customizer}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\BSI Customizer"; Filename: "{app}\BSI-Customizer.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\BSI-Customizer.exe"; Description: "{cm:LaunchProgram,BSI Customizer}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; Leftovers of a patch: the backup the tool keeps next to HD-Player.exe is not
; ours to delete, but its own settings and logs live with the user profile.
Type: filesandordirs; Name: "{localappdata}\BSI Customizer\cache"
