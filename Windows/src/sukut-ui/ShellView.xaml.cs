using System.Text.Json;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Windowing;
using Windows.System;

namespace Sukut;

public sealed partial class ShellView : UserControl
{
    private readonly Window _window;
    private AppWindow AppWindow => _window.AppWindow;
    public ShellState View { get; } = new();
    private readonly Engine _engine = new();
    private readonly DispatcherTimer _poll = new();
    private readonly string _root, _settingsFile;
    private readonly SettingsStore _settings;
    private HostLink _host = null!;
    private UiPreferences _preferences = new();
    private List<Endpoint> _endpoints = [], _outputs = [], _sources = [], _captures = [];
    private List<SourceApp> _apps = [];
    private readonly SourceAppIcons _sourceAppIcons = new();
    private EngineState? _state;
    private bool _applying = true, _command, _initialized, _polling, _visible = true, _allowExit, _loaded, _quitting;
    private bool? _controlsEnabled, _modesEnabled;
    private int _mode, _page;
    private readonly Stack<int> _pageHistory = new();
    private string _status = "Preparing", _statusText = "", _rawError = "";
    private bool _errorOpen;
    private bool _appearanceQueued;
    private readonly HostSettingsRevision _hostSettings = new();
    private bool Arabic => _preferences.Language == "ar";
    private string L(string en, string ar) => Arabic ? ar : en;
    private bool Minimized => AppWindow.Presenter is OverlappedPresenter { State: OverlappedPresenterState.Minimized };
    private bool CableReady => _sources.Exists(x => x.Id == _preferences.Source) && _captures.Exists(x => x.Id == _preferences.Capture);
    private Endpoint? SelectedOutput => _outputs.Find(x => x.Id == _preferences.Output);
    private bool ModeSetupReady => CableReady && OutputReady;
    private bool OutputReady => SelectedOutput != null;

    public static AudioModePreset GetModePreset(int mode) => AudioModePreset.ForMode(mode);

    public ShellView(Window owner)
    {
        App.DiagnosticStage = "shell constructor";
        _window = owner;
        _root = AppLocations.DataRoot;
        _settingsFile = Path.Combine(_root, "runtime", "sukut-ui.json");
        try { if (File.Exists(_settingsFile)) _preferences = JsonSerializer.Deserialize(File.ReadAllBytes(_settingsFile), SukutJsonContext.Default.UiPreferences) ?? new(); }
        catch { }
        MigrateLegacyRoute();
        int previousBackground = _preferences.OptimizedBackgroundDb;
        _preferences = _preferences.Copy();
        _selectedMode = _preferences.PreferredMode;
        _settings = new SettingsStore(_settingsFile);
        _settings.Failed += error => DispatcherQueue.TryEnqueue(() => Error(error.Message));
        InitializeComponent();
        InitializeAdvancedMixHold();
        _restartTimer.Tick += async (_, _) => await RunAutomaticRestart();
        if (previousBackground != _preferences.OptimizedBackgroundDb) Save();
    }

    internal void InitializeWindow()
    {
        App.DiagnosticStage = "window sizing and tray";
        _window.ExtendsContentIntoTitleBar = true;
        _window.SetTitleBar(AppTitleBar);
        // The root is transparent so the Fluent Mica backdrop shows; high contrast has no backdrop.
        if (new Windows.UI.ViewManagement.AccessibilitySettings().HighContrast)
            Root.Background = (Microsoft.UI.Xaml.Media.Brush)Application.Current.Resources["ApplicationPageBackgroundThemeBrush"];
        WindowSizing.Initialize(_window);
        AppWindow.SetIcon(Path.Combine(AppContext.BaseDirectory, "Assets", "Sukut.ico"));
        _windowModeIcons=new WindowModeIcons(WinRT.Interop.WindowNative.GetWindowHandle(_window));
        // The tray icon and flyout live in the native host (Sukut.exe).
        _host = new HostLink(_window, ShowWindow, () => _ = CloseWindow(), UpdateLifecycle);
        // Closing ends this window process entirely, so no GUI stays in memory.
        // With "Keep running in system tray" the host keeps filtering; without
        // it, Sukut exits and restores the original audio routes.
        AppWindow.Closing += (sender, args) =>
        {
            if (_allowExit) return;
            args.Cancel = true;
            if (_quitting) return;
            DispatcherQueue.TryEnqueue(() => { if (_preferences.CloseToTray) _ = CloseWindow(); else _ = Quit(); });
        };
        AppWindow.Changed += (_, _) => UpdateLifecycle();
        _window.Activated += (_, args) => { if (args.WindowActivationState != WindowActivationState.Deactivated && HomePanel != null) _ = RefreshRoutingChoices(includeApps: true); };
        Root.ActualThemeChanged += (_, _) => {UpdateLifecycle();RefreshHomeAppearance(refreshIcons:true);};
        _poll.Tick += async (_, _) => await Poll();
        ApplyLanguageAndTheme();
        App.DiagnosticStage = "shell constructed";
        _applying = false;
        Root.Loaded += async (_, _) =>
        {
            if (_loaded) return;
            _loaded = true;
            App.DiagnosticStage = "root loaded";
            ApplyLanguageAndTheme();
            if (_preferences.SetupCompleted) Navigate(0); else ShowSetup();
            App.DiagnosticStage = "engine initializing";
            try
            {
                await _engine.Initialize(_root, AppLocations.ResourceRoot, _host.Handle);
                await _engine.SetAmplification(Math.Clamp(_preferences.AmplificationDb, 0, 12));
                // The host may already be filtering (started from the tray).
                var first = await _engine.Read();
                if (first.Mode is >= 0 and <= 3) { _mode = first.Mode; SyncModeSelection(); }
                _setupEngineReady = true;
                UpdatePoll();
                await Poll();
                // Home's Source app list is visible: fill it once the engine answers.
                if (HomePanel != null) _ = RefreshRoutingChoices(includeApps: true);
                App.DiagnosticStage = "ready";
            }
            catch (Exception error) { Error(error.Message); }
        };
    }

    private void MigrateLegacyRoute()
    {
        if (!string.IsNullOrEmpty(_preferences.Output)) return;
        string path = Path.Combine(_root, "docs", "steps", "step-08", "evidence", "ui-settings.ini");
        if (!File.Exists(path)) return;
        foreach (string line in File.ReadLines(path))
        {
            int equals = line.IndexOf('='); if (equals < 0) continue;
            string key = line[..equals], value = line[(equals + 1)..];
            if (key == "physical_render_id") _preferences.Output = value;
            else if (key == "source_render_id") _preferences.Source = value;
            else if (key == "capture_id") _preferences.Capture = value;
        }
    }

    private void Save() => _settings.Schedule(_preferences);
    private async Task<bool> SaveNow()
    {
        try { await _settings.Flush(_preferences); return true; }
        catch (Exception error) { Error(error.Message); return false; }
    }

    private void ApplyLanguageAndTheme()
    {
        Root.Language = Arabic ? "ar" : "en";
        Root.FlowDirection = Arabic ? FlowDirection.RightToLeft : FlowDirection.LeftToRight;
        FontFamily = (Microsoft.UI.Xaml.Media.FontFamily)Application.Current.Resources[Arabic ? "SukutArabicFontFamily" : "ContentControlThemeFontFamily"];
        RequestedTheme = _preferences.Theme == "dark" ? ElementTheme.Dark : _preferences.Theme == "light" ? ElementTheme.Light : ElementTheme.Default;
        AppWindow.TitleBar.PreferredTheme = RequestedTheme == ElementTheme.Dark ? TitleBarTheme.Dark : RequestedTheme == ElementTheme.Light ? TitleBarTheme.Light : TitleBarTheme.UseDefaultAppMode;
        LocalizeNavigation();
        if (HomePanel != null) { ApplyHomeTypography(); RenderHome(); }
        if (SettingsPanel != null) RenderSettings();
        if (AboutPanel != null) RenderAbout();
        if (SetupPanel != null) ApplySetupLanguage();
        RenderStatus(); RenderError();
        QueueTypography();
    }

    private void HomePanel_Loaded(object sender, RoutedEventArgs args)
    {
        FitPageViewport(); ApplyHomeTypography();
        if (_apps.Count == 0) _apps.Add(new SourceApp { Name = L("All system audio", "صوت النظام بالكامل") });
        RenderHome(); RenderStatus(); RenderError(); QueueTypography();
        _ = RefreshRoutingChoices(includeApps: true);
    }
    private async void SettingsPanel_Loaded(object sender, RoutedEventArgs args)
    {
        FitPageViewport(); RenderSettings();
        try { await LoadApps(); } catch (Exception error) { Error(error.Message); }
        FocusRequestedSourceApp();
        QueueTypography();
    }
    private void AboutPanel_Loaded(object sender, RoutedEventArgs args) { RenderAbout(); StartBrand(); QueueTypography(); }
    private void SettingsPanel_ViewChanged(object? sender, ScrollViewerViewChangedEventArgs args) => UpdateBrandInView();
    private void AboutPanel_Unloaded(object sender, RoutedEventArgs args) => StopBrand();

    private void RenderHome()
    {
        if (HomePanel == null) return;
        
        RenderOverloadTip();
        HomeTitle.Text = L("Audio","الصوت");
        HeaderSubtitle.Text = L("Output, source and filtering", "مخرج الصوت والمصدر والترشيح");
        AutomationProperties.SetName(HomeSettingsButton,L("Settings","الإعدادات"));
        OutputLabel.Text = L("Audio output", "مخرج الصوت");
        BufferingLabel.Text = L("Audio buffering", "التخزين المؤقت للصوت");
        OutputSourceAppTitle.Text = L("Source app", "التطبيق المصدر");
        AutomationProperties.SetName(SourceAppList, OutputSourceAppTitle.Text); AutomationProperties.SetName(OutputList, OutputLabel.Text);
        string refreshApps = L("Refresh audio outputs and source apps", "تحديث مخارج الصوت والتطبيقات المصدر"), modeHelp = L("Audio modes and buffering help", "شرح أوضاع الصوت والتخزين المؤقت");
        AutomationProperties.SetName(RefreshAppsButton, refreshApps); ToolTipService.SetToolTip(RefreshAppsButton, refreshApps);
        AutomationProperties.SetName(ModeHelpButton, modeHelp); ToolTipService.SetToolTip(ModeHelpButton, modeHelp);
        ModeHeading.Text = L("Audio mode", "وضع الصوت");
        HalalLabel.Text = L("Music Filter", "مرشّح الموسيقى");
        OptimizedLabel.Text = L("Optimized", "محسّن");
        StrictLabel.Text = L("Strict", "صارم");
        HalalDescription.Text=L("Keep speech and other sounds","الإبقاء على الكلام والأصوات الأخرى");
        OptimizedDescription.Text=L("Adjust the background level","ضبط مستوى أصوات الخلفية");
        StrictDescription.Text=L("Focus on speech","التركيز على الكلام");
        AutomationProperties.SetName(HalalCard, HalalLabel.Text); AutomationProperties.SetName(OptimizedCard, OptimizedLabel.Text);
        AutomationProperties.SetName(StrictCard, StrictLabel.Text);
        bool previous = _applying; _applying = true;
        RenderSourceAppChoices();
        ModeChoices.SelectedIndex = _selectedMode-1;
        RenderBufferingSettings();
        RenderModeDetails(_selectedMode);
        RenderEngineRow();
        _applying = previous;
        SetEnabled(true);
    }

    private void RenderModeDetails(int mode)
    {
        if (OptimizedBackgroundPanel == null) return;
        var preset = AudioModePreset.ForMode(mode, _preferences.OptimizedBackgroundDb,_preferences.AdvancedOptimized,_preferences.AdvancedVoiceDb,_preferences.AdvancedMusicDb,_preferences.AdvancedBackgroundDb);
        string Gain(float db, bool muted) => muted ? L("Muted", "مكتوم") : (db > 0 ? "+" : "") + db.ToString("0", System.Globalization.CultureInfo.InvariantCulture) + " dB";
        OptimizedBackgroundPanel.Visibility = mode == 2 && !_preferences.AdvancedOptimized ? Visibility.Visible : Visibility.Collapsed;
        OptimizedBackgroundHeading.Text = L("Background level", "مستوى الخلفية");

        AutomationProperties.SetName(OptimizedBackgroundSlider, OptimizedBackgroundHeading.Text);
        bool applying = _applying; _applying = true;
        if (!OptimizedBackgroundSlider.IsInteracting) OptimizedBackgroundSlider.Value = Math.Clamp(_preferences.OptimizedBackgroundDb, -20, 10);
        OptimizedBackgroundValue.Text = Gain((float)OptimizedBackgroundSlider.Value, false);
        RenderAdvancedMix(mode);
        _applying = applying;
    }

    private void RenderSettings()
    {
        if (SettingsPanel == null) return;
        bool previous = _applying; _applying = true;
        SettingsHeading.Text = L("Settings", "الإعدادات");
        GeneralSettingsHeading.Text = L("General", "عام"); AudioSettingsHeading.Text = L("Audio", "الصوت"); HelpSettingsHeading.Text = L("Shortcuts", "اختصارات");
        RenderAmplification();
        RenderVolumeTools();
        LanguageCard.Header = L("Language", "اللغة"); ThemeCard.Header = L("App theme", "المظهر");
        TrayCard.Header = L("Close to tray", "الإغلاق إلى منطقة الإشعارات");
        TrayCard.Description = L("Closing the window keeps Sukut in the tray, using under 20 MB.", "إغلاق النافذة يُبقي سُكوت في منطقة الإشعارات بأقل من 20 ميغابايت.");
        SetupCard.Header = L("Setup guide", "دليل الإعداد"); RunSetupLabel.Text = L("Open guide", "فتح الدليل");
        AutomationProperties.SetName(RunSetupButton, RunSetupLabel.Text);
        LanguageChoice.SelectedIndex = Arabic ? 1 : 0;
        SystemTheme.Content = L("System", "النظام"); LightTheme.Content = L("Light", "فاتح"); DarkTheme.Content = L("Dark", "داكن");
        // Populate captions before selecting, otherwise the initial selection
        // box can keep the item's earlier empty content.
        ThemeChoice.SelectedIndex = -1;
        ThemeChoice.SelectedIndex = _preferences.Theme == "light" ? 1 : _preferences.Theme == "dark" ? 2 : 0;
        TraySwitch.IsOn = _preferences.CloseToTray; TraySwitch.OnContent = L("On", "تشغيل"); TraySwitch.OffContent = L("Off", "إيقاف");
        AdvancedHeader.Text = L("Advanced", "متقدم");
        AutomationProperties.SetName(AdvancedSection, AdvancedHeader.Text);
        AppCard.Text = L("Source app", "التطبيق المصدر"); InputCard.Text = L("Virtual input", "الإدخال الافتراضي"); CaptureCard.Text = L("Audio capture", "التقاط الصوت");
        RefreshLabel.Text = L("Refresh devices", "تحديث الأجهزة");
        RenderBufferingSettings();
        RenderSourceAppChoices();
        InputChoice.ItemsSource = _sources; InputChoice.SelectedItem = _sources.Find(x => x.Id == _preferences.Source);
        CaptureChoice.ItemsSource = _captures; CaptureChoice.SelectedItem = _captures.Find(x => x.Id == _preferences.Capture);
        AutomationProperties.SetName(LanguageChoice, (string)LanguageCard.Header); AutomationProperties.SetName(ThemeChoice, (string)ThemeCard.Header);
        AutomationProperties.SetName(TraySwitch, (string)TrayCard.Header); AutomationProperties.SetName(AppChoice, AppCard.Text);
        AutomationProperties.SetName(InputChoice, InputCard.Text); AutomationProperties.SetName(CaptureChoice, CaptureCard.Text);
        _applying = previous;
        SetEnabled(true);
    }

    private void RenderAbout()
    {
        if (AboutPanel == null) return;
        AboutHeading.Text = PageName(2);
        AboutVersion.Text = ReleaseVersion;
        DevelopedLabel.Text = L("Developed by", "تطوير"); FontCredit.Text = L("IBM Plex Sans Arabic · Readex Pro · Handjet · SIL OFL", "IBM Plex Sans Arabic · Readex Pro · Handjet · رخصة SIL OFL");
        RenderChangelog();
    }

    // Native desktop navigation; setup temporarily owns the full window.
    private string PageName(int page) => page switch { 1 => L("Settings", "الإعدادات"), 2 => L("About", "حول"), 3 => L("Phone", "الهاتف"), _ => L("Home", "الرئيسية") };

    private void LocalizeNavigation()
    {
        void Name(FrameworkElement button, string text) { AutomationProperties.SetName(button, text); ToolTipService.SetToolTip(button, text); }
        Name(PageBackButton, L("Back", "رجوع"));
        DesktopHomeItem.Content=L("Audio","الصوت"); HomePhoneButton.Content=PageName(3); HomeSettingsButton.Content=PageName(1);
        Name(DesktopHomeItem,L("Audio","الصوت")); Name(HomePhoneButton,PageName(3)); Name(HomeSettingsButton,PageName(1));
        PageTitle.Text = PageName(_page);
        if (HomePanel == null) return;
        Name(HomeSettingsButton, PageName(1)); Name(HomePhoneButton, PageName(3));
    }

    private void Navigate(int page, bool remember = true)
    {
        bool animate=_loaded && page!=_page;
        if (remember && _loaded && page != _page) _pageHistory.Push(_page);
        if (page != 1) StopBrand();
        bool previous = _applying; _applying = true;
        _page = page;
        DesktopNavigation.SelectedItem=page==1?HomeSettingsButton:page==3?HomePhoneButton:DesktopHomeItem;
        View.HomeLoaded = page == 0; View.SettingsLoaded = page == 1; View.PhoneLoaded = page == 3;
        SubPageHeader.Visibility = page == 0 ? Visibility.Collapsed : Visibility.Visible;
        // Phone has one Back command in its fixed footer, with setup-step semantics.
        PageBackButton.Visibility = page == 3 ? Visibility.Collapsed : Visibility.Visible;
        SubPageHeader.ColumnSpacing = page == 3 ? 0 : 10;
        FitPageViewport();
        LocalizeNavigation();
        _applying = previous;
        if(animate) AnimatePageContent(PageHost);
        // Keep keyboard focus useful after a page swap: Back on sub-pages.
        if (page == 1 && _loaded) DispatcherQueue.TryEnqueue(Microsoft.UI.Dispatching.DispatcherQueuePriority.Low, () => PageBackButton.Focus(FocusState.Programmatic));
    }

    private void GoBack()
    {
        if (_setupVisible || _quitting) return;
        if (_page == 3 && _phoneTask != PhoneTask.Choices) { PhoneGoBack(); return; }
        if (_pageHistory.TryPop(out int previous)) Navigate(previous, remember: false);
        else if (_page != 0) Navigate(0, remember: false);
    }
    private void PageBack_Click(object sender, RoutedEventArgs args) => GoBack();

    private void DesktopBack_Invoked(KeyboardAccelerator sender, KeyboardAcceleratorInvokedEventArgs args)
    {
        if (_setupVisible || _quitting || _page == 0 || _modeHelpOpen) return;
        if (Root.XamlRoot != null && Microsoft.UI.Xaml.Media.VisualTreeHelper.GetOpenPopupsForXamlRoot(Root.XamlRoot).Any(popup => popup.Child is not ToolTip)) return;
        // Escape first closes an overlay navigation pane; an expanded desktop
        // sidebar stays in place when returning to Audio.
        if (sender.Key == VirtualKey.Escape && DesktopNavigation.IsPaneOpen && DesktopNavigation.DisplayMode != NavigationViewDisplayMode.Expanded) return;
        args.Handled = true;
        GoBack();
    }

    private void SetEnabled(bool force = false)
    {
        bool enabled = _initialized && !_command && (_state?.Busy ?? 0) == 0 && (_state?.HostCommand ?? 0) == 0;
        if (force || _controlsEnabled != enabled)
        {
            _controlsEnabled = enabled;
            if (OutputList != null) OutputList.IsEnabled = enabled;
            if(PowerButton!=null)PowerButton.IsEnabled=enabled;
            if (SourceAppList != null) SourceAppList.IsEnabled = enabled;
            if (RefreshAppsButton != null) RefreshAppsButton.IsEnabled = enabled;
            if (InputChoice != null) InputChoice.IsEnabled = enabled;
            if (CaptureChoice != null) CaptureChoice.IsEnabled = enabled;
            if (AppChoice != null) AppChoice.IsEnabled = enabled;
            if (RefreshButton != null) RefreshButton.IsEnabled = enabled;
            if (HomeBuffering != null) HomeBuffering.IsEnabled = enabled;
            if (SetupBuffering != null) SetupBuffering.IsEnabled = enabled;
            if (AmplificationSlider != null) AmplificationSlider.IsEnabled = enabled;
        }
        bool modesEnabled = enabled;
        if (RestartErrorButton != null) RestartErrorButton.IsEnabled = enabled && _mode != 0;
        if (RestartOverloadButton != null) RestartOverloadButton.IsEnabled = enabled && _mode != 0;
        if (OptimizedBackgroundSlider != null) OptimizedBackgroundSlider.IsEnabled = enabled;
        foreach(var slider in _advancedSliders) slider.IsEnabled = enabled;
        if (force || _modesEnabled != modesEnabled)
        {
            _modesEnabled = modesEnabled;
            if (ModeChoices != null) ModeChoices.IsEnabled = modesEnabled;
        }
    }

    private string StatusText() => _status switch
    {
        // Standby: nothing is playing, so the AI rests until sound returns.
        "Active" when _state?.Standby == 1 => L("Standby", "استعداد"),
        "Active" => L("On", "يعمل"),
        "Preparing" => _command ? L("Starting…", "جارٍ التشغيل…") : L("Preparing", "جارٍ التجهيز"),
        "Reconnecting" => L($"Reconnecting {Math.Min(3, _audioRestart.Attempts + (_automaticRestarting ? 0 : 1))}/3", $"إعادة الاتصال {Math.Min(3, _audioRestart.Attempts + (_automaticRestarting ? 0 : 1))}/3"),
        "Recovering" => _state?.Paused != 0 ? L("Recovering", "جارٍ الاستعادة") : L("Recently recovered", "استُعيد الصوت مؤخرًا"), "Attention" => L("Attention", "تنبيه"), _ => L("Off", "متوقف")
    };
    private void Status(string state)
    {
        string previous = _status; _status = state;
        string text = StatusText();
        if (previous == state && _statusText == text) return;
        RenderStatus();
    }
    private void RenderStatus()
    {
        _statusText = StatusText();
        if (StatusHost != null) StatusLabel.Text = _statusText;
        RefreshHomeAppearance();
    }

    private string ErrorText(string raw) => !Arabic ? raw : raw switch
    {
        "close the CleanWave preview before starting Sukut" => "أغلق نافذة CleanWave قبل تشغيل سُكوت.",
        "selected source app is no longer running" => "التطبيق المصدر لم يعد يعمل.",
        "reopen the source app to restore its audio route" => "أعد فتح التطبيق المصدر لاستعادة مسار صوته.",
        "bridge is still stopping" => "جارٍ إيقاف الصوت، انتظر قليلًا.", "audio engine is preparing" => "جارٍ تجهيز محرك الصوت.",
        "audio route is still switching" => "جارٍ تبديل مسار الصوت.", _ => raw
    };
    private void Error(string message)
    {
        if (_rawError == message) return;
        _rawError = message; _errorOpen = true;
        if (!_setupVisible && _page != 0) Navigate(0);
        RenderError(); Status("Attention");
    }
    private void RenderError()
    {
        if (RestartErrorButton != null) RestartErrorButton.Content = L("Restart audio", "إعادة تشغيل الصوت");
        foreach (var bar in new[] { ErrorBar, SetupErrorBar })
            if (bar != null) { bar.Title = L("Attention", "تنبيه"); bar.Message = ErrorText(_rawError); bar.Visibility = _errorOpen ? Visibility.Visible : Visibility.Collapsed; bar.IsOpen = _errorOpen; }
    }
    private void ErrorBar_Closed(InfoBar sender, InfoBarClosedEventArgs args) { _errorOpen = false; sender.Visibility = Visibility.Collapsed; }

    private async Task Poll(bool force = false)
    {
        if (_polling || _command || _quitting || !force && !_visible && _mode == 0) return;
        _polling = true;
        try
        {
            _state = await _engine.Read();
            if (_quitting) return;
            // A mode chosen from the tray flyout while this window is open.
            if (!_command && _state.HostCommand == 0 && _state.Mode is >= 0 and <= 3 && _state.Mode != _mode)
            {
                _mode = _state.Mode; CancelAutomaticRestart(); SyncModeSelection();
            }
            _initialized = _state.Initialized != 0; _setupScanned = _state.Busy == 0;
            if (!_command && _hostSettings.TryAdopt(_state, _preferences))
            {
                CancelAutomaticRestart();
                Save();
                RenderEngineRow(); RenderBufferingSettings(); RenderSourceAppChoices(); RenderModeDetails(_selectedMode);
            }
            UpdateEndpoints(_state); SetEnabled();
            // The host owns a device/tray switch even while the window is open.
            // Do not launch a competing recovery while that command is running.
            if (_state.HostCommand != 0) { Status("Preparing"); return; }
            ObserveRecovery(_state);
            if (_state.Error == "selected source app is no longer running" && _state.Busy == 0 && _state.State == 0 && _state.PipelineActive == 0)
            {
                // Closing the chosen source ends that session, not Sukut.
                // Do not silently start filtering other apps. The next explicit
                // mode click can use All system audio, or another app in Settings.
                if (_preferences.SourcePid != 0 || _mode != 0)
                {
                    CancelAutomaticRestart(); _preferences.SourcePid = 0;
                    Save();
                    // Apply normal OFF too, so the selected physical output
                    // remains the playback device after the source disappears.
                    await ChangeMode(0, force: true);
                    if (SettingsPanel != null) RenderSettings();
                }
                Error(L("Source app closed. Select a mode for all system audio, or choose another Source app in Settings.", "أُغلق التطبيق المصدر. اختر وضعًا لصوت النظام بالكامل، أو اختر تطبيقًا آخر من الإعدادات."));
            }
            else if (!string.IsNullOrWhiteSpace(_state.Error)) AudioFault(_state.Error);
            else if (_mode != 0 && _state.Busy == 0 && (_state.State != 1 || _state.PipelineActive == 0 || _state.ControllerAgeMs > 5000))
                AudioFault(L("Audio stopped responding. Restart the audio bridge.", "توقف الصوت عن الاستجابة. أعد تشغيل جسر الصوت."));
            else if (_mode != 0 && _overloadDetected && !_overloadTipDismissed)
                AudioFault(L("Repeated processing overload detected.", "رُصد ضغط متكرر على معالجة الصوت."), overload: true);
            else
            {
                if (_state.State == 1 && _state.PipelineActive != 0 && _state.Busy == 0 && _state.Paused == 0)
                {
                    _audioRestart.Healthy(Environment.TickCount64); CancelAutomaticRestart(reset: false);
                }
                Status(_automaticRestarting || _audioRestart.Pending ? "Reconnecting" : _state.Busy != 0 || !_initialized ? "Preparing" : _state.State == 2 ? "Attention" : _state.State == 1 ? _state.Paused != 0 || Environment.TickCount64 < _recoveryUntil ? "Recovering" : "Active" : "Off");
            }
            if (SetupPanel != null) UpdateSetupState();
        }
        catch (Exception error) { AudioFault(error.Message); }
        finally { _polling = false; UpdatePoll(); }
    }

    private void UpdateEndpoints(EngineState state)
    {
        if (state.EndpointsVersion < _lastEndpointVersion) return;
        _lastEndpointVersion = state.EndpointsVersion;
        bool same = _endpoints.Count == state.Endpoints.Count;
        for (int index = 0; same && index < _endpoints.Count; index++)
        {
            var a = _endpoints[index]; var b = state.Endpoints[index];
            same = a.Id == b.Id && a.Name == b.Name && a.Flow == b.Flow && a.Virtual == b.Virtual;
        }
        if (same) return;
        _endpoints = state.Endpoints; _outputs = []; _sources = []; _captures = [];
        foreach (var endpoint in _endpoints)
            if (endpoint.Flow == 1 && endpoint.Virtual == 0) _outputs.Add(endpoint);
            else if (endpoint.Flow == 1 && endpoint.Virtual != 0) _sources.Add(endpoint);
            else if (endpoint.Flow == 0 && endpoint.Virtual != 0) _captures.Add(endpoint);
        var output = _outputs.Find(x => x.Id == _preferences.Output) ?? _outputs.Find(x => x.Id == state.Output) ?? _outputs.FirstOrDefault();
        var source = _sources.Find(x => x.Id == _preferences.Source) ?? _sources.Find(x => x.Id == state.Source) ?? _sources.Find(x => x.Name.StartsWith("CABLE Input", StringComparison.OrdinalIgnoreCase)) ?? _sources.FirstOrDefault();
        var capture = _captures.Find(x => x.Id == _preferences.Capture) ?? _captures.Find(x => x.Id == state.Capture) ?? _captures.Find(x => x.Name.StartsWith("CABLE Output", StringComparison.OrdinalIgnoreCase)) ?? _captures.FirstOrDefault();
        bool dirty = output != null && output.Id != _preferences.Output || source != null && source.Id != _preferences.Source || capture != null && capture.Id != _preferences.Capture;
        if (output != null && state.HostCommand == 0) _preferences.Output = output.Id; if (source != null) _preferences.Source = source.Id; if (capture != null) _preferences.Capture = capture.Id;
        if (dirty) Save();
        if (HomePanel != null) RenderHome(); if (SettingsPanel != null) RenderSettings(); if (SetupPanel != null) RenderSetupOutput();
    }

    private Task? _appsLoading;
    private Task LoadApps() => _appsLoading is { IsCompleted: false } ? _appsLoading : _appsLoading = LoadAppsCore();
    private async Task LoadAppsCore()
    {
        var windows = SourceAppDiscovery.RunningWindows();
        var apps = SourceAppDiscovery.Merge(await _engine.Apps(), await windows);
        await _sourceAppIcons.Populate(apps);
        if (_quitting) return;
        _apps = apps; _apps.Insert(0, new SourceApp { Name = L("All system audio", "صوت النظام بالكامل") });
        if (!_apps.Exists(x => x.Pid == _preferences.SourcePid))
            _apps.Add(new SourceApp { Pid = _preferences.SourcePid, Name = L("Selected app (unavailable)", "التطبيق المحدد (غير متاح)") });
        if (SettingsPanel != null) RenderSettings();
        else RenderSourceAppChoices();
        
    }

    private void RenderSourceAppChoices()
    {
        bool previous = _applying; _applying = true;
        try
        {
            string systemLabel = L("All system audio", "صوت النظام بالكامل");
            bool renamed = _apps.Count > 0 && _apps[0].Name != systemLabel;
            if (_apps.Count > 0) _apps[0].Name = systemLabel;
            var selected = _apps.Find(x => x.Pid == _preferences.SourcePid);
            foreach (var picker in new Microsoft.UI.Xaml.Controls.Primitives.Selector?[] { AppChoice, SourceAppList })
                if (picker != null)
                {
                    // A selection should not rebuild the horizontal list and
                    // jump away from the card the user just chose.
                    if (renamed || !ReferenceEquals(picker.ItemsSource, _apps)) { picker.ItemsSource = null; picker.ItemsSource = _apps; }
                    if (!ReferenceEquals(picker.SelectedItem, selected)) picker.SelectedItem = selected;
                }
            if (SourceAppHint != null) SourceAppHint.Text = L("Open an app, then refresh to add it here.", "افتح تطبيقًا ثم حدّث القائمة لإضافته هنا.");
        }
        finally { _applying = previous; }
    }

    private async Task<bool> ChangeMode(int mode, bool force = false, bool restart = false, bool automatic = false)
    {
        if (_command || !_initialized) return false;
        if (_mode == mode && !force) { SyncModeSelection(); return true; }
        if (!automatic && (mode == 0 || mode != _mode || restart)) CancelAutomaticRestart();
        if (mode != 0 && !ModeSetupReady)
        {
            ShowSetup(); SyncModeSelection();
            return false;
        }
        _command = true; _poll.Stop(); SetEnabled(); _rawError = ""; _errorOpen = false; RenderError(); Status(automatic ? "Reconnecting" : "Preparing");
        bool success = false;
        try
        {
            if (!await SaveNow()) return false;
            await _engine.Select(mode, _preferences.Source, _preferences.Capture, _preferences.Output, _preferences.SourcePid, Math.Clamp(_preferences.Buffering, 0, 2), _preferences.CableVolumeDisabledConfirmed, _preferences.OptimizedBackgroundDb, restart,_preferences.AdvancedOptimized,_preferences.AdvancedVoiceDb,_preferences.AdvancedMusicDb,_preferences.AdvancedBackgroundDb);
            _mode = mode; success = true;
        }
        catch (Exception error) { if (_quitting) RecordShutdownError(error.Message); else if (automatic) _automaticFailure = error.Message; else Error(error.Message); }
        finally
        {
            _command = false;
            if (!_quitting)
            {
                SyncModeSelection();
                if (!automatic) await Poll(force: true);
                if (_backgroundCommitPending) await ApplyCommittedBackground();
            }
        }
        return success;
    }
    private void SyncModeSelection()
    {
        if(_mode!=0) {
            _selectedMode=_mode;
            if(_preferences.PreferredMode!=_mode){_preferences.PreferredMode=_mode;Save();}
        }
        RefreshHomeAppearance();
        if(ModeChoices==null)return;
        bool previous=_applying;_applying=true;ModeChoices.SelectedIndex=_selectedMode-1;_applying=previous;
        RenderModeDetails(_selectedMode);
    }
    private void Modes_SizeChanged(object sender,SizeChangedEventArgs args)
    {
        if(ModeChoices==null)return;
        double width=Math.Max(0,Math.Floor(args.NewSize.Width));
        foreach(var card in new[]{HalalCard,OptimizedCard,StrictCard})card.Width=width;
    }
    private async void Modes_SelectionChanged(object sender,SelectionChangedEventArgs args)
    {
        if(_applying || ModeChoices==null)return;
        if(_command || !_initialized){SyncModeSelection();return;}
        int mode=ModeChoices.SelectedIndex+1;
        if(mode<1 || mode>3)return;
        _selectedMode=mode;_preferences.PreferredMode=mode;Save();RenderModeDetails(mode);RefreshHomeAppearance();
        if(_mode!=0)await ChangeMode(mode);
    }
    private void RenderEngineRow()
    {
        if (OutputList == null) return;
        bool previous = _applying; _applying = true;
        if (!ReferenceEquals(OutputList.ItemsSource, _outputs)) OutputList.ItemsSource = _outputs;
        OutputList.SelectedItem = SelectedOutput;
        _applying = previous;
    }
    private async void OutputList_Changed(object sender, SelectionChangedEventArgs args)
    {
        if (_applying || OutputList?.SelectedItem is not Endpoint endpoint) return;
        if (endpoint.Id == _preferences.Output) return;
        _preferences.Output = endpoint.Id; Save();
        RenderEngineRow();
        if (_mode != 0) await ChangeMode(_mode, true);
    }
    private async void Route_Changed(object sender, SelectionChangedEventArgs args)
    {
        if (_applying) return;
        bool dirty = false;
        if (InputChoice?.SelectedItem is Endpoint source && source.Id != _preferences.Source) { _preferences.Source = source.Id; dirty = true; }
        if (CaptureChoice?.SelectedItem is Endpoint capture && capture.Id != _preferences.Capture) { _preferences.Capture = capture.Id; dirty = true; }
        if (!dirty) return; Save(); if (_mode != 0) await ChangeMode(_mode, true);
    }
    private async void App_Changed(object sender, SelectionChangedEventArgs args)
    {
        if (_applying || _command || sender is not Microsoft.UI.Xaml.Controls.Primitives.Selector { SelectedItem: SourceApp source } || source.Pid == _preferences.SourcePid) return;
        _preferences.SourcePid = source.Pid; Save(); RenderSourceAppChoices();  if (_mode != 0) await ChangeMode(_mode, true);
    }
    private void Language_Changed(object sender, SelectionChangedEventArgs args)
    {
        if (_applying || LanguageChoice == null) return;
        SetLanguage(LanguageChoice.SelectedIndex == 1 ? "ar" : "en");
    }
    private void SetLanguage(string language)
    {
        if (_preferences.Language == language) return;
        _preferences.Language = language; QueueAppearance(); Save();
    }
    private void Theme_Changed(object sender, SelectionChangedEventArgs args)
    {
        if (_applying || ThemeChoice == null) return;
        string theme = ThemeChoice.SelectedIndex == 1 ? "light" : ThemeChoice.SelectedIndex == 2 ? "dark" : "system";
        if (_preferences.Theme == theme) return;
        _preferences.Theme = theme; QueueAppearance(); Save();
    }
    private void QueueAppearance()
    {
        if (_appearanceQueued) return;
        _appearanceQueued = true;
        DispatcherQueue.TryEnqueue(Microsoft.UI.Dispatching.DispatcherQueuePriority.Low, () =>
        {
            _appearanceQueued = false;
            if (_quitting) return;
            bool previous = _applying; _applying = true;
            ApplyLanguageAndTheme(); _applying = previous;
        });
    }
    private void TraySwitch_Toggled(object sender, RoutedEventArgs args)
    {
        if (_applying || TraySwitch == null || _preferences.CloseToTray == TraySwitch.IsOn) return;
        _preferences.CloseToTray = TraySwitch.IsOn; Save();
    }
    private async void Refresh_Click(object sender, RoutedEventArgs args)
    {
        try { await _engine.Refresh(); _endpoints = []; await LoadApps(); await Poll(); }
        catch (Exception error) { Error(error.Message); }
    }

    private void UpdatePoll()
    {
        if (!_setupEngineReady || _command || _quitting || !_visible && _mode == 0) { _poll.Stop(); return; }
        var interval = TimeSpan.FromMilliseconds(_visible && !Minimized && (_mode != 0 || !_initialized) ? 800 : 2500);
        if (_poll.Interval != interval) _poll.Interval = interval;
        if (!_poll.IsEnabled) _poll.Start();
    }
    private void UpdateLifecycle() { WindowSizing.UpdateMinimum(_window); UpdatePoll(); TrimWhenMinimized();if (_visible && !Minimized && _page == 1 && !_setupVisible && AboutPanel != null) StartBrand(); else StopBrand(); }
    private void ShowWindow()
    {
        _visible = true;
        if (AppWindow.Presenter is OverlappedPresenter { State: OverlappedPresenterState.Minimized } presenter) presenter.Restore();
        AppWindow.Show(); _window.Activate();
        if (_setupVisible) View.SetupLoaded = true; else Navigate(_page);
        UpdateLifecycle(); if (_setupEngineReady) _ = Poll();
    }
    // Close this window process; the tray host keeps Sukut running.
    private Task CloseWindow() => Shutdown(exitSukut: false);
    // Exit Sukut entirely; the host restores the original audio routes.
    private Task Quit() => Shutdown(exitSukut: true);
    private async Task Shutdown(bool exitSukut)
    {
        if (_quitting) return;
        _quitting = true; _poll.Stop(); CancelAutomaticRestart(); StopBrand();
        // Disappear at once; saving and disconnecting finish in the background.
        AppWindow.Hide();
        // Exit is a teardown command, not another filtering/route-selection
        // request. Old capture faults and a closed source app cannot veto it.
        try { await _settings.Flush(_preferences).WaitAsync(TimeSpan.FromSeconds(2)); }
        catch (Exception error) { RecordShutdownError(error.Message); }
        if (exitSukut)
        {
            try { await _engine.ExitHost().WaitAsync(TimeSpan.FromSeconds(3)); }
            catch (Exception error) { RecordShutdownError(error.Message); }
        }
        try { await _engine.DisposeAsync().AsTask().WaitAsync(TimeSpan.FromSeconds(3)); }
        catch (Exception error) { RecordShutdownError(error.Message); }
        try { await _settings.DisposeAsync().AsTask().WaitAsync(TimeSpan.FromSeconds(2)); }
        catch (Exception error) { RecordShutdownError(error.Message); }
        _windowModeIcons?.Dispose(); _host.Dispose(); _allowExit = true; (Application.Current as App)?.ReleaseInstance();
        _window.Close(); Application.Current.Exit();
    }
    private void RecordShutdownError(string error)
    {
        try { Directory.CreateDirectory(Path.Combine(_root, "runtime")); File.AppendAllText(Path.Combine(_root, "runtime", "shutdown-last-error.txt"), error + Environment.NewLine); }
        catch { /* A failed optional diagnostic must not prevent Exit. */ }
    }
}
