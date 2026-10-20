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
    private TrayIcon _tray = null!;
    private UiPreferences _preferences = new();
    private List<Endpoint> _endpoints = [], _outputs = [], _sources = [], _captures = [];
    private List<SourceApp> _apps = [];
    private readonly SourceAppIcons _sourceAppIcons = new();
    private EngineState? _state;
    private bool _applying = true, _command, _initialized, _polling, _visible = true, _allowExit, _loaded, _quitting;
    private bool? _controlsEnabled, _modesEnabled;
    private int _mode, _page;
    private string _status = "Preparing", _statusText = "", _rawError = "";
    private bool _errorOpen;
    private bool _appearanceQueued;
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
        _root = FindRoot();
        _settingsFile = Path.Combine(_root, "runtime", "sukut-ui.json");
        try { if (File.Exists(_settingsFile)) _preferences = JsonSerializer.Deserialize(File.ReadAllBytes(_settingsFile), SukutJsonContext.Default.UiPreferences) ?? new(); }
        catch { }
        MigrateLegacyRoute();
        int previousBackground = _preferences.OptimizedBackgroundDb;
        _preferences = _preferences.Copy();
        _settings = new SettingsStore(_settingsFile);
        _settings.Failed += error => DispatcherQueue.TryEnqueue(() => Error(error.Message));
        InitializeComponent();
        _restartTimer.Tick += async (_, _) => await RunAutomaticRestart();
        if (previousBackground != _preferences.OptimizedBackgroundDb) Save();
    }

    internal void InitializeWindow()
    {
        App.DiagnosticStage = "window sizing and tray";
        _window.ExtendsContentIntoTitleBar = true;
        _window.SetTitleBar(AppTitleBar);
        WindowSizing.Initialize(_window);
        AppWindow.SetIcon(Path.Combine(AppContext.BaseDirectory, "Assets", "Sukut.ico"));
        _tray = new TrayIcon(_window, Path.Combine(AppContext.BaseDirectory, "Assets"), ShowWindow, () => _ = ChangeMode(0), () => _ = Quit(), UpdateLifecycle);
        // Cancel before destruction, not from Window.Closed (which is too late
        // to reliably keep the host alive for close-to-tray).
        AppWindow.Closing += (sender, args) =>
        {
            if (_allowExit) return;
            args.Cancel = true;
            if (_quitting) return;
            DispatcherQueue.TryEnqueue(() => { if (_preferences.CloseToTray) HideToTray(); else _ = Quit(); });
        };
        AppWindow.Changed += (_, _) => UpdateLifecycle();
        Root.ActualThemeChanged += (_, _) => UpdateLifecycle();
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
                await _engine.Initialize(_root);
                await _engine.SetAmplification(Math.Clamp(_preferences.AmplificationDb, 0, 12));
                _setupEngineReady = true;
                UpdatePoll();
                await Poll();
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

    private static string FindRoot()
    {
        DirectoryInfo? parent = new(AppContext.BaseDirectory);
        for (int depth = 0; depth < 9 && parent != null; depth++, parent = parent.Parent)
            if (File.Exists(Path.Combine(parent.FullName, "sukut-package.json")) || File.Exists(Path.Combine(parent.FullName, "docs", "route.json"))) return parent.FullName;
        throw new InvalidOperationException("Sukut audio files are missing. Extract the whole package and keep its app and assets folders together.");
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
        PhoneNavItem.Content = L("Phone", "الهاتف"); HomeNavItem.Content = L("Home", "الرئيسية"); AboutNavItem.Content = L("About", "حول");
        foreach (var item in new[] { HomeNavItem, AboutNavItem, PhoneNavItem }) { ToolTipService.SetToolTip(item, item.Content); AutomationProperties.SetName(item, (string)item.Content); }
        if (Navigation.SettingsItem is NavigationViewItem settings)
        {
            settings.Content = L("Settings", "الإعدادات");
            AutomationProperties.SetAutomationId(settings, "SettingsNavItem");
            AutomationProperties.SetName(settings, (string)settings.Content);
            ToolTipService.SetToolTip(settings, settings.Content);
        }
        _tray?.SetLabels(L("Open Sukut", "فتح سُكوت"), L("OFF", "إيقاف"), L("Exit", "خروج"));
        if (HomePanel != null) { ApplyHomeTypography(); RenderHome(); }
        if (SettingsPanel != null) RenderSettings();
        if (AboutPanel != null) RenderAbout();
        if (SetupPanel != null) ApplySetupLanguage();
        RenderStatus(); RenderError();
        // Theme application can recreate the built-in Settings item after layout.
        DispatcherQueue.TryEnqueue(Microsoft.UI.Dispatching.DispatcherQueuePriority.Low, LocalizeSettingsItem);
        QueueTypography();
    }

    private void LocalizeSettingsItem()
    {
        if (Navigation.SettingsItem is not NavigationViewItem item) return;
        item.Content = L("Settings", "الإعدادات");
        AutomationProperties.SetAutomationId(item, "SettingsNavItem");
        AutomationProperties.SetName(item, (string)item.Content);
        ToolTipService.SetToolTip(item, item.Content);
    }

    private void HomePanel_Loaded(object sender, RoutedEventArgs args) { OutputButton.Flyout = null; FitPageViewport(); LocalizeSettingsItem(); ApplyHomeTypography(); RenderHome(); RenderStatus(); RenderError(); QueueTypography(); }
    private async void SettingsPanel_Loaded(object sender, RoutedEventArgs args)
    {
        FitPageViewport(); LocalizeSettingsItem(); RenderSettings();
        try { await LoadApps(); } catch (Exception error) { Error(error.Message); }
        FocusRequestedSourceApp();
        QueueTypography();
    }
    private void AboutPanel_Loaded(object sender, RoutedEventArgs args) { FitPageViewport(); AboutPanel.ChangeView(0, 0, null, true); RenderAbout(); StartBrand(); QueueTypography(); }
    private void AboutPanel_Unloaded(object sender, RoutedEventArgs args) => StopBrand();

    private void RenderHome()
    {
        if (HomePanel == null) return;
        RenderCableVolumeTip();
        RenderOverloadTip();
        HeaderSubtitle.Text = L("Clean audio from your apps", "صوت أنقى من تطبيقاتك");
        OutputLabel.Text = L("Audio output", "مخرج الصوت");
        BufferingLabel.Text = L("Audio buffering", "التخزين المؤقت للصوت");
        OutputFlyoutTitle.Text = L("Playback device", "جهاز التشغيل");
        OutputSourceAppTitle.Text = L("Source app", "التطبيق المصدر");
        AutomationProperties.SetName(OutputSourceAppChoice, OutputSourceAppTitle.Text);
        OutputFlyout.Placement = Arabic ? Microsoft.UI.Xaml.Controls.Primitives.FlyoutPlacementMode.BottomEdgeAlignedRight : Microsoft.UI.Xaml.Controls.Primitives.FlyoutPlacementMode.BottomEdgeAlignedLeft;
        BufferingFlyout.Placement = Arabic ? Microsoft.UI.Xaml.Controls.Primitives.FlyoutPlacementMode.BottomEdgeAlignedRight : Microsoft.UI.Xaml.Controls.Primitives.FlyoutPlacementMode.BottomEdgeAlignedLeft;
        ModeHeading.Text = L("Audio mode", "وضع الصوت");
        OffLabel.Text = L("OFF", "إيقاف");
        HalalLabel.Text = L("Music Filter", "مرشّح الموسيقى");
        OptimizedLabel.Text = L("Optimized Mode", "الوضع المحسّن");
        StrictLabel.Text = L("Strict Mode", "الوضع الصارم");
        AutomationProperties.SetName(OffCard, OffLabel.Text); AutomationProperties.SetName(HalalCard, HalalLabel.Text); AutomationProperties.SetName(OptimizedCard, OptimizedLabel.Text);
        AutomationProperties.SetName(StrictCard, StrictLabel.Text);
        bool previous = _applying; _applying = true;
        OutputList.ItemsSource = _outputs; OutputList.SelectedItem = SelectedOutput;
        RenderSourceAppChoices();
        ModeChoices.SelectedIndex = _mode;
        RenderBufferingSettings();
        RenderModeDetails(_mode);
        RenderEngineRow();
        _applying = previous;
        AutomationProperties.SetName(OutputButton, OutputLabel.Text + ", " + OutputValue.Text);
        AutomationProperties.SetName(BufferingButton, BufferingLabel.Text + ", " + BufferingValue.Text);
        SetEnabled(true);
    }

    private void RenderModeDetails(int mode)
    {
        if (ModeDetailTitle == null) return;
        var preset = AudioModePreset.ForMode(mode, _preferences.OptimizedBackgroundDb);
        ModeDetailTitle.Text = mode switch { 1 => L("Music Filter", "مرشّح الموسيقى"), 2 => L("Optimized Mode", "الوضع المحسّن"), 3 => L("Strict Mode", "الوضع الصارم"), _ => L("Original audio", "الصوت الأصلي") };
        ModeDetailTitle.Style = (Style)Application.Current.Resources[mode switch { 1 => "ModeHalalText", 2 => "ModeOptimizedText", 3 => "ModeStrictText", _ => "ModeOffText" }];
        ModeDetailDescription.Inlines.Clear();
        string lead = mode switch { 1 or 2 => L("Music muted. ", "الموسيقى مكتومة. "), 3 => L("Music and background muted. ", "الموسيقى والخلفية مكتومتان. "), _ => "" };
        ModeDetailDescription.Inlines.Add(new Microsoft.UI.Xaml.Documents.Run { Text = lead, FontWeight = Microsoft.UI.Text.FontWeights.SemiBold, Foreground = ModeDetailTitle.Foreground });
        ModeDetailDescription.Inlines.Add(new Microsoft.UI.Xaml.Documents.Run { Text = mode switch {
            1 => L("Keep speech and surrounding sounds.", "أبقِ الكلام والأصوات المحيطة."),
            2 => L("Adjust background sounds below the modes.", "اضبط أصوات الخلفية أسفل الأوضاع."),
            3 => L("Focus on speech. Some sound effects may disappear.", "ركّز على الكلام. قد تختفي بعض المؤثرات."),
            _ => L("Original audio through your selected device.", "الصوت الأصلي عبر الجهاز الذي اخترته.") } });
        string Gain(float db, bool muted) => muted ? L("Muted", "مكتوم") : (db > 0 ? "+" : "") + db.ToString("0", System.Globalization.CultureInfo.InvariantCulture) + " dB";
        OptimizedBackgroundPanel.Visibility = mode == 2 ? Visibility.Visible : Visibility.Collapsed;
        OptimizedBackgroundHeading.Text = L("Background level", "مستوى الخلفية");
        OptimizedBackgroundValue.Text = Gain(preset.BackgroundDb, preset.MuteBackground);
        AutomationProperties.SetName(OptimizedBackgroundSlider, OptimizedBackgroundHeading.Text);
        bool applying = _applying; _applying = true;
        if (!OptimizedBackgroundSlider.IsInteracting) OptimizedBackgroundSlider.Value = Math.Clamp(_preferences.OptimizedBackgroundDb, -20, 10);
        _applying = applying;
    }

    private void RenderSettings()
    {
        if (SettingsPanel == null) return;
        bool previous = _applying; _applying = true;
        SettingsHeading.Text = L("Settings", "الإعدادات");
        GeneralSettingsHeading.Text = L("General", "عام"); AudioSettingsHeading.Text = L("Audio", "الصوت"); HelpSettingsHeading.Text = L("Help", "المساعدة");
        RenderAmplification();
        RenderVolumeTools();
        LanguageCard.Header = L("Language", "اللغة"); ThemeCard.Header = L("App theme", "المظهر");
        TrayCard.Header = L("Keep running in system tray", "البقاء في منطقة الإشعارات");
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
        AboutVersion.Text = ReleaseVersion;
        DevelopedLabel.Text = L("Developed by", "تطوير"); FontCredit.Text = L("IBM Plex Sans Arabic · Readex Pro · Handjet · SIL OFL", "IBM Plex Sans Arabic · Readex Pro · Handjet · رخصة SIL OFL");
        RenderChangelog();
    }

    private void Navigation_SelectionChanged(NavigationView sender, NavigationViewSelectionChangedEventArgs args)
    {
        if (_applying || _setupVisible) return;
        int page = args.IsSettingsSelected ? 1 : args.SelectedItemContainer?.Tag as string == "about" ? 2 : args.SelectedItemContainer?.Tag as string == "phone" ? 3 : 0;
        // Let the invoking automation/input callback release its element first.
        DispatcherQueue.TryEnqueue(Microsoft.UI.Dispatching.DispatcherQueuePriority.Low, () => { if (!_setupVisible && !_quitting) Navigate(page); });
    }

    private void Navigate(int page)
    {
        if (page != 2) StopBrand();
        bool previous = _applying; _applying = true;
        _page = page;
        View.HomeLoaded = page == 0; View.SettingsLoaded = page == 1; View.AboutLoaded = page == 2; View.PhoneLoaded = page == 3;
        Navigation.SelectedItem = page == 0 ? HomeNavItem : page == 2 ? AboutNavItem : page == 3 ? PhoneNavItem : Navigation.SettingsItem;
        _applying = previous;
    }

    private void SetEnabled(bool force = false)
    {
        bool enabled = _initialized && !_command && (_state?.Busy ?? 0) == 0;
        if (force || _controlsEnabled != enabled)
        {
            _controlsEnabled = enabled;
            if (OutputButton != null) OutputButton.IsEnabled = enabled && !_openingOutput;
            if (OutputSourceAppChoice != null) OutputSourceAppChoice.IsEnabled = enabled;
            if (InputChoice != null) InputChoice.IsEnabled = enabled;
            if (CaptureChoice != null) CaptureChoice.IsEnabled = enabled;
            if (AppChoice != null) AppChoice.IsEnabled = enabled;
            if (RefreshButton != null) RefreshButton.IsEnabled = enabled;
            if (HomeBuffering != null) HomeBuffering.IsEnabled = enabled;
            if (SetupBuffering != null) SetupBuffering.IsEnabled = enabled;
            if (AmplificationSlider != null) AmplificationSlider.IsEnabled = enabled;
        }
        bool modesEnabled = _initialized && ModeSetupReady;
        if (RestartErrorButton != null) RestartErrorButton.IsEnabled = enabled && _mode != 0;
        if (RestartOverloadButton != null) RestartOverloadButton.IsEnabled = enabled && _mode != 0;
        if (OptimizedBackgroundSlider != null) OptimizedBackgroundSlider.IsEnabled = enabled;
        if (force || _modesEnabled != modesEnabled)
        {
            _modesEnabled = modesEnabled;
            if (ModeChoices != null) ModeChoices.IsEnabled = modesEnabled;
        }
    }

    private string StatusText() => _status switch
    {
        "Active" => _mode == 1 ? L("Music Filter", "مرشّح الموسيقى") : _mode == 3 ? L("Strict", "صارم") : L("Optimized", "محسّن"),
        "Preparing" => _command ? L("Switching", "جارٍ التبديل") : L("Preparing", "جارٍ التجهيز"),
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
        if (StatusHost != null) { VisualStateManager.GoToState(StatusHost, _status, false); StatusLabel.Text = _statusText; }
        _tray?.SetTip("سُكوت Sukut · " + _statusText);
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
            _initialized = _state.Initialized != 0; _setupScanned = _state.Busy == 0;
            UpdateEndpoints(_state); SetEnabled();
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
            _tray.SetActive(_state.State == 1 && _mode != 0);
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
        if (output != null) _preferences.Output = output.Id; if (source != null) _preferences.Source = source.Id; if (capture != null) _preferences.Capture = capture.Id;
        if (dirty) Save();
        if (HomePanel != null) RenderHome(); if (SettingsPanel != null) RenderSettings(); if (SetupPanel != null) RenderSetupOutput();
    }

    private async Task LoadApps()
    {
        var apps = await _engine.Apps();
        await _sourceAppIcons.Populate(apps);
        if (_quitting) return;
        _apps = apps; _apps.Insert(0, new SourceApp { Name = L("All system audio", "صوت النظام بالكامل") });
        if (!_apps.Exists(x => x.Pid == _preferences.SourcePid)) { _preferences.SourcePid = 0; Save(); }
        if (SettingsPanel != null) RenderSettings();
        else RenderSourceAppChoices();
        RenderCableVolumeTip();
    }

    private void RenderSourceAppChoices()
    {
        bool previous = _applying; _applying = true;
        try
        {
            if (_apps.Count > 0) _apps[0].Name = L("All system audio", "صوت النظام بالكامل");
            var selected = _apps.Find(x => x.Pid == _preferences.SourcePid);
            foreach (var picker in new Microsoft.UI.Xaml.Controls.Primitives.Selector?[] { AppChoice, OutputSourceAppChoice })
                if (picker != null) { picker.ItemsSource = null; picker.ItemsSource = _apps; picker.SelectedItem = selected; }
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
            Navigate(0); RenderCableVolumeTip(); SyncModeSelection();
            DispatcherQueue.TryEnqueue(() => CableVolumeTip?.StartBringIntoView());
            return false;
        }
        _command = true; _poll.Stop(); SetEnabled(); _rawError = ""; _errorOpen = false; RenderError(); Status(automatic ? "Reconnecting" : "Preparing");
        bool success = false;
        try
        {
            await _engine.Select(mode, _preferences.Source, _preferences.Capture, _preferences.Output, _preferences.SourcePid, Math.Clamp(_preferences.Buffering, 0, 2), _preferences.CableVolumeDisabledConfirmed, _preferences.OptimizedBackgroundDb, restart);
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
        if (ModeChoices == null) return;
        bool previous = _applying; _applying = true; ModeChoices.SelectedIndex = _mode; _applying = previous;
        RenderModeDetails(_mode);
    }
    private void Modes_SizeChanged(object sender, SizeChangedEventArgs args)
    {
        if (ModeChoices == null) return;
        // Measure the deferred content in DIPs, including the compact navigation
        // pane and page margins, rather than the top-level window width.
        double width = Math.Clamp(Math.Floor((args.NewSize.Width - 24) / 4), 96, 148);
        foreach (var card in new[] { OffCard, HalalCard, OptimizedCard, StrictCard })
            if (Math.Abs(card.Width - width) > .5 || double.IsNaN(card.Width)) card.Width = width;
    }
    private async void Modes_SelectionChanged(object sender, SelectionChangedEventArgs args)
    {
        if (_applying || ModeChoices == null) return;
        if (_command || !_initialized || !ModeSetupReady) { SyncModeSelection(); return; }
        int mode = ModeChoices.SelectedIndex;
        if (mode >= 0 && mode <= 3) { RenderModeDetails(mode); await ChangeMode(mode); }
    }
    private void RenderEngineRow()
    {
        if (OutputValue == null) return;
        var output = SelectedOutput;
        OutputGlyph.Glyph = output?.Glyph ?? "\uE7F6";
        OutputValue.Text = output?.Name ?? L("No device", "لا يوجد جهاز");
        BufferingValue.Text = _preferences.Buffering switch { 1 => L("Balanced", "متوازن"), 2 => L("Maximum", "الأكبر"), _ => L("Lowest", "الأقل") };
    }
    private bool _openingOutput;
    private async void OutputButton_Click(object sender, RoutedEventArgs args)
    {
        if (!_setupEngineReady || _command || _quitting || _openingOutput || _dropdownRefresh is { IsCompleted: false }) return;
        bool previous;
        _openingOutput = true;
        OutputButton.IsEnabled = false;
        try
        {
            _dropdownRefresh = _engine.RefreshDevices();
            var state = await _dropdownRefresh;
            if (_quitting) return;
            _state = state; _initialized = state.Initialized != 0; _setupScanned = state.Busy == 0;
            UpdateEndpoints(state); SetEnabled(true);
            await LoadApps();
            if (_quitting || !_visible || OutputButton?.IsLoaded != true) return;
            if (SetupPanel != null) UpdateSetupState();
            previous = _applying; _applying = true;
            OutputList.ItemsSource = _outputs; OutputList.SelectedItem = SelectedOutput;
            _applying = previous;
            RenderEngineRow();
            double contentWidth = AlignAudioFlyout(OutputFlyout, OutputButton);
            OutputFlyoutContent.Width = contentWidth;
            OutputFlyout.ShowAt(OutputButton);
        }
        catch (Exception error) { Error(error.Message); }
        finally { _openingOutput = false; if (!_quitting) SetEnabled(true); }
    }
    private double AlignAudioFlyout(Flyout flyout, Button button)
    {
        // Width includes the presenter's border and padding. Align both edges to
        // the trigger instead of allowing the caption's desired width to expand it.
        double width = Math.Max(120, button.ActualWidth);
        var style = new Style { TargetType = typeof(FlyoutPresenter) };
        style.Setters.Add(new Setter(FrameworkElement.WidthProperty, width));
        style.Setters.Add(new Setter(FrameworkElement.MinWidthProperty, 0d));
        style.Setters.Add(new Setter(FrameworkElement.MaxWidthProperty, width));
        style.Setters.Add(new Setter(Control.PaddingProperty, new Thickness(12)));
        style.Setters.Add(new Setter(Control.BorderThicknessProperty, new Thickness(1)));
        style.Setters.Add(new Setter(Control.CornerRadiusProperty, new CornerRadius(8)));
        style.Setters.Add(new Setter(Control.HorizontalContentAlignmentProperty, HorizontalAlignment.Stretch));
        // Popups have a separate visual root. Do not rely on Root inheritance
        // for Arabic direction, font or text shaping.
        style.Setters.Add(new Setter(FrameworkElement.FlowDirectionProperty, Arabic ? FlowDirection.RightToLeft : FlowDirection.LeftToRight));
        style.Setters.Add(new Setter(FrameworkElement.LanguageProperty, Arabic ? "ar" : "en"));
        style.Setters.Add(new Setter(Control.FontFamilyProperty, FontFamily));
        flyout.FlyoutPresenterStyle = style;
        return Math.Max(0, width - 26);
    }
    private void BufferingFlyout_Opening(object sender, object args)
    {
        double contentWidth = AlignAudioFlyout(BufferingFlyout, BufferingButton);
        HomeBuffering.MinWidth = 0;
        HomeBuffering.MaxWidth = HomeBuffering.Width = contentWidth;
        ArabicTypography.Apply(BufferingFlyout.Content, Arabic);
    }
    private async void OutputList_Changed(object sender, SelectionChangedEventArgs args)
    {
        if (_applying || OutputList?.SelectedItem is not Endpoint endpoint) return;
        OutputFlyout?.Hide();
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
        OutputFlyout?.Hide();
        _preferences.SourcePid = source.Pid; Save(); RenderSourceAppChoices(); RenderCableVolumeTip(); if (_mode != 0) await ChangeMode(_mode, true);
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
    private void UpdateLifecycle() { WindowSizing.UpdateMinimum(_window); UpdatePoll(); if (_visible && !Minimized && _page == 2 && !_setupVisible && AboutPanel != null) StartBrand(); else StopBrand(); }
    private void ShowWindow()
    {
        _visible = true;
        if (AppWindow.Presenter is OverlappedPresenter { State: OverlappedPresenterState.Minimized } presenter) presenter.Restore();
        AppWindow.Show(); _window.Activate();
        if (_setupVisible) View.SetupLoaded = true; else Navigate(_page);
        UpdateLifecycle(); if (_setupEngineReady) _ = Poll();
    }
    private void HideToTray()
    {
        if (_quitting || !_visible) return;
        _visible = false; AppWindow.Hide(); StopBrand();
        View.HomeLoaded = View.SettingsLoaded = View.AboutLoaded = View.PhoneLoaded = View.SetupLoaded = false;
        // Source-app process lists can be large and are only needed in Settings.
        _apps.Clear();
        UpdatePoll();
    }
    private async Task Quit()
    {
        if (_quitting) return;
        _quitting = true; _poll.Stop(); CancelAutomaticRestart(); StopBrand();
        // Exit is a teardown command, not another filtering/route-selection
        // request. Old capture faults and a closed source app cannot veto it.
        try { await _settings.Flush(_preferences).WaitAsync(TimeSpan.FromSeconds(2)); }
        catch (Exception error) { RecordShutdownError(error.Message); }
        bool timedOut = false;
        try { await _engine.DisposeAsync().AsTask().WaitAsync(TimeSpan.FromSeconds(10)); }
        catch (Exception error) { timedOut = true; RecordShutdownError(error.Message); }
        try { await _settings.DisposeAsync().AsTask().WaitAsync(TimeSpan.FromSeconds(2)); }
        catch (Exception error) { RecordShutdownError(error.Message); }
        _tray.Dispose(); _allowExit = true; (Application.Current as App)?.ReleaseInstance();
        // If a driver refuses cancellation, process exit lets the independent
        // journal guard finish restoration. Never reopen an Attention loop.
        if (timedOut) Environment.Exit(0);
        _window.Close(); Application.Current.Exit();
    }
    private void RecordShutdownError(string error)
    {
        try { Directory.CreateDirectory(Path.Combine(_root, "runtime")); File.AppendAllText(Path.Combine(_root, "runtime", "shutdown-last-error.txt"), error + Environment.NewLine); }
        catch { /* A failed optional diagnostic must not prevent Exit. */ }
    }
}
