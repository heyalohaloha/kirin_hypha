#ifndef AppVersion
  #error AppVersion is required
#endif
#ifndef OutputDir
  #error OutputDir is required
#endif
#ifndef PreBundle
  #error PreBundle is required
#endif
#ifndef PostBundle
  #error PostBundle is required
#endif
#ifdef WithAax
  #ifndef PreAaxBundle
    #error PreAaxBundle is required with AAX
  #endif
  #ifndef PostAaxBundle
    #error PostAaxBundle is required with AAX
  #endif
#endif

[Setup]
AppId={{B3301A1C-4782-4E36-9B4D-89CC6C5B701E}
AppName=Kirin Hypha
AppVersion={#AppVersion}
AppPublisher=Kirin Mastering
AppPublisherURL=https://kirinmastering.com/hypha
AppSupportURL=https://github.com/heyalohaloha/kirin_hypha/issues
AppUpdatesURL=https://github.com/heyalohaloha/kirin_hypha/releases
VersionInfoVersion={#AppVersion}
VersionInfoCompany=Kirin Mastering
#ifdef WithAax
VersionInfoDescription=Kirin Hypha Windows VST3 + AAX Installer
#else
VersionInfoDescription=Kirin Hypha Windows VST3 Installer
#endif
VersionInfoProductName=Kirin Hypha
DefaultDirName={autocf}\VST3
CreateAppDir=no
UninstallFilesDir={autopf}\Kirin Mastering\Kirin Hypha
UninstallDisplayName=Kirin Hypha
UninstallDisplayIcon={autocf}\VST3\Kirin Hypha POST.vst3\Contents\x86_64-win\Kirin Hypha POST.vst3
#ifdef WithAax
PrivilegesRequired=admin
#else
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=commandline dialog
#endif
UsePreviousPrivileges=yes
ArchitecturesAllowed=x64os
ArchitecturesInstallIn64BitMode=x64os
DisableDirPage=yes
DisableProgramGroupPage=yes
WizardStyle=modern
CloseApplications=yes
CloseApplicationsFilter=*.*
RestartApplications=no
OutputDir={#OutputDir}
OutputBaseFilename=Kirin-Hypha-{#AppVersion}-Windows-x64-Setup
Compression=lzma2
SolidCompression=yes
#ifdef SignedBuild
SignTool=kirin_esigner
SignedUninstaller=yes
#else
SignedUninstaller=no
#endif

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "japanese"; MessagesFile: "compiler:Languages\Japanese.isl"

[InstallDelete]
Type: filesandordirs; Name: "{autocf}\VST3\Kirin Hypha PRE.vst3"
Type: filesandordirs; Name: "{autocf}\VST3\Kirin Hypha POST.vst3"
#ifdef WithAax
Type: filesandordirs; Name: "{commoncf}\Avid\Audio\Plug-Ins\Kirin Hypha PRE.aaxplugin"
Type: filesandordirs; Name: "{commoncf}\Avid\Audio\Plug-Ins\Kirin Hypha POST.aaxplugin"
#endif

[Files]
Source: "{#PreBundle}\*"; DestDir: "{autocf}\VST3\Kirin Hypha PRE.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#PostBundle}\*"; DestDir: "{autocf}\VST3\Kirin Hypha POST.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs
#ifdef WithAax
Source: "{#PreAaxBundle}\*"; DestDir: "{commoncf}\Avid\Audio\Plug-Ins\Kirin Hypha PRE.aaxplugin"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#PostAaxBundle}\*"; DestDir: "{commoncf}\Avid\Audio\Plug-Ins\Kirin Hypha POST.aaxplugin"; Flags: ignoreversion recursesubdirs createallsubdirs
#endif
