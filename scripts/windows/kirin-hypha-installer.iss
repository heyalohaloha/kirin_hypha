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
SignToolRetryCount=0
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
#ifdef LegalDir
Source: "{#LegalDir}\*"; DestDir: "{autopf}\Kirin Mastering\Kirin Hypha\Legal"; Flags: ignoreversion recursesubdirs createallsubdirs
#endif
Source: "{#PreBundle}\*"; DestDir: "{autocf}\VST3\Kirin Hypha PRE.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#PostBundle}\*"; DestDir: "{autocf}\VST3\Kirin Hypha POST.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs
#ifdef WithAax
Source: "{#PreAaxBundle}\*"; DestDir: "{commoncf}\Avid\Audio\Plug-Ins\Kirin Hypha PRE.aaxplugin"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#PostAaxBundle}\*"; DestDir: "{commoncf}\Avid\Audio\Plug-Ins\Kirin Hypha POST.aaxplugin"; Flags: ignoreversion recursesubdirs createallsubdirs
#endif

[CustomMessages]
english.HyphaMigrationReadyMemo=This Windows user also has an older Kirin Hypha installed only for this user. Setup removes that copy first so every DAW loads this version for all users. Your settings and DAW projects are kept.
japanese.HyphaMigrationReadyMemo=このWindowsユーザーには、このユーザー専用の古いKirin Hyphaも入っています。どのDAWでもこの版を読み込むように、先にその古い版を削除してから、すべてのユーザー用に入れます。設定とDAWのプロジェクトはそのまま残ります。
english.HyphaMigrationFailed=Setup could not remove all of the older Kirin Hypha installed only for this Windows user; part of it may already be removed. Close every DAW and plug-in scanner, then run Setup again to finish the update. The new version is not installed yet. Your settings and DAW projects were not changed.
japanese.HyphaMigrationFailed=このWindowsユーザー専用の古いKirin Hyphaを削除しきれませんでした。一部はすでに削除されていることがあります。すべてのDAWとプラグインのスキャンを終了してから、もう一度インストーラーを実行して更新を完了してください。新しい版はまだ入っていません。設定とDAWのプロジェクトは変更していません。
english.HyphaAllUsersPresent=Kirin Hypha is already installed for all users. Run Setup for all users (as administrator) so that only one copy exists.
japanese.HyphaAllUsersPresent=Kirin Hyphaはすでにすべてのユーザー用に入っています。コピーが1つだけになるよう、すべてのユーザー用（管理者）でインストーラーを実行してください。

[Code]
// One installation scope at a time. VST3 hosts search the per-user folder as well as the
// all-users folder, so an older current-user copy would keep loading after an all-users
// update. Only this user's exact PRE/POST bundles and this AppId's registration are touched;
// other accounts, diagnostics, other products, settings and DAW projects are never changed.
// The elevated Setup never runs anything from the user-writable profile (a replaced
// uninstaller there would run as administrator): it removes the two exact bundles itself and
// leaves the old per-user uninstaller files, which are no longer registered, untouched.
const
  HyphaUninstallKey = 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{B3301A1C-4782-4E36-9B4D-89CC6C5B701E}_is1';

function HyphaCurrentUserVst3Bundle(Role: String): String;
begin
  Result := ExpandConstant('{localappdata}\Programs\Common\VST3\Kirin Hypha ') + Role + '.vst3';
end;

function HyphaCurrentUserBundlesPresent: Boolean;
begin
  Result := DirExists(HyphaCurrentUserVst3Bundle('PRE')) or DirExists(HyphaCurrentUserVst3Bundle('POST'));
end;

function HyphaCurrentUserCopyFound: Boolean;
begin
  Result := RegKeyExists(HKEY_CURRENT_USER, HyphaUninstallKey) or HyphaCurrentUserBundlesPresent;
end;

// The registration goes only after both bundles are gone, so a refused update keeps the old
// entry and the remaining files can be removed by running Setup again.
function HyphaRemoveCurrentUserCopy: String;
var
  Role: String;
  Index: Integer;
begin
  Result := '';
  for Index := 0 to 1 do
  begin
    if Index = 0 then Role := 'PRE' else Role := 'POST';
    if DirExists(HyphaCurrentUserVst3Bundle(Role)) then
    begin
      Log('Kirin Hypha: removing the current-user bundle ' + HyphaCurrentUserVst3Bundle(Role));
      DelTree(HyphaCurrentUserVst3Bundle(Role), True, True, True);
    end;
  end;
  if HyphaCurrentUserBundlesPresent then
  begin
    Log('Kirin Hypha: a current-user bundle is still in use; nothing new was installed');
    Result := CustomMessage('HyphaMigrationFailed');
    Exit;
  end;
  if RegKeyExists(HKEY_CURRENT_USER, HyphaUninstallKey) then
  begin
    if not RegDeleteKeyIncludingSubkeys(HKEY_CURRENT_USER, HyphaUninstallKey) then
    begin
      Log('Kirin Hypha: the current-user registration could not be removed; nothing new was installed');
      Result := CustomMessage('HyphaMigrationFailed');
      Exit;
    end;
  end;
  Log('Kirin Hypha: the current-user installation was removed before the all-users installation');
end;

function HyphaAllUsersCopyFound: Boolean;
begin
  Result := RegKeyExists(HKEY_LOCAL_MACHINE, HyphaUninstallKey) or
    DirExists(ExpandConstant('{commoncf}\VST3\Kirin Hypha PRE.vst3')) or
    DirExists(ExpandConstant('{commoncf}\VST3\Kirin Hypha POST.vst3'));
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  if IsAdminInstallMode then
  begin
    if HyphaCurrentUserCopyFound then
      Result := HyphaRemoveCurrentUserCopy
    else
      Result := '';
  end
  else if HyphaAllUsersCopyFound then
  begin
    Log('Kirin Hypha: refusing a current-user installation beside the all-users installation');
    Result := CustomMessage('HyphaAllUsersPresent');
  end
  else
    Result := '';
end;

function UpdateReadyMemo(Space, NewLine, MemoUserInfoInfo, MemoDirInfo, MemoTypeInfo,
  MemoComponentsInfo, MemoGroupInfo, MemoTasksInfo: String): String;
begin
  if IsAdminInstallMode and HyphaCurrentUserCopyFound then
    Result := CustomMessage('HyphaMigrationReadyMemo')
  else
    Result := '';
end;
