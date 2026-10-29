; SPDX-License-Identifier: GPL-3.0-only
; Copyright (C) 2026 ProjectHax LLC
;
; Inno Setup script for nutty: a per-user Windows installer.
;
; Installs to %LOCALAPPDATA%\Programs\nutty with a Start Menu shortcut and an
; Add/Remove Programs entry. Per-user (no admin): standard users can still
; sleep, hibernate and shut down, and the bundled MSVC runtime sits next to
; nutty.exe, so nothing needs elevation.
;
; Built by .github/workflows/release.yml:
;
;   ISCC /DMyAppVersion=0.1.0 /DMyArch=x86_64 /DSourceDir=<deploy dir> ^
;        /DAssetDir=<repo>\resources\appicon /O<out dir> packaging\windows\nutty.iss
;
; When the release job can sign (Azure Trusted Signing), it also passes
;   /DSignToolName=jsign "/Sjsign=<jsign command> $f"
; so ISCC signs both the installer and the uninstaller it embeds.

#ifndef MyAppVersion
  #define MyAppVersion "0.0.0"
#endif
#ifndef MyArch
  #define MyArch "x86_64"
#endif
#ifndef SourceDir
  #define SourceDir "dist"
#endif
#ifndef AssetDir
  #define AssetDir "resources\appicon"
#endif

#define MyAppName "nutty"
#define MyAppPublisher "ProjectHax LLC"
#define MyAppURL "https://github.com/ProjectHax/nutty"
#define MyAppExeName "nutty.exe"

[Setup]
; A stable AppId keeps upgrades and uninstall coherent across versions; never change it.
AppId={{223BB2F9-C138-430F-A2BF-26D42240B817}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}/issues
AppUpdatesURL={#MyAppURL}/releases
AppCopyright=Copyright (C) 2026 {#MyAppPublisher}
VersionInfoVersion={#MyAppVersion}
DefaultDirName={localappdata}\Programs\nutty
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
OutputDir=.
OutputBaseFilename=nutty-windows-{#MyArch}-setup
SetupIconFile={#AssetDir}\nutty.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
UninstallDisplayName={#MyAppName}
LicenseFile=..\..\LICENSE
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
; nutty runs in the tray, so offer to close it for an upgrade or uninstall.
CloseApplications=yes
RestartApplications=no
#if MyArch == "arm64"
ArchitecturesAllowed=arm64
ArchitecturesInstallIn64BitMode=arm64
#else
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
#endif
#ifdef SignToolName
SignTool={#SignToolName}
SignedUninstaller=yes
#endif

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "brazilianportuguese"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"
Name: "czech"; MessagesFile: "compiler:Languages\Czech.isl"
Name: "dutch"; MessagesFile: "compiler:Languages\Dutch.isl"
Name: "french"; MessagesFile: "compiler:Languages\French.isl"
Name: "german"; MessagesFile: "compiler:Languages\German.isl"
Name: "italian"; MessagesFile: "compiler:Languages\Italian.isl"
Name: "japanese"; MessagesFile: "compiler:Languages\Japanese.isl"
Name: "polish"; MessagesFile: "compiler:Languages\Polish.isl"
Name: "portuguese"; MessagesFile: "compiler:Languages\Portuguese.isl"
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"
Name: "spanish"; MessagesFile: "compiler:Languages\Spanish.isl"
Name: "turkish"; MessagesFile: "compiler:Languages\Turkish.isl"
Name: "ukrainian"; MessagesFile: "compiler:Languages\Ukrainian.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
; Everything windeployqt staged: nutty.exe, Qt DLLs, plugins, QML modules,
; translations and the MSVC runtime.
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#MyAppName}}"; Flags: nowait postinstall skipifsilent

[Code]
// nutty writes its own "start at login" entry when the user turns it on in
// Settings; remove it on uninstall so nothing points at a deleted exe.
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usPostUninstall then
    RegDeleteValue(HKEY_CURRENT_USER, 'Software\Microsoft\Windows\CurrentVersion\Run', 'nutty');
end;
