using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Automation;

namespace Sukut;

public sealed partial class ShellView
{
    private bool _setupVisible, _setupEngineReady, _setupScanned, _setupRefreshing;
    private int _setupStep;
    private bool _skipCableStep;
    private string _lastSetupStatus = "";

    private void ShowSetup()
    {
        _setupVisible = true; _setupStep = 0; _skipCableStep = CableReady; StopBrand();
        bool previous = _applying; _applying = true;
        Navigation.Visibility = Visibility.Collapsed;
        View.HomeLoaded = View.SettingsLoaded = View.AboutLoaded = View.PhoneLoaded = false;
        View.SetupLoaded = true;
        _applying = previous;
    }
    private void HideSetup()
    {
        _setupVisible = false; View.SetupLoaded = false;
        Navigation.Visibility = Visibility.Visible; Navigate(0);
    }
    private void SetupPanel_Loaded(object sender, RoutedEventArgs args)
    {
        ApplySetupLanguage(); RenderSetupOutput(); RenderBufferingSettings(); RenderError();
    }
    private void ApplySetupLanguage()
    {
        if (SetupPanel == null) return;
        bool previous = _applying; _applying = true;
        SetupLanguageChoice.SelectedIndex = Arabic ? 1 : 0;
        SetupWelcomeTitle.Text = L("Welcome to Sukut", "أهلًا بك في سُكوت");
        SetupWelcomeDescription.Text = L("Enjoy the internet. Without the music.", "استمتع بالإنترنت، بلا موسيقى.");
        SetupOverviewCable.Text = L("Connect your apps with VB-CABLE", "وصّل صوت تطبيقاتك عبر VB-CABLE");
        SetupOverviewOutput.Text = L("Choose your speakers or headphones", "اختر السماعات أو مكبر الصوت");
        SetupOverviewVolume.Text = L("Link real output volume, then choose a mode", "اربط مستوى السماعات، ثم اختر وضعًا");
        SetupLanguageLabel.Text = L("Language", "اللغة");
        SetupCableTitle.Text = L("Set up VB-CABLE", "إعداد VB-CABLE");
        SetupCableDescription.Text = L("VB-CABLE is a virtual audio driver. It sends sound from your apps into Sukut for music removal, then Sukut plays the result through your speakers or headphones.", "VB-CABLE برنامج تشغيل صوت افتراضي. ينقل صوت تطبيقاتك إلى سُكوت لإزالة الموسيقى، ثم يشغّل سُكوت النتيجة عبر سماعاتك.");
        SetupInstallSteps.Text = L("1. Download VB-CABLE and extract the files.\n2. Run its setup program as administrator.\n3. Restart Windows and reopen Sukut.", "١. حمّل VB-CABLE وفك ضغط الملفات.\n٢. شغّل برنامج التثبيت كمسؤول.\n٣. أعد تشغيل ويندوز وافتح سُكوت.");
        SetupDownloadButton.Content = L("Open driver download website", "فتح موقع تحميل برنامج التشغيل");
        SetupRefreshButton.Content = L("Refresh devices", "تحديث الأجهزة");
        SetupVolumeOpenTitle.Text = L("1 · Open VB-CABLE as administrator", "١ · افتح VB-CABLE كمسؤول");
        SetupPanelScreenshot.Header = L("VB-CABLE control panel", "لوحة تحكم VB-CABLE");
        SetupOutputTitle.Text = L("Choose your audio output", "اختر مخرج الصوت");
        SetupOutputDescription.Text = L("Pick the speakers or headphones you listen through.", "اختر السماعات أو مكبر الصوت الذي تستمع من خلاله.");
        SetupPhysicalHint.Text = L("Choose your real headphones or speakers here. Sukut handles VB-CABLE routing automatically when you choose a mode.", "اختر سماعاتك الفعلية هنا. يتولى سُكوت التوجيه عبر VB-CABLE تلقائيًا عند اختيار وضع.");
        SetupVolumeTitle.Text = L("Make the volume keys work", "فعّل أزرار التحكم بالصوت");
        SetupVolumeDescription.Text = L("Just a precaution: this option is normally off after a fresh VB-CABLE install.", "للتأكّد فقط: يكون هذا الخيار معطّلًا عادةً بعد تثبيت VB-CABLE الجديد.");
        SetupVolumeEnableTitle.Text = L("2 · Open Options from the menu bar", "٢ · افتح Options من شريط القوائم");
        SetupVolumeLead.Text = L("From the menu bar, open ", "من شريط القوائم، افتح ");
        SetupVolumeThen.Text = L(", uncheck ", "، ألغِ ");
        SetupVolumeTail.Text = L(". Leave this option OFF.", ". أبقِ هذا الخيار معطّلًا.");
        SetupCablePanelLabel.Text = L("Open VB-CABLE as administrator", "فتح VB-CABLE كمسؤول");
        AutomationProperties.SetName(SetupCablePanelButton, SetupCablePanelLabel.Text);
        SetupVolumeScreenshot.Header = L("Options menu · turn volume control off", "قائمة Options · تعطيل التحكم بالصوت");
        SetupCheckedHint.Text = L("If this option is checked, turn it off. Otherwise, leave it as it is.", "إن كان الخيار محددًا، ألغِ تحديده. وإلا فاتركه كما هو.");
        SetupReadyTitle.Text = L("You’re ready", "أنت جاهز");
        SetupRoutingHint.Text = L("Choose a mode on Home. Sukut switches audio to VB-CABLE automatically.", "اختر وضعًا من الرئيسية. يحوّل سُكوت الصوت إلى VB-CABLE تلقائيًا.");
        SetupHalalHint.Text = L("Music Filter — removes music and improves speech clarity.", "مرشّح الموسيقى — يزيل الموسيقى ويحسّن وضوح الكلام.");
        SetupStrictHint.Text = L("Strict — the strongest music removal, muting music and background. Some sound effects may be removed too.", "صارم — أقوى إزالة للموسيقى مع كتم الموسيقى والخلفية. قد تختفي بعض المؤثرات الصوتية أيضًا.");
        SetupOptimizedHint.Text = L("Optimized — mutes music. Adjust background sounds with its Home slider.", "محسّن — يكتم الموسيقى. اضبط أصوات الخلفية من شريطه في الرئيسية.");
        SetupOffHint.Text = L("OFF — stops processing and uses your selected playback device.", "إيقاف — يوقف المعالجة ويستخدم جهاز التشغيل المختار.");
        SetupTrayHint.Text = _preferences.CloseToTray ? L("Closing keeps Sukut in the tray. Use its menu to open, stop, or exit.", "يبقى سُكوت في منطقة الإشعارات عند الإغلاق. من قائمته يمكنك الفتح أو الإيقاف أو الخروج.") : L("Closing exits Sukut and restores normal audio.", "يخرج إغلاق النافذة من سُكوت ويعيد الصوت العادي.");
        SetupReadyVolumeHint.Text = L("Keep your physical output selected in Sukut. Settings → Help has VB-CABLE, headphone effects and this setup guide.", "أبقِ سماعاتك الفعلية مختارة في سُكوت. تجد VB-CABLE ومؤثرات السماعات وهذا الدليل في الإعدادات ← المساعدة.");
        SetupBackButton.Content = L("Back", "السابق");
        AutomationProperties.SetName(SetupLanguageChoice, SetupLanguageLabel.Text); AutomationProperties.SetName(SetupOutputChoice, SetupOutputTitle.Text);
        RenderBufferingSettings();
        _applying = previous; UpdateSetupPage();
        ArabicTypography.Apply(SetupPanel, Arabic); QueueTypography();
    }
    private void RenderSetupOutput()
    {
        if (SetupPanel == null) return;
        bool previous = _applying; _applying = true;
        SetupOutputChoice.ItemTemplate = (DataTemplate)Root.Resources["EndpointTemplate"];
        SetupOutputChoice.ItemsSource = _outputs; SetupOutputChoice.SelectedItem = SelectedOutput;
        _applying = previous; UpdateSetupState();
    }
    private void UpdateSetupPage()
    {
        if (SetupPanel == null) return;
        SetupWelcomePage.Visibility = _setupStep == 0 ? Visibility.Visible : Visibility.Collapsed;
        SetupCablePage.Visibility = _setupStep == 1 ? Visibility.Visible : Visibility.Collapsed;
        SetupOutputPage.Visibility = _setupStep == 2 ? Visibility.Visible : Visibility.Collapsed;
        SetupVolumePage.Visibility = _setupStep == 3 ? Visibility.Visible : Visibility.Collapsed;
        SetupReadyPage.Visibility = _setupStep == 4 ? Visibility.Visible : Visibility.Collapsed;
        UpdateSetupProgress();
        SetupBackButton.IsEnabled = _setupStep > 0; SetupNextButton.Content = _setupStep == 4 ? L("Open Sukut", "فتح سُكوت") : L("Next", "التالي");
        UpdateSetupState();
    }
    private void UpdateSetupState()
    {
        if (SetupPanel == null) return;
        bool ready = CableReady;
        if (_setupStep == 0) { _skipCableStep = ready; UpdateSetupProgress(); }
        string status = !_setupScanned ? L("Preparing devices…", "جارٍ تجهيز الأجهزة…") : ready ? L("VB-CABLE is ready", "VB-CABLE جاهز") : L("VB-CABLE is needed", "يلزم تثبيت VB-CABLE");
        if (_lastSetupStatus != status)
        {
            _lastSetupStatus = status; SetupCableStatus.Text = status;
            // These brushes are theme resources, cached by the XAML status swatches.
            SetupCableDot.Fill = ready ? SetupReadySwatch.Fill : SetupMissingSwatch.Fill;
        }
        SetupInstallInstructions.Visibility = ready ? Visibility.Collapsed : Visibility.Visible;
        SetupRefreshButton.IsEnabled = _setupEngineReady && !_setupRefreshing;
        SetupReadyDevice.Text = SelectedOutput?.Name ?? "";
        SetupOutputHint.Text = OutputReady ? L("You can change this later on Home.", "يمكنك تغييره لاحقًا من الرئيسية.") : L("Connect your speakers or headphones, then go back and refresh.", "وصّل السماعات أو مكبر الصوت، ثم ارجع وحدّث الأجهزة.");
        SetupNextButton.IsEnabled = !_command && (_setupStep switch { 0 => true, 1 => _setupScanned && ready, 2 => OutputReady && ready, _ => _initialized && OutputReady && ready });
    }
    private void UpdateSetupProgress()
    {
        int total = _skipCableStep ? 4 : 5;
        int current = _skipCableStep && _setupStep >= 2 ? _setupStep : _setupStep + 1;
        SetupStepLabel.Text = L($"Step {current} of {total}", $"الخطوة {current} من {total}"); SetupProgress.Maximum = total; SetupProgress.Value = current;
    }
    private void SetupBack_Click(object sender, RoutedEventArgs args) { if (_setupStep == 2 && _skipCableStep) _setupStep = 0; else if (_setupStep > 0) _setupStep--; UpdateSetupPage(); }
    private async void SetupNext_Click(object sender, RoutedEventArgs args)
    {
        if (SetupNextButton == null || !SetupNextButton.IsEnabled) return;
        if (_setupStep < 4) { if (_setupStep == 0 && CableReady) { _skipCableStep = true; _setupStep = 2; } else _setupStep++; UpdateSetupPage(); return; }
        _preferences.SetupCompleted = true;
        if (!await SaveNow()) { _preferences.SetupCompleted = false; return; }
        HideSetup();
    }
    private void SetupLanguage_Changed(object sender, SelectionChangedEventArgs args)
    {
        if (!_applying && SetupLanguageChoice != null) SetLanguage(SetupLanguageChoice.SelectedIndex == 1 ? "ar" : "en");
    }
    private void SetupOutput_Changed(object sender, SelectionChangedEventArgs args)
    {
        if (_applying || SetupOutputChoice?.SelectedItem is not Endpoint output || output.Id == _preferences.Output) return;
        _preferences.Output = output.Id; Save(); UpdateSetupState();
    }
    private async void SetupRefresh_Click(object sender, RoutedEventArgs args)
    {
        if (!_setupEngineReady || _setupRefreshing) return;
        _setupRefreshing = true; UpdateSetupState();
        try { await _engine.Refresh(); _endpoints = []; await Poll(); }
        catch (Exception error) { Error(error.Message); }
        finally { _setupRefreshing = false; UpdateSetupState(); }
    }
    private async void RunSetup_Click(object sender, RoutedEventArgs args)
    {
        if (_command) return;
        if (_mode != 0 && !await ChangeMode(0, true)) return;
        _lastSetupStatus = ""; ShowSetup();
    }
}
