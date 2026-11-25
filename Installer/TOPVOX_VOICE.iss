; Installateur de TOPVOX VOICE (Windows 10 / 11, 64 bits)
; Installe le plugin VST3 dans le dossier standard lu par Cubase, FL Studio, Ableton Live, etc.

[Setup]
AppId={{6F3B8C52-4D0E-4B77-9A1C-7E2D5B9A41C8}
AppName=TOPVOX VOICE
AppVersion=1.0.0
AppPublisher=TOP
DefaultDirName={autopf}\TOPVOX VOICE
DefaultGroupName=TOPVOX VOICE
OutputDir=..\dist
OutputBaseFilename=TOPVOX_VOICE_SETUP
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName=TOPVOX VOICE
DisableProgramGroupPage=yes

[Languages]
Name: "french"; MessagesFile: "compiler:Languages\French.isl"

[Tasks]
Name: "desktopicon"; Description: "Créer un raccourci sur le Bureau (version autonome)"; GroupDescription: "Raccourcis :"

[Files]
; Plugin VST3 -> C:\Program Files\Common Files\VST3
Source: "..\build\TOPVOX_VOICE_artefacts\Release\VST3\TOPVOX VOICE.vst3\*"; DestDir: "{commoncf64}\VST3\TOPVOX VOICE.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs
; Version autonome (sans Cubase)
Source: "..\build\TOPVOX_VOICE_artefacts\Release\Standalone\TOPVOX VOICE.exe"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\TOPVOX VOICE (autonome)"; Filename: "{app}\TOPVOX VOICE.exe"
Name: "{group}\Désinstaller TOPVOX VOICE"; Filename: "{uninstallexe}"
Name: "{autodesktop}\TOPVOX VOICE"; Filename: "{app}\TOPVOX VOICE.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\TOPVOX VOICE.exe"; Description: "Lancer TOPVOX VOICE (version autonome)"; Flags: nowait postinstall skipifsilent unchecked
