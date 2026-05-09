#define MyAppName "Aura"
#define MyAppVersion "0.1.0-prealpha"
#define MyAppPublisher "Aura contributors"
#define MyAppURL "https://github.com/auralanguage/aura-lang"
#define MyAppExeName "aura.exe"
#define MySourceExe "..\\build\\aura.exe"

[Setup]
AppId={{A2A5D8B8-1C76-4A17-A61B-0DF44E6D87D3}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
DefaultDirName={autopf}\Aura
DefaultGroupName=Aura
DisableProgramGroupPage=yes
LicenseFile=..\LICENSE
OutputDir=..\dist
OutputBaseFilename=AuraSetup-x64
Compression=lzma
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
SetupIconFile=..\branding\aura.ico
UninstallDisplayIcon={app}\{#MyAppExeName}

[Tasks]
Name: "addtopath"; Description: "Add Aura to PATH"; GroupDescription: "Additional tasks:"

[Files]
Source: "{#MySourceExe}"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\examples\*"; DestDir: "{app}\examples"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\Aura"; Filename: "{app}\{#MyAppExeName}"
Name: "{autodesktop}\Aura"; Filename: "{app}\{#MyAppExeName}"

[Registry]
Root: HKCU; Subkey: "Environment"; ValueType: expandsz; ValueName: "Path"; ValueData: "{olddata};{app}"; Check: NeedsAddPath(ExpandConstant('{app}')); Tasks: addtopath

[Code]
function NeedsAddPath(Param: string): boolean;
var
  OrigPath: string;
begin
  if not RegQueryStringValue(HKCU, 'Environment', 'Path', OrigPath) then
    OrigPath := '';
  Result := Pos(';' + Uppercase(Param) + ';', ';' + Uppercase(OrigPath) + ';') = 0;
end;
