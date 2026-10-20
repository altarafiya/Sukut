; Rebuild through scripts/build-installer.ps1: validates the latest Android bundle.
#ifndef AppVersion
  #define AppVersion "0.2.33"
#endif
#ifndef AndroidVersion
  #define AndroidVersion "0.3.31"
#endif
#ifndef PayloadDir
  #define PayloadDir "..\dist\Sukut-" + AppVersion + "-win-x64"
#endif
#ifndef SetupOutputDir
  #define SetupOutputDir "..\dist\installer-final"
#endif
#ifndef DependencyRoot
  #define DependencyRoot "..\deps"
#endif
#define DriverDir DependencyRoot + "\installer\vbcable45"
#define DriverSetupHash "734c35dfa6d98f48782a451633ceb471166ec70d60482fd89a1123d0ee3c4f41"

[Setup]
AppId={{2FAED6FA-C059-4F61-A83D-449A12B6B13C}
AppName=Sukut
AppVersion={#AppVersion}
AppVerName=Sukut {#AppVersion}
AppPublisher=الطرفية
DefaultDirName={localappdata}\Programs\Sukut
DefaultGroupName=Sukut
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64os
ArchitecturesInstallIn64BitMode=x64os
MinVersion=10.0.19041
WizardStyle=modern dynamic windows11 hidebevels
WizardSizePercent=115,115
WizardImageFile=Assets\QuietWave-Light.png
WizardImageFileDynamicDark=Assets\QuietWave.png
WizardSmallImageFile=..\src\sukut-ui\Assets\Sukut.png
SetupIconFile=..\src\sukut-ui\Assets\Sukut.ico
UninstallDisplayIcon={app}\app\Sukut.exe
UninstallDisplayName=Sukut
CloseApplications=no
RestartApplications=no
DisableWelcomePage=no
DisableReadyPage=no
DisableDirPage=auto
AlwaysShowDirOnReadyPage=no
AlwaysShowGroupOnReadyPage=no
LanguageDetectionMethod=uilanguage
ShowLanguageDialog=yes
LicenseFile={#DriverDir}\readme.txt
OutputDir={#SetupOutputDir}
OutputBaseFilename=Sukut-{#AppVersion}-Setup
Compression=lzma2/max
SolidCompression=yes
LZMANumBlockThreads=2
SetupLogging=yes
VersionInfoVersion={#AppVersion}.0
VersionInfoDescription=Sukut setup
VersionInfoCompany=الطرفية

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "arabic"; MessagesFile: "compiler:Languages\Arabic.isl,Languages\Arabic-Sukut.isl"

[LangOptions]
DialogFontName=Segoe UI
DialogFontSize=10
arabic.DialogFontSize=13

[CustomMessages]
english.TaglineFirst=Enjoy the internet.
english.TaglineSecond=Without the music.
english.WelcomeBody=Install Sukut for Windows. Your Android companion is included, ready for USB setup.
english.DriverTitle=Your audio connection
english.DriverSubtitle=Sukut uses VB-CABLE to receive audio from your apps.
english.DriverFound=VB-CABLE is already installed
english.DriverMissing=VB-CABLE will be installed
english.DriverFoundBody=Your existing driver and its settings will be kept.
english.DriverMissingBody=Windows will ask for administrator approval. Restart the PC afterward to finish setting up the driver.
english.DriverCredit=VB-CABLE is made by VB-Audio. It is donationware; all contributions are welcome.
english.DriverWebsite=VB-CABLE website
english.DriverDonate=Support VB-Audio
english.DriverInstalling=Installing VB-CABLE. Approve the Windows prompt if it appears.
english.DriverError=VB-CABLE could not be installed. Retry, or install it from vb-cable.com and run this installer again.
english.DriverRestart=Restart Windows before using audio processing.
english.Companion=Android companion included: Sukut {#AndroidVersion}
english.InstallSummary=Install Sukut and its Android companion
english.CloseSukut=Please choose Exit from Sukut's tray menu, then try again. Your audio settings will be kept.
english.DesktopIcon=Create a desktop shortcut
english.Launch=Open Sukut
english.CpuRequired=This Sukut build needs an x64 processor with AVX support.
english.PreviewOnly=This is a preview run. No application or driver was installed.
english.DevelopedBy=Developed by
arabic.TaglineFirst=إنترنت
arabic.TaglineSecond=بدون موسيقى.
arabic.WelcomeBody=تثبيت سُكوت على حاسوبك، مع نسخة الهاتف.
arabic.DriverTitle=توصيل الصوت
arabic.DriverSubtitle=يستقبل سُكوت صوت تطبيقاتك عبر VB-CABLE.
arabic.DriverFound=VB-CABLE مثبت بالفعل
arabic.DriverMissing=سيتم تثبيت VB-CABLE
arabic.DriverFoundBody=سنبقي برنامج التشغيل الحالي وإعداداته كما هي.
arabic.DriverMissingBody=سيطلب Windows إذن المسؤول. أعد تشغيل الحاسوب بعد التثبيت لإكمال الإعداد.
arabic.DriverCredit=VB-CABLE من تطوير VB-Audio. برنامج قائم على التبرع؛ مساهمتك موضع ترحيب.
arabic.DriverWebsite=موقع VB-CABLE
arabic.DriverDonate=دعم VB-Audio
arabic.DriverInstalling=يتم تثبيت VB-CABLE. وافق على طلب Windows إذا ظهر.
arabic.DriverError=تعذر تثبيت VB-CABLE. أعد المحاولة، أو ثبته من vb-cable.com ثم شغل المثبت مجددا.
arabic.DriverRestart=أعد تشغيل Windows قبل استخدام معالجة الصوت.
arabic.Companion=نسخة الهاتف: {#AndroidVersion}
arabic.InstallSummary=تثبيت سُكوت ونسخة Android المرفقة
arabic.CloseSukut=اختر «خروج» من قائمة سُكوت في منطقة الإشعارات، ثم أعد المحاولة. ستبقى إعدادات الصوت محفوظة.
arabic.DesktopIcon=إنشاء اختصار على سطح المكتب
arabic.Launch=فتح سُكوت
arabic.CpuRequired=تحتاج هذه النسخة إلى معالج x64 يدعم AVX.
arabic.PreviewOnly=هذه معاينة فقط. لم يثبت أي برنامج أو برنامج تشغيل.
arabic.DevelopedBy=تطوير:

[Tasks]
Name: "desktopicon"; Description: "{cm:DesktopIcon}"

[Files]
; Keep the unchanged vendor bundle first: early extraction never decompresses
; the complete app, and every companion INF/catalog/SYS remains beside setup.
Source: "{#DriverDir}\*"; DestDir: "{tmp}\Sukut-VB-CABLE"; Flags: dontcopy
Source: "..\src\sukut-ui\Assets\IBMPlexSansArabic-Regular.ttf"; Flags: dontcopy
Source: "..\src\sukut-ui\Assets\IBMPlexSansArabic-SemiBold.ttf"; Flags: dontcopy
Source: "Assets\SukutHandjet.ttf"; Flags: dontcopy
Source: "{#PayloadDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs; Excludes: "*.pdb,*.log,runtime\*"

[Icons]
Name: "{autoprograms}\Sukut"; Filename: "{app}\Sukut.exe"; WorkingDir: "{app}"; IconFilename: "{app}\app\Sukut.exe"
Name: "{autodesktop}\Sukut"; Filename: "{app}\Sukut.exe"; WorkingDir: "{app}"; IconFilename: "{app}\app\Sukut.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\Sukut.exe"; Description: "{cm:Launch}"; Flags: postinstall nowait skipifsilent runasoriginaluser; Check: CanLaunch

[Code]
var
  DriverPage: TWizardPage;
  DriverHeading, DriverBody, DriverCredit: TNewStaticText;
  InstalledDriverThisRun: Boolean;
  ArabicFontLoaded, ArabicBoldLoaded, BrandFontLoaded: Boolean;
  DeveloperCaption, DeveloperBrand: TNewStaticText;
  PrayerLabel: TNewStaticText;
  SparkleOne, SparkleTwo: TNewStaticText;

function AddFontResourceEx(FileName: String; Flags: Cardinal; Reserved: NativeUInt): Integer;
  external 'AddFontResourceExW@gdi32.dll stdcall';
function RemoveFontResourceEx(FileName: String; Flags: Cardinal; Reserved: NativeUInt): Boolean;
  external 'RemoveFontResourceExW@gdi32.dll stdcall';

function IsProcessorFeaturePresent(Feature: Cardinal): Boolean;
  external 'IsProcessorFeaturePresent@kernel32.dll stdcall';

function DriverInstalled: Boolean;
begin
  { The public hardware ID is VBAudioVACWDM; the INF service is VBAudioVACMME.
    Service detection also covers disabled endpoints and renamed device labels. }
  Result := RegKeyExists(HKLM64, 'SYSTEM\CurrentControlSet\Services\VBAudioVACMME');
end;

function DriverSetup: String;
begin
  Result := ExpandConstant('{tmp}\Sukut-VB-CABLE\VBCABLE_Setup_x64.exe');
end;

procedure ExtractVerifiedDriver;
begin
  ExtractTemporaryFiles('{tmp}\Sukut-VB-CABLE\*');
  if CompareText(GetSHA256OfFile(DriverSetup), '{#DriverSetupHash}') <> 0 then
    RaiseException('VB-CABLE package verification failed.');
end;

function InitializeSetup: Boolean;
var
  Report, DriverState: String;
begin
  Result := True;
  Report := ExpandConstant('{param:SELFTEST|}');
  if Report <> '' then begin
    { Read-only dependency test: no app installation, elevation or driver run. }
    ExtractVerifiedDriver;
    if DriverInstalled then DriverState := 'true' else DriverState := 'false';
    SaveStringToFile(Report, '{"selftest":"passed","driverInstalled":' + DriverState +
      ',"driverPayloadVerified":true,"driverExecuted":false,"appInstalled":false}', False);
    Result := False;
    Exit;
  end;
  if not IsProcessorFeaturePresent(39) then begin
    SuppressibleMsgBox(CustomMessage('CpuRequired'), mbError, MB_OK, IDOK);
    Result := False;
  end;
end;

procedure LinkClicked(Sender: TObject; const Link: String; LinkType: TSysLinkType);
var
  Code: Integer;
begin
  if LinkType = sltURL then
    ShellExecAsOriginalUser('open', Link, '', '', SW_SHOWNORMAL, ewNoWait, Code);
end;

function AddText(Top, Height: Integer; const Value: String; Bold: Boolean): TNewStaticText;
begin
  Result := TNewStaticText.Create(DriverPage);
  Result.Parent := DriverPage.Surface;
  Result.Left := ScaleX(12); Result.Top := ScaleY(Top);
  Result.Width := DriverPage.SurfaceWidth - ScaleX(24);
  Result.Height := ScaleY(Height); Result.AutoSize := False;
  Result.WordWrap := True; Result.Caption := Value;
  if Bold then Result.Font.Style := [fsBold];
end;

procedure InitializeWizard;
var
  Link: TNewLinkLabel;
begin
  { Private process fonts: no system font installation or broadcast. }
  ExtractTemporaryFile('IBMPlexSansArabic-Regular.ttf');
  ExtractTemporaryFile('IBMPlexSansArabic-SemiBold.ttf');
  ExtractTemporaryFile('SukutHandjet.ttf');
  ArabicFontLoaded := AddFontResourceEx(ExpandConstant('{tmp}\IBMPlexSansArabic-Regular.ttf'), 16, 0) > 0;
  ArabicBoldLoaded := AddFontResourceEx(ExpandConstant('{tmp}\IBMPlexSansArabic-SemiBold.ttf'), 16, 0) > 0;
  BrandFontLoaded := AddFontResourceEx(ExpandConstant('{tmp}\SukutHandjet.ttf'), 16, 0) > 0;
  Log(Format('Private Arabic font: %d; brand font: %d', [Ord(ArabicFontLoaded), Ord(BrandFontLoaded)]));
  if (ActiveLanguage = 'arabic') and ArabicFontLoaded then begin
    WizardForm.Font.Name := 'IBM Plex Sans Arabic';
    WizardForm.WelcomeLabel1.Font.Name := 'IBM Plex Sans Arabic';
    WizardForm.WelcomeLabel2.Font.Name := 'IBM Plex Sans Arabic';
    WizardForm.FinishedHeadingLabel.Font.Name := 'IBM Plex Sans Arabic';
  end;
  WizardForm.WelcomeLabel1.Caption := CustomMessage('TaglineFirst') + #13#10 + CustomMessage('TaglineSecond');
  if ActiveLanguage = 'arabic' then WizardForm.WelcomeLabel1.Font.Size := 26
  else WizardForm.WelcomeLabel1.Font.Size := 22;
  WizardForm.WelcomeLabel1.StyleElements := WizardForm.WelcomeLabel1.StyleElements - [seFont];
  WizardForm.WelcomeLabel1.Font.Color := $00337BAE;
  WizardForm.WelcomeLabel1.Height := ScaleY(100);
  WizardForm.WelcomeLabel2.Top := WizardForm.WelcomeLabel1.Top + WizardForm.WelcomeLabel1.Height + ScaleY(16);
  WizardForm.WelcomeLabel2.Height := ScaleY(120);
  WizardForm.WelcomeLabel2.Caption := CustomMessage('WelcomeBody') + #13#10#13#10 + CustomMessage('Companion');
  DeveloperCaption := TNewStaticText.Create(WizardForm);
  DeveloperCaption.Parent := WizardForm.WelcomePage;
  DeveloperCaption.AutoSize := False;
  DeveloperCaption.SetBounds(WizardForm.WelcomeLabel1.Left,
    WizardForm.WelcomePage.ClientHeight - ScaleY(110), WizardForm.WelcomeLabel1.Width, ScaleY(28));
  DeveloperCaption.Caption := CustomMessage('DevelopedBy');
  DeveloperBrand := TNewStaticText.Create(WizardForm);
  DeveloperBrand.Parent := WizardForm.WelcomePage;
  DeveloperBrand.AutoSize := False;
  DeveloperBrand.SetBounds(DeveloperCaption.Left, DeveloperCaption.Top + ScaleY(27), DeveloperCaption.Width, ScaleY(68));
  DeveloperBrand.Caption := 'الطرفية';
  DeveloperBrand.Font.Size := 28;
  DeveloperBrand.Font.Style := [];
  DeveloperBrand.StyleElements := DeveloperBrand.StyleElements - [seFont];
  DeveloperBrand.Font.Color := $007B9818;
  if BrandFontLoaded then DeveloperBrand.Font.Name := 'Sukut Handjet'
  else if ArabicFontLoaded then DeveloperBrand.Font.Name := 'IBM Plex Sans Arabic';
  { Separate ornaments never split the Arabic word or alter its shaping. }
  SparkleOne := TNewStaticText.Create(WizardForm);
  SparkleOne.Parent := WizardForm.WelcomePage;
  SparkleOne.AutoSize := False;
  SparkleOne.SetBounds(DeveloperBrand.Left + ScaleX(142), DeveloperBrand.Top + ScaleY(2), ScaleX(32), ScaleY(36));
  SparkleOne.Caption := '✦'; SparkleOne.Font.Name := 'Segoe UI Symbol'; SparkleOne.Font.Size := 18;
  SparkleOne.StyleElements := SparkleOne.StyleElements - [seFont];
  SparkleOne.Font.Color := $00CF7390;
  SparkleTwo := TNewStaticText.Create(WizardForm);
  SparkleTwo.Parent := WizardForm.WelcomePage;
  SparkleTwo.AutoSize := False;
  SparkleTwo.SetBounds(DeveloperBrand.Left + ScaleX(172), DeveloperBrand.Top + ScaleY(31), ScaleX(24), ScaleY(26));
  SparkleTwo.Caption := '✧'; SparkleTwo.Font.Name := 'Segoe UI Symbol'; SparkleTwo.Font.Size := 12;
  SparkleTwo.StyleElements := SparkleTwo.StyleElements - [seFont];
  SparkleTwo.Font.Color := $00337BAE;
  { Inno mirrors controls after InitializeWizard. Keep their initial alignment
    left so the native RTL pass turns it right exactly once. }
  PrayerLabel := TNewStaticText.Create(WizardForm);
  PrayerLabel.Parent := WizardForm.FinishedPage;
  PrayerLabel.AutoSize := False;
  PrayerLabel.SetBounds(WizardForm.FinishedLabel.Left,
    WizardForm.FinishedPage.ClientHeight - ScaleY(100), WizardForm.FinishedLabel.Width, ScaleY(84));
  PrayerLabel.WordWrap := True;
  PrayerLabel.Alignment := taCenter;
  PrayerLabel.Font.Name := 'IBM Plex Sans Arabic';
  PrayerLabel.Font.Size := 15;
  if not HighContrastActive then begin
    PrayerLabel.StyleElements := PrayerLabel.StyleElements - [seFont];
    if IsDarkInstallMode then PrayerLabel.Font.Color := $008AC8E8
    else PrayerLabel.Font.Color := $00336688;
  end;
  PrayerLabel.Caption := 'ادعوا لخالي بالرَّحمة، ولزوجته بالشِّفاء،' + #13#10 + 'ولجميع المسلمين.';
  DriverPage := CreateCustomPage(wpWelcome, CustomMessage('DriverTitle'), CustomMessage('DriverSubtitle'));
  DriverHeading := AddText(18, 48, '', True);
  if ActiveLanguage = 'arabic' then DriverHeading.Font.Size := 18
  else DriverHeading.Font.Size := 16;
  DriverBody := AddText(78, 86, '', False);
  DriverCredit := AddText(178, 60, CustomMessage('DriverCredit'), False);
  Link := TNewLinkLabel.Create(DriverPage);
  Link.Parent := DriverPage.Surface;
  Link.Left := ScaleX(12); Link.Top := ScaleY(248);
  Link.Width := DriverPage.SurfaceWidth - ScaleX(24); Link.Height := ScaleY(36);
  Link.Caption := '<a href="https://vb-audio.com/Cable/">' + CustomMessage('DriverWebsite') + '</a>   ' +
    '<a href="https://shop.vb-audio.com/en/win-apps/11-vb-cable.html">' + CustomMessage('DriverDonate') + '</a>';
  Link.OnLinkClick := @LinkClicked;
end;

procedure DeinitializeSetup;
begin
  if BrandFontLoaded then RemoveFontResourceEx(ExpandConstant('{tmp}\SukutHandjet.ttf'), 16, 0);
  if ArabicBoldLoaded then RemoveFontResourceEx(ExpandConstant('{tmp}\IBMPlexSansArabic-SemiBold.ttf'), 16, 0);
  if ArabicFontLoaded then RemoveFontResourceEx(ExpandConstant('{tmp}\IBMPlexSansArabic-Regular.ttf'), 16, 0);
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  if CurPageID = DriverPage.ID then begin
    if DriverInstalled then begin
      DriverHeading.Caption := CustomMessage('DriverFound');
      DriverBody.Caption := CustomMessage('DriverFoundBody');
    end else begin
      DriverHeading.Caption := CustomMessage('DriverMissing');
      DriverBody.Caption := CustomMessage('DriverMissingBody');
    end;
  end;
  if CurPageID = wpFinished then begin
    if InstalledDriverThisRun then
      WizardForm.FinishedLabel.Caption := CustomMessage('DriverRestart');
    { Reserve a separate closing area below the native launch/restart choices. }
    PrayerLabel.Top := WizardForm.FinishedPage.ClientHeight - ScaleY(100);
    WizardForm.RunList.Height := PrayerLabel.Top - WizardForm.RunList.Top - ScaleY(12);
  end;
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  { Accept the original vendor terms only when installing its driver. }
  Result := (PageID = wpLicense) and DriverInstalled;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  Code: Integer;
begin
  Result := '';
  if ExpandConstant('{param:PREVIEW|0}') = '1' then begin
    Result := CustomMessage('PreviewOnly'); Exit;
  end;
  if CheckForMutexes('Local\SukutWinUI') then begin
    Result := CustomMessage('CloseSukut'); Exit;
  end;
  if DriverInstalled then begin
    Log('VB-CABLE already installed; preserving the driver and its settings.');
    Exit;
  end;
  ExtractVerifiedDriver;
  WizardForm.PreparingLabel.Caption := CustomMessage('DriverInstalling');
  { Elevate only the original signed vendor installer. Never import certificates,
    disable security prompts, silently uninstall, or force a Windows restart. }
  if not ShellExec('runas', DriverSetup, '-i -h', ExtractFileDir(DriverSetup), SW_SHOWNORMAL, ewWaitUntilTerminated, Code) then begin
    Result := CustomMessage('DriverError') + #13#10 + SysErrorMessage(Code); Exit;
  end;
  Log(Format('VB-CABLE installer returned %d', [Code]));
  if not DriverInstalled then begin
    Result := CustomMessage('DriverError'); Exit;
  end;
  InstalledDriverThisRun := True;
end;

function NeedRestart: Boolean;
begin
  Result := InstalledDriverThisRun;
end;

function CanLaunch: Boolean;
begin
  Result := not InstalledDriverThisRun;
end;

function InitializeUninstall: Boolean;
begin
  Result := not CheckForMutexes('Local\SukutWinUI');
  if not Result then SuppressibleMsgBox(CustomMessage('CloseSukut'), mbInformation, MB_OK, IDOK);
end;
