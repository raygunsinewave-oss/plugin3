; Inno Setup script for Mystery Box (Windows x64).
;   iscc /DVersion=0.3.0 /DArtefacts=build\MysteryBox_artefacts\Release packaging\windows\MysteryBox.iss
#ifndef Version
  #define Version "0.0.0"
#endif
#ifndef Artefacts
  #define Artefacts "..\..\build\MysteryBox_artefacts\Release"
#endif

[Setup]
AppId={{6C1E0F3A-8D2B-4E57-9A61-2B7F4D0C9E11}
AppName=Mystery Box
AppVersion={#Version}
AppPublisher=Raygun
DefaultDirName={autopf}\Raygun\Mystery Box
DefaultGroupName=Raygun
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputBaseFilename=MysteryBox-{#Version}-Windows-Setup
OutputDir=.
Compression=lzma2
SolidCompression=yes
DisableProgramGroupPage=yes
WizardStyle=modern

[Types]
Name: "full"; Description: "Full installation"
Name: "custom"; Description: "Custom"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 plug-in"; Types: full custom; Flags: fixed
Name: "standalone"; Description: "Standalone application"; Types: full

[Files]
Source: "{#Artefacts}\VST3\Mystery Box.vst3\*"; DestDir: "{commoncf64}\VST3\Mystery Box.vst3"; Components: vst3; Flags: recursesubdirs ignoreversion
Source: "{#Artefacts}\Standalone\Mystery Box.exe"; DestDir: "{app}"; Components: standalone; Flags: ignoreversion

[Icons]
Name: "{group}\Mystery Box"; Filename: "{app}\Mystery Box.exe"; Components: standalone
