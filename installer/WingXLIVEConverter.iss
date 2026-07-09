[Setup]
AppId={{8CF6A3A2-B0B1-4C77-A89F-4E1A9A6CB9A5}
AppName=Wing X-LIVE Converter
AppVersion=1.0.0
AppPublisher=Wing X-LIVE Tools
DefaultDirName={localappdata}\WingXLIVEConverter
DefaultGroupName=Wing X-LIVE Converter
DisableProgramGroupPage=yes
OutputBaseFilename=WingXLIVEConverterSetup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=lowest
ArchitecturesInstallIn64BitMode=x64compatible

[Files]
Source: "..\dist\WingXLIVEConverter.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\README_XLIVE_CONVERTER.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\dist\ffmpeg.exe"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\dist\FFMPEG_LICENSE.txt"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist

[Icons]
Name: "{autoprograms}\Wing X-LIVE Converter"; Filename: "{app}\WingXLIVEConverter.exe"
Name: "{autodesktop}\Wing X-LIVE Converter"; Filename: "{app}\WingXLIVEConverter.exe"

[Run]
Filename: "{app}\WingXLIVEConverter.exe"; Description: "Launch Wing X-LIVE Converter"; Flags: nowait postinstall skipifsilent
