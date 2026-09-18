; Inno Setup script for the Windows installer.
;
; Every name, version and path arrives as a /D define from
; .github/scripts/Package-Windows.ps1, which reads them out of buildspec.json.
; Nothing is spelled twice, and the script needs no CMake configure step.
;
;   iscc /DAppName=fff-tools /DAppDisplayName="FFF Tools for OBS" ^
;        /DAppVersion=0.2.0 /DAppPublisher="..." /DAppURL="..." ^
;        /DSourceDir="...\release\RelWithDebInfo" /DOutputDir="...\release" ^
;        installer-windows.iss
;
; install() lays the plugin out as
;   <AppName>\bin\64bit\<AppName>.dll
;   <AppName>\data\...
; which is the shape OBS expects under its plugins directory, so the installer
; copies that tree across unchanged.

[Setup]
AppId={{9E6E9A5C-1C4C-4E4E-9B2B-6F2D0B1B5F21}
AppName={#AppDisplayName}
AppVersion={#AppVersion}
AppVerName={#AppDisplayName} {#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
AppSupportURL={#AppURL}
AppUpdatesURL={#AppURL}
VersionInfoVersion={#AppVersion}
DefaultDirName={commonappdata}\obs-studio\plugins\{#AppName}
DefaultGroupName={#AppDisplayName}
; The plugin is a folder of files, not a program: anywhere other than the OBS
; plugins directory simply would not load, so there is nothing to choose.
DisableDirPage=yes
DisableProgramGroupPage=yes
CreateAppDir=yes
Uninstallable=yes
UninstallDisplayName={#AppDisplayName} {#AppVersion}
OutputDir={#OutputDir}
OutputBaseFilename={#AppName}-{#AppVersion}-windows-x64
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
; Writing under ProgramData needs an elevated install.
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
Source: "{#SourceDir}\{#AppName}\bin\64bit\*"; DestDir: "{app}\bin\64bit"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SourceDir}\{#AppName}\data\*"; DestDir: "{app}\data"; Flags: ignoreversion recursesubdirs createallsubdirs

[Messages]
; OBS reads its plugins directory at start-up only, so installing while it is
; running does nothing until it is restarted. Say so where it cannot be missed.
FinishedLabel=Setup has installed {#AppDisplayName} on your computer.%n%nRestart OBS Studio, then open the dock from Docks in the menu bar.
