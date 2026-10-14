#define AppVersion "0.1.0-preview.1"
[Setup]
AppId={{D9DB5BBB-F610-499F-B6EC-06CBED1E526C}
AppName=Spatial ASIO
AppVersion={#AppVersion}
AppPublisher=Spatial ASIO contributors
AppPublisherURL=https://github.com/hikari-taritsu/spatial-asio
AppSupportURL=https://github.com/hikari-taritsu/spatial-asio/issues
DefaultDirName={localappdata}\Programs\Spatial ASIO
DefaultGroupName=Spatial ASIO
PrivilegesRequired=lowest
ArchitecturesAllowed=x64os
ArchitecturesInstallIn64BitMode=x64os
MinVersion=10.0.19041
LicenseFile=..\LICENSE
InfoBeforeFile=INSTALL-NOTES.zh-CN.txt
OutputDir=..\dist
OutputBaseFilename=SpatialASIO-0.1.0-preview.1-win-x64-Setup
Compression=lzma2/normal
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\SpatialASIO.exe
CloseApplications=yes
RestartApplications=no
SetupLogging=yes
[Languages]
Name: "zhCN"; MessagesFile: "languages\ChineseSimplified.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"
[Tasks]
Name: "desktopicon"; Description: "创建桌面快捷方式"; Flags: checkedonce
[Files]
Source: "..\dist\app\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs; Excludes: "*.pdb,*.pyc,__pycache__\*,app-checks.txt"
[Icons]
Name: "{group}\Spatial ASIO"; Filename: "{app}\SpatialASIO.exe"
Name: "{autodesktop}\Spatial ASIO"; Filename: "{app}\SpatialASIO.exe"; Tasks: desktopicon
Name: "{group}\卸载 Spatial ASIO"; Filename: "{uninstallexe}"
; No autostart: audio capture always begins with an explicit user action.
; User configuration under LocalAppData\SpatialASIO is preserved on uninstall.
