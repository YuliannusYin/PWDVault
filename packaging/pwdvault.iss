; ============================================================================
; PwdVault - Inno Setup 安装脚本
;
; 用途：将构建产物（pwdvault-ui.exe / pwdvault-service.exe / Qt 运行时 DLL /
;       插件目录 / LICENSE）打包为 Windows 安装程序（setup.exe）。
;
; 调用方式（在项目根目录执行）：
;   cmake --build build --config Release
;   cmake --build build --target package_inno
;   或直接：
;   iscc.exe packaging\pwdvault.iss
;
; 输出位置：build\package\pwdvault-<version>-setup.exe
;
; 注意：本脚本采用相对路径，运行时 WorkingDirectory 必须为项目根目录。
;       build 目录名固定为 "build"（与 CMake 配置示例一致）。
; ============================================================================

#define MyAppName "PwdVault"
#define MyAppVersion "3.0.0"
#define MyAppPublisher "PwdVault Project"
#define MyAppExeName "pwdvault-ui.exe"
#define MyAppServiceName "pwdvault-service.exe"

[Setup]
; AppId 在升级时用于识别同一应用（保持稳定，勿随意修改）
AppId={{8A7B6C5D-4E3F-2A1B-9C8D-7E6F5A4B3C2D}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
AllowNoIcons=yes
OutputDir=..\build\package
OutputBaseFilename=pwdvault-{#MyAppVersion}-setup
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
WizardStyle=modern
UninstallDisplayIcon={app}\bin\{#MyAppExeName}
; 卸载时同时清理用户数据目录需用户确认，默认不删除以保护数据
; 如需卸载时清理 %APPDATA%\PwdVault，请在 [UninstallDelete] 段添加条目

[Languages]
Name: "chinesesimp"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
; {cm:LaunchApp} 用于 [Run] 段 postinstall 复选框文本，%1 替换为应用名。
; Inno Setup 不内置此消息，需在此显式定义，否则 iscc 报错。
LaunchApp=运行 %1

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
; 主可执行程序
Source: "..\build\bin\Release\*.exe"; DestDir: "{app}\bin"; Flags: ignoreversion
; Qt 运行时 DLL（由 windeployqt 部署）
Source: "..\build\bin\Release\*.dll"; DestDir: "{app}\bin"; Flags: ignoreversion
; Qt 插件目录（platforms / styles / imageformats / tls 等）
; skipifsourcedoesntexist：若某插件目录在构建产物中缺失（如 Qt 版本未启用 TLS）
; 则跳过该项而不报错，保证打包流程对 Qt 子集差异具备鲁棒性。
Source: "..\build\bin\Release\platforms\*"; DestDir: "{app}\bin\platforms"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
Source: "..\build\bin\Release\styles\*"; DestDir: "{app}\bin\styles"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
Source: "..\build\bin\Release\imageformats\*"; DestDir: "{app}\bin\imageformats"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
Source: "..\build\bin\Release\tls\*"; DestDir: "{app}\bin\tls"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
Source: "..\build\bin\Release\iconengines\*"; DestDir: "{app}\bin\iconengines"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
Source: "..\build\bin\Release\networkinformation\*"; DestDir: "{app}\bin\networkinformation"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
; LICENSE 文件
Source: "..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\bin\{#MyAppExeName}"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\bin\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\bin\{#MyAppExeName}"; Description: "{cm:LaunchApp,{#MyAppName}}"; Flags: nowait postinstall skipifsilent

[UninstallRun]
; 卸载时停止服务进程（避免文件占用导致卸载失败）
Filename: "{cmd}"; Parameters: "/C taskkill /IM {#MyAppServiceName} /F"; Flags: runhidden; RunOnceId: "StopService"
Filename: "{cmd}"; Parameters: "/C taskkill /IM {#MyAppExeName} /F"; Flags: runhidden; RunOnceId: "StopUI"
