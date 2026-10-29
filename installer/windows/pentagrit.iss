; Pentagrit Windows installer (Inno Setup 6.4+).
; Built by scripts/ci/make_installer_windows.ps1, which passes:
;   /DAppVersion=X.Y.Z        version without the leading "v" and without -rcN
;   /DAppVersionText=X.Y.Z-rc1 full version string (optional, defaults to AppVersion)
;   /DSourceDir=<dir>          unzipped package: Pentagrit.vst3\, Pentagrit.clap, Pentagrit.exe, LICENSE
;   /DOutputDir=<dir>          where the setup exe is written
;   /DOutputName=<name>        base name of the setup exe (no extension)
#ifndef AppVersion
  #error AppVersion must be defined (iscc /DAppVersion=1.2.3 ...)
#endif
#ifndef AppVersionText
  #define AppVersionText AppVersion
#endif
#ifndef SourceDir
  #error SourceDir must be defined
#endif
#ifndef OutputDir
  #define OutputDir "."
#endif
#ifndef OutputName
  #define OutputName "Pentagrit-v" + AppVersionText + "-windows-setup"
#endif

[Setup]
; Never change AppId: it identifies the installation for upgrades and uninstall.
AppId={{6B0F7C0E-4B64-4D55-9A0B-5E6C0A7D1F42}
AppName=Pentagrit
AppVersion={#AppVersionText}
AppVerName=Pentagrit {#AppVersionText}
AppPublisher=Ivan Erricis
AppPublisherURL=https://github.com/ivanerricis/pentagrit
AppSupportURL=https://github.com/ivanerricis/pentagrit/issues
AppUpdatesURL=https://github.com/ivanerricis/pentagrit/releases
VersionInfoVersion={#AppVersion}.0
DefaultDirName={autopf}\Pentagrit
DefaultGroupName=Pentagrit
DisableProgramGroupPage=yes
; Per-machine install: VST3/CLAP go to %CommonProgramFiles%, which needs admin.
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
LicenseFile={#SourceDir}\LICENSE
OutputDir={#OutputDir}
OutputBaseFilename={#OutputName}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName=Pentagrit {#AppVersionText}
UninstallFilesDir={autopf}\Pentagrit
CloseApplications=no

[Types]
Name: "full"; Description: "Full installation (VST3, CLAP, Standalone)"
Name: "custom"; Description: "Custom installation"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 plug-in"; Types: full custom
Name: "clap"; Description: "CLAP plug-in"; Types: full custom
Name: "standalone"; Description: "Standalone application (with Start menu shortcut)"; Types: full custom

[Files]
; VST3 is a bundle directory on Windows.
Source: "{#SourceDir}\Pentagrit.vst3\*"; DestDir: "{commoncf64}\VST3\Pentagrit.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SourceDir}\Pentagrit.clap"; DestDir: "{commoncf64}\CLAP"; Components: clap; Flags: ignoreversion
Source: "{#SourceDir}\Pentagrit.exe"; DestDir: "{app}"; Components: standalone; Flags: ignoreversion
; Licence is always installed next to the uninstaller.
Source: "{#SourceDir}\LICENSE"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\Pentagrit"; Filename: "{app}\Pentagrit.exe"; Components: standalone

[Run]
Filename: "{app}\Pentagrit.exe"; Description: "Launch Pentagrit"; Components: standalone; Flags: nowait postinstall skipifsilent unchecked
