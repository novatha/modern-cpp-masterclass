; Script Inno Setup untuk GridGuard N-2
#define MyAppName "GridGuard N-2"
#define MyAppVersion "2.1.0"
#define MyAppPublisher "PT PLN (Persero) / ITB & UNIB Power Systems Research"
#define MyAppURL "https://github.com/novatha/modern-cpp-masterclass"
#define MyAppExeName "GridGuard.exe"

[Setup]
AppId={{5E36A792-F1BC-4E23-9B6A-9D8D279E53A1}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
DefaultDirName={autopf}\GridGuard N-2
DisableProgramGroupPage=yes
OutputDir=dist
OutputBaseFilename=GridGuard-N2-Windows-Setup-v2.1
SetupIconFile=assets\AppIcon.ico
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
Source: "dist\GridGuard\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; IconFilename: "{app}\assets\AppIcon.ico"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; IconFilename: "{app}\assets\AppIcon.ico"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent
