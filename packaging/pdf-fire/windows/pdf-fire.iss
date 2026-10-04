; PDF Fire - Windows installer (Inno Setup 6).
; Built by .github/workflows/pdf-fire-windows.yml:
;   ISCC /DAppVersion=0.1.4 /DSourceDir=<program folder> /O<output folder> pdf-fire.iss

#ifndef AppVersion
  #define AppVersion "0.1.4"
#endif
#ifndef SourceDir
  #define SourceDir "..\..\..\build\install\usr\bin"
#endif

[Setup]
; The AppId identifies PDF Fire for updates and the uninstaller - never change it
AppId={{12926F50-9244-4928-966D-8BF3882A5B0A}
AppName=PDF Fire
AppVersion={#AppVersion}
AppVerName=PDF Fire {#AppVersion}
AppPublisher=Firehouse 360
AppPublisherURL=https://firehouse360.com/tools/pdf-fire
AppSupportURL=https://github.com/firehouse360/PDF-Fire
AppUpdatesURL=https://firehouse360.com/tools/pdf-fire
AppCopyright=MIT License - PDF Fire contributors, Jakub Melka and contributors (PDF4QT)
VersionInfoVersion={#AppVersion}
VersionInfoProductName=PDF Fire
VersionInfoCompany=Firehouse 360
DefaultDirName={autopf}\PDF Fire
DefaultGroupName=PDF Fire
DisableProgramGroupPage=yes
; Everyone on the computer by default; the person installing can choose "only for me" (no administrator needed)
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
LicenseFile={#SourceDir}\LICENSE.txt
SetupIconFile=..\..\..\Pdf4QtEditor\app-icon.ico
UninstallDisplayIcon={app}\Pdf4QtEditor.exe
UninstallDisplayName=PDF Fire
OutputBaseFilename=PDF-Fire-Setup-{#AppVersion}-x64
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ChangesAssociations=yes
CloseApplications=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"
Name: "openwith"; Description: "List PDF Fire under ""Open with"" for PDF files"; GroupDescription: "PDF files:"

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\PDF Fire"; Filename: "{app}\Pdf4QtEditor.exe"; Comment: "Edit, fill, sign and protect PDF files"
Name: "{autodesktop}\PDF Fire"; Filename: "{app}\Pdf4QtEditor.exe"; Tasks: desktopicon

[Registry]
; "Open with" for .pdf - it does not take over the default PDF program (Windows asks the person)
Root: HKA; Subkey: "Software\Classes\PDFFire.Document"; ValueType: string; ValueName: ""; ValueData: "PDF Document"; Flags: uninsdeletekey; Tasks: openwith
Root: HKA; Subkey: "Software\Classes\PDFFire.Document\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: """{app}\Pdf4QtEditor.exe"",0"; Tasks: openwith
Root: HKA; Subkey: "Software\Classes\PDFFire.Document\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\Pdf4QtEditor.exe"" ""%1"""; Tasks: openwith
Root: HKA; Subkey: "Software\Classes\.pdf\OpenWithProgids"; ValueType: none; ValueName: "PDFFire.Document"; Flags: uninsdeletevalue; Tasks: openwith
Root: HKA; Subkey: "Software\Classes\Applications\Pdf4QtEditor.exe"; ValueType: string; ValueName: "FriendlyAppName"; ValueData: "PDF Fire"; Flags: uninsdeletekey; Tasks: openwith

[Run]
Filename: "{app}\Pdf4QtEditor.exe"; Description: "{cm:LaunchProgram,PDF Fire}"; Flags: nowait postinstall skipifsilent
