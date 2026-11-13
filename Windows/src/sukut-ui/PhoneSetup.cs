using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Documents;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Media.Imaging;

namespace Sukut;

public sealed partial class ShellView
{
    private const int PhoneStepCount = 5;
    private const double PhoneIconSize = 24, PhoneSmallIconSize = 16;
    private enum PhoneTask { Choices, FullSetup, UsbHelp, ActivateWorker, SmartSukut }
    private PhoneTask _phoneTask;
    private readonly PhoneTools _phoneTools = new();
    private List<PhoneDevice> _phoneKnown = [];
    private ComboBox? _phoneDevices;
    private InfoBar? _phoneMessage;
    private StackPanel? _phoneActions;
    private StackPanel? _phonePageHost;
    private TextBlock? _phoneStepLabel;
    private Button? _phoneBack, _phoneNext;
    private PhoneDevice? _phoneSelected;
    private int _phoneStep;
    private bool _phoneBusy;
    private ProgressBar? _phoneProgress;
    private TextBlock? _phoneProgressLabel;

    private static Style PhoneText(string key) => (Style)Application.Current.Resources[key];
    private Brush? PhoneBrush(string key)
    {
        // An unattached TextBlock resolves ThemeResource against the app default,
        // which can be dark while this window explicitly uses Light.
        string resource = key switch
        {
            "AccentText" => "SukutAccentBrush", "ModeOffText" => "ModeOffBrush",
            "ModeHalalText" => "ModeHalalBrush", "ModeOptimizedText" => "ModeOptimizedBrush",
            "ModeStrictText" => "ModeStrictBrush", "GainRaised" => "GainRaisedBrush",
            "GainMuted" => "GainMutedBrush", "GainReduced" => "GainReducedBrush", _ => ""
        };
        string theme = new Windows.UI.ViewManagement.AccessibilitySettings().HighContrast ? "HighContrast"
            : Root.ActualTheme == ElementTheme.Light ? "Light" : "Default";
        var palette = (ResourceDictionary)Application.Current.Resources.ThemeDictionaries[theme];
        return resource.Length != 0 ? (Brush)palette[resource] : new TextBlock { Style = PhoneText(key) }.Foreground;
    }
    private static TextBlock PhoneBody(string text) => new() { Text = text, TextWrapping = TextWrapping.Wrap };

    private void PhonePanel_Loaded(object sender, RoutedEventArgs args) { FitPageViewport(); RenderPhone(); _=RefreshPhoneQuietly(); }
    private void PhonePanel_Unloaded(object sender, RoutedEventArgs args)
    {
        if (PhonePanel != null && !ReferenceEquals(sender, PhonePanel)) return;
        // x:Load releases the page; these managed fields must not retain its pictures/controls.
        _phonePageHost?.Children.Clear();
        _phonePageHost = null; _phoneActions = null; _phoneDevices = null; _phoneMessage = null;
        _phoneProgress = null; _phoneProgressLabel = null;
        _phoneStepLabel = null; _phoneBack = null; _phoneNext = null;
        _phoneNameText=_phoneConnectionText=_phoneVersionText=null;_phoneConnectionIcon=null;_phoneRefreshButton=null;
    }

    private void RenderPhone()
    {
        if (PhoneContent == null) return;
        PhonePanel.FontFamily = (FontFamily)Application.Current.Resources[Arabic ? "SukutArabicFontFamily" : "ContentControlThemeFontFamily"];
        PhoneContent.Children.Clear();
        // The shell provides the title; step navigation lives only in the footer.
        // Keep device status out of the guide; it has a compact, fixed strip below.
        PhoneConnectionHost.Child = BuildPhoneConnectionCard();
        _phonePageHost = new StackPanel { Spacing = 10 };
        PhoneContent.Children.Add(_phonePageHost);

        var footer = new Grid { ColumnSpacing = 8, Margin = new Thickness(0, 6, 0, 0) };
        footer.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        footer.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        footer.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        _phoneBack = new Button { MinWidth = 110, MinHeight = 44, CornerRadius = new CornerRadius(4) };
        SetPhoneButtonLabel(_phoneBack, L("\uE72B", "\uE72A"), L("Back", "السابق"));
        Microsoft.UI.Xaml.Automation.AutomationProperties.SetAutomationId(_phoneBack, "PhonePrevious");
        _phoneBack.Click += (_, _) => PhoneGoBack();
        _phoneStepLabel = new TextBlock { HorizontalAlignment = HorizontalAlignment.Center, VerticalAlignment = VerticalAlignment.Center, Style = PhoneText("CaptionTextBlockStyle") };
        _phoneNext = new Button { MinWidth = 130, MinHeight = 44, CornerRadius = new CornerRadius(4), Style = (Style)Application.Current.Resources["AccentButtonStyle"] };
        Microsoft.UI.Xaml.Automation.AutomationProperties.SetAutomationId(_phoneNext, "PhoneNext");
        _phoneNext.Click += (_, _) => { if (_phoneBusy) return; if (_phoneStep < PhoneStepCount - 1) { _phoneStep++; RenderPhonePage(animate: true); } else SelectPhoneTask(PhoneTask.Choices); };
        Grid.SetColumn(_phoneStepLabel, 1); Grid.SetColumn(_phoneNext, 2);
        footer.Children.Add(_phoneBack); footer.Children.Add(_phoneStepLabel); footer.Children.Add(_phoneNext);
        PhoneFooter.Child = footer;
        RenderPhonePage();
    }

    private void PhoneGoBack()
    {
        if (_phoneBusy) return;
        if (_phoneTask == PhoneTask.FullSetup && _phoneStep > 0) { _phoneStep--; RenderPhonePage(animate: true); }
        else SelectPhoneTask(PhoneTask.Choices);
    }

    private void SelectPhoneTask(PhoneTask task)
    {
        if (_phoneBusy) return;
        _phoneTask = task;
        if (task == PhoneTask.FullSetup) _phoneStep = 0;
        RenderPhonePage(animate: true);
    }

    private void PhoneChoicesPage()
    {
        _phoneActions = new StackPanel { Spacing = 14 };
        _phonePageHost!.Children.Add(_phoneActions);
        PhoneChoice(PhoneTask.FullSetup,"\uE713","ModeOptimizedText",L("Full setup","الإعداد الكامل"),
            L("Connect, install and activate. We’ll guide you through each step.","توصيل وتثبيت وتفعيل. نرشدك في كل خطوة."),"PhoneFullSetupOption");
        var tools=new StackPanel { Spacing=6 };
        tools.Children.Add(new TextBlock { Text=L("Tools","الأدوات"), Style=PhoneText("BodyStrongTextBlockStyle"), Margin=new Thickness(4,0,4,2) });
        PhoneChoice(PhoneTask.ActivateWorker,"\uE90F","GainRaised",L("Activate Sukut","تفعيل سُكوت"),
            L("Activate the audio worker over USB.","تفعيل عامل الصوت عبر USB."),"PhoneActivateWorkerOption",tools);
        PhoneChoice(PhoneTask.SmartSukut,"\uE895","ModeOptimizedText",L("Install or update Sukut","تثبيت سُكوت أو تحديثه"),
            L("Install, update or open it. Keep your settings.","تثبيت التطبيق أو تحديثه أو فتحه، مع الاحتفاظ بإعداداتك."),"PhoneSmartSukutOption",tools);
        _phoneActions.Children.Add(new Border { Style=(Style)Application.Current.Resources["SetupGuideCard"], Padding=new Thickness(8), Child=tools });
        var connection=new HyperlinkButton { Content=L("USB connection help","المساعدة في اتصال USB"), HorizontalAlignment=HorizontalAlignment.Left, Padding=new Thickness(4,6,4,6) };
        connection.Click+=(_,_)=>SelectPhoneTask(PhoneTask.UsbHelp);
        _phoneActions.Children.Add(connection);
    }

    private void PhoneSection(string title) => _phoneActions!.Children.Add(new TextBlock {
        Text = title, Style = PhoneText("BodyStrongTextBlockStyle"), Margin = new Thickness(4, 12, 0, 0), TextWrapping = TextWrapping.Wrap });

    private void PhoneChoice(PhoneTask task, string glyph, string color, string title, string detail, string id, Panel? host = null)
    {
        var button = new Button { HorizontalAlignment = HorizontalAlignment.Stretch, HorizontalContentAlignment = HorizontalAlignment.Stretch, Padding = new Thickness(12,10,12,10), MinHeight = task == PhoneTask.FullSetup ? 88 : 72, VerticalAlignment=VerticalAlignment.Stretch, VerticalContentAlignment=VerticalAlignment.Center, CornerRadius = new CornerRadius(8) };
        var row = new Grid { ColumnSpacing = 12 };
        row.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        row.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        row.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        var icon = new FontIcon { Glyph = glyph, FontSize = PhoneIconSize, Width = PhoneIconSize, Height = PhoneIconSize, VerticalAlignment = VerticalAlignment.Center, Foreground = PhoneBrush(color) };
        var words = new StackPanel { Spacing = 2 };
        words.Children.Add(new TextBlock { Text = title, Style = PhoneText(color), FontSize = task == PhoneTask.FullSetup ? 20 : 16, TextWrapping = TextWrapping.Wrap });
        words.Children.Add(new TextBlock { Text = detail, Style = PhoneText("CaptionTextBlockStyle"), TextWrapping = TextWrapping.Wrap });
        var arrow = new FontIcon { Glyph = L("\uE76C", "\uE76B"), FontSize = PhoneSmallIconSize, VerticalAlignment = VerticalAlignment.Center };
        Grid.SetColumn(words, 1); Grid.SetColumn(arrow, 2);
        row.Children.Add(icon); row.Children.Add(words); row.Children.Add(arrow); button.Content = row;
        Microsoft.UI.Xaml.Automation.AutomationProperties.SetName(button, title);
        Microsoft.UI.Xaml.Automation.AutomationProperties.SetAutomationId(button, id);
        button.Click += (_, _) => SelectPhoneTask(task);
        if (host is Grid grid) Grid.SetColumn(button, grid.Children.Count);
        (host ?? _phoneActions!).Children.Add(button);
    }

    private void RenderPhonePage(bool animate = false)
    {
        if (_phonePageHost == null) return;
        _phoneStep = Math.Clamp(_phoneStep, 0, PhoneStepCount - 1);
        _phonePageHost.Children.Clear();
        _phoneMessage = null; _phoneActions = null;
        _phoneProgress = null; _phoneProgressLabel = null;
        PhoneScroll.ChangeView(null, 0, null, true);
        PhoneFooter.Visibility = _phoneTask == PhoneTask.Choices ? Visibility.Collapsed : Visibility.Visible;
        if (_phoneTask == PhoneTask.Choices) PhoneChoicesPage();
        else if (_phoneTask == PhoneTask.UsbHelp) PhoneUsbHelpPage();
        else if (_phoneTask == PhoneTask.ActivateWorker) PhoneInstallPage();
        else if (_phoneTask == PhoneTask.SmartSukut) PhoneSukutPage();
        else switch (_phoneStep)
        {
            case 0: PhoneDeveloperPage(); break;
            case 1: PhoneDebuggingPage(); break;
            case 2: PhoneSukutPage(); break;
            case 3: PhoneInstallPage(); break;
            default: PhoneAllowPage(); break;
        }
        if (_phoneStepLabel != null) _phoneStepLabel.Text = _phoneTask == PhoneTask.FullSetup ? L($"Step {_phoneStep + 1} of {PhoneStepCount}", $"الخطوة {_phoneStep + 1} من {PhoneStepCount}") : "";
        if (_phoneNext != null) { _phoneNext.Visibility = _phoneTask == PhoneTask.FullSetup ? Visibility.Visible : Visibility.Collapsed; _phoneNext.Content = _phoneStep == PhoneStepCount - 1 ? L("Done", "تمّ") : L("Next", "التالي"); }
        SetPhoneActionsEnabled(!_phoneBusy);
        ArabicTypography.Apply(PhonePanel, Arabic);
        QueueTypography();
        if(animate) AnimatePageContent(_phonePageHost);
    }

    private StackPanel PhonePage(string heading)
    {
        var page = new StackPanel { Spacing = 12 };
        var title = new TextBlock { Text = heading, FontSize = 24, FontWeight = Microsoft.UI.Text.FontWeights.SemiBold, TextWrapping = TextWrapping.Wrap, Margin = new Thickness(0, 2, 0, 4) };
        Microsoft.UI.Xaml.Automation.AutomationProperties.SetHeadingLevel(title, Microsoft.UI.Xaml.Automation.Peers.AutomationHeadingLevel.Level2);
        page.Children.Add(title);
        _phonePageHost!.Children.Add(page);
        return page;
    }

    private TextBlock PhoneRich(params (string Text, string? Color)[] parts)
    {
        var block = new TextBlock { TextWrapping = TextWrapping.Wrap };
        foreach (var (text, color) in parts)
        {
            var run = new Run { Text = text };
            if (color != null)
            {
                run.FontWeight = Microsoft.UI.Text.FontWeights.SemiBold;
                if (PhoneBrush(color) is { } mark) run.Foreground = mark;
            }
            block.Inlines.Add(run);
        }
        return block;
    }

    private StackPanel PhoneBullets(params (string Glyph, string Color, string Title, string Detail)[] items)
    {
        var list = new StackPanel { Spacing = 10 };
        foreach (var item in items)
        {
            var row = new Grid { ColumnSpacing = 10 };
            row.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
            row.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
            var icon = new FontIcon { Glyph = item.Glyph, FontSize = PhoneIconSize, Width = PhoneIconSize, Height = PhoneIconSize, VerticalAlignment = VerticalAlignment.Center };
            if (PhoneBrush(item.Color) is { } mark) icon.Foreground = mark;
            var text = new StackPanel { Spacing = 2 };
            text.Children.Add(new TextBlock { Text = item.Title, FontSize = 16, Style = PhoneText(item.Color), TextWrapping = TextWrapping.Wrap });
            text.Children.Add(new TextBlock { Text = item.Detail, FontSize = 14, Style = PhoneText("CaptionTextBlockStyle"), TextWrapping = TextWrapping.Wrap });
            Grid.SetColumn(text, 1);
            row.Children.Add(icon); row.Children.Add(text);
            list.Children.Add(row);
        }
        return list;
    }

    private Border PhoneNote(string color, string text)
    {
        var border = new Border { Padding = new Thickness(12), CornerRadius = new CornerRadius(8), BorderThickness = new Thickness(1) };
        if (PhoneBrush(color) is { } mark) border.BorderBrush = mark;
        var row = new Grid { ColumnSpacing = 10 };
        row.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        row.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        var icon = new FontIcon { Glyph = "\uE946", FontSize = PhoneIconSize, Width = PhoneIconSize, Height = PhoneIconSize, VerticalAlignment = VerticalAlignment.Center };
        if (PhoneBrush(color) is { } mark2) icon.Foreground = mark2;
        var body = PhoneBody(text);
        Grid.SetColumn(body, 1);
        row.Children.Add(icon); row.Children.Add(body);
        border.Child = row;
        return border;
    }

    private Expander PhonePicture(string picture)
    {
        var expander = new Expander { Header = picture.Contains("developer") ? L("Show developer options", "عرض صورة خيارات المطوّر") : L("Show USB debugging", "عرض صورة تصحيح USB"), HorizontalAlignment = HorizontalAlignment.Stretch,
            HorizontalContentAlignment = HorizontalAlignment.Stretch };
        expander.Expanding += (_, _) => expander.Content = new Image {
            Source = new BitmapImage(new Uri("ms-appx:///Assets/Setup/" + picture)) { DecodePixelWidth = 520 },
            MaxHeight = 330, Stretch = Stretch.Uniform, HorizontalAlignment = HorizontalAlignment.Center };
        expander.Collapsed += (_, _) => expander.Content = null;
        return expander;
    }

    private void PhoneUsbHelpPage()
    {
        var page = PhonePage(L("Connect your phone", "توصيل هاتفك"));
        page.Children.Add(PhoneBullets(("\uE713", "ModeOptimizedText", L("1 · Enable developer options", "١ · تفعيل خيارات المطوّر"),
            L("Settings → About phone → tap Build number seven times. On Xiaomi, tap OS version.", "الإعدادات ← حول الهاتف ← اضغط رقم الإصدار سبع مرات. في شاومي اضغط إصدار النظام."))));
        page.Children.Add(PhonePicture("phone-developer-options.png"));
        page.Children.Add(PhoneBullets(("\uE72E", "ModeOptimizedText", L("2 · Enable USB debugging", "٢ · تفعيل تصحيح USB"),
            L("Open Developer options and turn on USB debugging.", "افتح خيارات المطوّر وشغّل تصحيح USB."))));
        page.Children.Add(PhonePicture("phone-usb-debugging.png"));
        page.Children.Add(PhoneBullets(("\uE73E", "ModeHalalText", L("3 · Connect and approve", "٣ · التوصيل والموافقة"),
            L("Use a USB data cable, unlock the phone and approve this PC on the phone.", "استخدم كابل USB للبيانات، وافتح قفل الهاتف ووافق على اتصال هذا الحاسوب."))));
        PhoneToolActions(page, "Check connection", "التحقّق من الاتصال", _ => Task.FromResult(L("Phone connected. Install Sukut or activate its audio worker.", "الهاتف متصل. ثبّت سُكوت أو فعّل عامل الصوت.")), "\uE721");
    }

    private void PhoneOverviewPage()
    {
        var page = PhonePage(L("What you need", "ما تحتاجه"));
        page.Children.Add(PhoneBody(L("Sukut uses its own audio worker on your phone. Install the app, then activate it over USB. No root or separate Android app is needed.", "يستخدم سُكوت عامل صوت خاصًا به في هاتفك. ثبّت التطبيق، ثم فعّله عبر USB؛ لا تحتاج إلى روت أو تطبيق أندرويد آخر.")));
        page.Children.Add(PhoneBullets(
            ("\uE945", "ModeOptimizedText", L("A USB data cable", "كابل USB للبيانات"), L("Unlock your phone and approve this PC.", "افتح قفل الهاتف ووافق على اتصال هذا الحاسوب.")),
            ("\uE774", "ModeHalalText", L("Internet, once", "إنترنت لمرة واحدة"), L("Only the official Google USB tools need downloading.", "يلزم تنزيل أدوات USB الرسمية من Google فقط."))));
        page.Children.Add(PhoneNote("ModeHalalText", L("After activation you can unplug USB. Activate again after a phone restart, an app update or if Android ends the worker.", "بعد التفعيل يمكنك فصل USB. أعد التفعيل بعد إعادة تشغيل الهاتف أو تحديث التطبيق أو إنهاء أندرويد لعامل الصوت.")));
    }

    private void PhoneDeveloperPage()
    {
        var page = PhonePage(L("Enable developer options", "تفعيل خيارات المطوّر"));
        page.Children.Add(PhoneBullets(
            ("\uE713", "AccentText", L("Open Settings → About phone", "افتح الإعدادات ← حول الهاتف"), L("Find Build number. On Xiaomi, use OS version.", "ابحث عن رقم الإصدار. في شاومي استخدم إصدار النظام.")),
            ("\uE73E", "GainRaised", L("Tap seven times", "اضغط سبع مرات"), L("Enter your phone PIN if asked. Then open Developer options.", "أدخل رمز قفل هاتفك إذا طُلب، ثم افتح خيارات المطوّر."))));
        page.Children.Add(PhonePicture("phone-developer-options.png"));
        page.Children.Add(PhoneNote("ModeOptimizedText", L("You will see a warning that these options are for developers. That is normal.", "ستظهر رسالة تحذير بأن هذه الخيارات للمطوّرين. هذا طبيعي.")));
    }

    private void PhoneDebuggingPage()
    {
        var page = PhonePage(L("Allow USB debugging", "السماح بتصحيح USB"));
        page.Children.Add(PhoneBullets(
            ("\uE713", "AccentText", L("Turn on USB debugging", "شغّل تصحيح USB"), L("You’ll find it in Developer options.", "تجده في خيارات المطوّر.")),
            ("\uE88E", "GainRaised", L("Connect and allow this PC", "وصّل الهاتف واسمح لهذا الحاسوب"), L("Use a USB data cable. Keep the phone unlocked and tap Allow.", "استخدم كابل USB للبيانات، وأبقِ الهاتف مفتوحًا ثم اضغط السماح."))));
        page.Children.Add(PhonePicture("phone-usb-debugging.png"));
        page.Children.Add(PhoneNote("ModeOptimizedText", L("If installation is blocked, also allow Install via USB on the phone.", "إذا حُظر التثبيت، اسمح أيضًا بـ«التثبيت عبر USB» في الهاتف.")));
    }

    private void PhoneInstallPage()
    {
        var page = PhonePage(L("Activate Sukut", "تفعيل سُكوت"));
        page.Children.Add(PhoneBody(L("Keep your phone unlocked and activate Sukut. Wait for Audio worker ready, then choose a mode and press Start on the phone.", "أبقِ الهاتف مفتوحًا وفعّل سُكوت. انتظر ظهور «عامل الصوت جاهز»، ثم اختر وضعًا واضغط التشغيل في الهاتف.")));
        PhoneToolActions(page, "Activate Sukut", "تفعيل سُكوت", device => _phoneTools.ActivateSukut(device), "\uE768");
        page.Children.Add(PhoneNote("AccentText", L("Activation does not start audio. Press Start in Sukut on your phone. Activate again after a phone restart or update.", "التفعيل لا يشغّل الصوت. اضغط زر التشغيل في سُكوت على هاتفك. أعد التفعيل بعد إعادة تشغيل الهاتف أو تحديث التطبيق.")));
        var stop = PhoneActionButton("Deactivate worker", "إلغاء تفعيل عامل الصوت", async () => await _phoneTools.DeactivateSukut(await GetPhone()), "\uE71A");
        _phoneActions!.Children.Add(stop);
    }

    private void PhoneAllowPage()
    {
        var page = PhonePage(L("Ready — and easy next time", "جاهز، وبخطوات أسهل لاحقًا"));
        page.Children.Add(PhoneBody(L("On the phone, choose a mode and press Start on Home.", "في الهاتف، اختر وضعًا واضغط زر التشغيل في الرئيسية.")));
        page.Children.Add(PhoneBullets(
            ("\uE713", "AccentText", L("When activation is needed again", "عندما تحتاج إلى التفعيل مجددًا"), L("Enable USB debugging in Android Settings, then connect the USB cable.", "فعّل تصحيح USB من إعدادات أندرويد، ثم وصّل كابل USB.")),
            ("\uE768", "GainRaised", L("Activate Sukut", "فعّل سُكوت"), L("Choose Phone → Activate Sukut here. Keep debugging enabled if turning it off stops the worker.", "اختر هنا الهاتف ← تفعيل سُكوت. أبقِ التصحيح مفعّلًا إذا كان إيقافه يوقف عامل الصوت."))));
        page.Children.Add(PhoneNote("ModeOptimizedText", L("Repeat activation after a phone restart, an app update or if Android ends the worker. The shortcut does not enable debugging automatically.", "أعد التفعيل بعد إعادة تشغيل الهاتف أو تحديث التطبيق أو إنهاء أندرويد لعامل الصوت. لا يفعّل الاختصار تصحيح الأخطاء تلقائيًا.")));
    }

    private void PhoneSukutPage()
    {
        var page=PhonePage(L("Install or update Sukut","تثبيت سُكوت أو تحديثه"));
        page.Children.Add(PhoneBody(L("Your settings and newer versions are kept.","نحتفظ بإعداداتك والنسخ الأحدث.")));
        PhoneToolActions(page,"Set up Sukut","إعداد سُكوت",device=>_phoneTools.SetupSukut(device),"\uE895");
    }

    private Button PhoneToolActions(StackPanel page, string en, string ar, Func<PhoneDevice, Task<string>> action, string? glyph = null)
    {
        _phoneActions = new StackPanel { Spacing = 12 };
        var primary = PhoneActionButton(en, ar, async () => await action(await GetPhone()), glyph);
        primary.Style = (Style)Application.Current.Resources["AccentButtonStyle"];
        primary.MinHeight = 52; primary.FontSize = 16; primary.Padding = new Thickness(16, 12, 16, 12);
        _phoneActions.Children.Add(primary);
        page.Children.Add(_phoneActions);
        _phoneProgress = new ProgressBar { Visibility = Visibility.Collapsed, IsIndeterminate = true };
        _phoneProgressLabel = PhoneBody("");
        _phoneProgressLabel.Visibility = Visibility.Collapsed;
        page.Children.Add(_phoneProgress); page.Children.Add(_phoneProgressLabel);
        _phoneMessage = new InfoBar { IsOpen = false, IsClosable = false, Severity = InfoBarSeverity.Informational };
        page.Children.Add(_phoneMessage);
        SetPhoneActionsEnabled(!_phoneBusy);
        return primary;
    }

    private IProgress<PhoneTransfer> PhoneDownloadProgress() => new Progress<PhoneTransfer>(transfer =>
    {
        if (!_phoneBusy || _phoneProgress == null) return;
        _phoneProgress.Visibility = _phoneProgressLabel!.Visibility = Visibility.Visible;
        _phoneProgress.IsIndeterminate = transfer.Total is not > 0;
        double received = transfer.Received / 1048576d;
        string item = L("USB tools", "أدوات USB");
        if (transfer.Total is > 0)
        {
            _phoneProgress.Value = Math.Clamp(100d * transfer.Received / transfer.Total.Value, 0, 100);
            _phoneProgressLabel.Text = L($"Downloading {item} · {received:0.0} / {transfer.Total / 1048576d:0.0} MB", $"تنزيل {item} · {received:0.0} / {transfer.Total / 1048576d:0.0} م.ب");
        }
        else _phoneProgressLabel.Text = L($"Downloading {item} · {received:0.0} MB", $"تنزيل {item} · {received:0.0} م.ب");
    });

    private async Task<PhoneDevice> GetPhone()
    {
        if (!_phoneTools.AdbReady) await _phoneTools.Prepare(PhoneDownloadProgress());
        await RefreshPhoneDevices();
        var device = PhoneSelected();
        if (device.State != "device") throw new IOException(device.State == "unauthorized"
            ? L("Allow USB debugging on the phone, then try again.", "وافق على تصحيح USB في الهاتف، ثم أعد المحاولة.")
            : L("The phone is offline. Reconnect the USB cable.", "الهاتف غير متصل. أعد توصيل كابل USB."));
        if (_phoneProgress != null) _phoneProgress.Visibility = Visibility.Collapsed;
        if (_phoneProgressLabel != null) _phoneProgressLabel.Visibility = Visibility.Collapsed;
        return device;
    }

    private static void SetPhoneButtonLabel(Button button, string glyph, string text)
    {
        var content = new Grid { ColumnSpacing = 8, HorizontalAlignment = HorizontalAlignment.Stretch };
        content.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        content.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        content.Children.Add(new FontIcon { Glyph = glyph, FontSize = PhoneIconSize, Width = PhoneIconSize, Height = PhoneIconSize, VerticalAlignment = VerticalAlignment.Center });
        var label = PhoneBody(text); label.VerticalAlignment = VerticalAlignment.Center; Grid.SetColumn(label, 1);
        content.Children.Add(label); button.Content = content; button.HorizontalContentAlignment = HorizontalAlignment.Stretch;
        Microsoft.UI.Xaml.Automation.AutomationProperties.SetName(button, text);
    }

    private Button PhoneActionButton(string en, string ar, Func<Task<string>> action, string? glyph = null)
    {
        var button = new Button { Content = L(en, ar), MinHeight = 44, Padding = new Thickness(14, 10, 14, 10), CornerRadius = new CornerRadius(4), HorizontalAlignment = HorizontalAlignment.Stretch };
        if (glyph != null) SetPhoneButtonLabel(button, glyph, L(en, ar));
        button.Click += async (_, _) => await PhoneAction(action);
        return button;
    }

    private ComboBox PhoneDevicePicker()
    {
        _phoneDevices = new ComboBox { HorizontalAlignment = HorizontalAlignment.Stretch, DisplayMemberPath = "Label", PlaceholderText = L("Choose phone", "اختر الهاتف"), MaxDropDownHeight = 240, ItemsSource = _phoneKnown, SelectedItem = _phoneSelected };
        _phoneDevices.SelectionChanged += async (_, _) => {
            if (_applyingPhone || _phoneDevices?.SelectedItem is not PhoneDevice device) return;
            _phoneSelected = device;UpdatePhoneConnectionCard();await ReadPhoneInfo(device);

        };
        return _phoneDevices;
    }

    private PhoneDevice PhoneSelected() => _phoneDevices?.SelectedItem as PhoneDevice ?? _phoneSelected ?? throw new IOException(L("Select your phone first.", "اختر هاتفك أولًا."));

    private async Task<string> RefreshPhoneDevicesCore()
    {
        if (!_phoneTools.AdbReady) await _phoneTools.Prepare(PhoneDownloadProgress());
        var devices = await _phoneTools.Devices();
        _phoneKnown = devices;
        _phoneSelected = PhoneTools.SelectDevice(devices, _phoneSelected?.Serial);
        _applyingPhone=true;
        if (_phoneDevices != null) { _phoneDevices.ItemsSource = null; _phoneDevices.ItemsSource = _phoneKnown; _phoneDevices.SelectedItem = _phoneSelected; }
        _applyingPhone=false;UpdatePhoneConnectionCard();
        if(_phoneSelected is {} selected)await ReadPhoneInfo(selected);
        if (devices.Count == 0) throw new IOException(L("Connect your phone by USB and allow debugging.", "وصّل الهاتف عبر USB واسمح بتصحيح USB."));
        return L("Phone connected.", "الهاتف متصل.");
    }

    private void SetPhoneActionsEnabled(bool enabled)
    {
        if (_phoneActions != null) { _phoneActions.IsHitTestVisible = enabled; _phoneActions.Opacity = enabled ? 1 : .6; }
        if(_phoneRefreshButton!=null)_phoneRefreshButton.IsEnabled=enabled;
        if(_phoneDevices!=null)_phoneDevices.IsEnabled=enabled;
        if (_phoneBack != null) _phoneBack.IsEnabled = enabled;
        if (_phoneNext != null) _phoneNext.IsEnabled = enabled;
    }
    private async Task PhoneAction(Func<Task<string>> action)
    {
        if (_phoneBusy) return;
        _phoneBusy = true;
        SetPhoneActionsEnabled(false);
        var message = _phoneMessage;
        if (message != null) { message.IsOpen = true; message.Severity = InfoBarSeverity.Informational; message.Message = L("Working… Check the phone for prompts.", "جارٍ التنفيذ… تحقّق من طلبات الهاتف."); }
        try { string result = await action(); if (message != null) { message.Severity = result == PhoneTools.ComputerShortcutUnavailable ? InfoBarSeverity.Warning : InfoBarSeverity.Success; message.Message = Arabic ? result switch {
            "Sukut is already installed. Your settings are kept." => "سُكوت مثبّت بالفعل. إعداداتك محفوظة.",
            "Sukut installed. Continue to open it on the phone." => "تم تثبيت سُكوت. تابع لفتحه في الهاتف.",
            "Sukut updated. Your settings are kept." => "تم تحديث سُكوت. إعداداتك محفوظة.",
            "Sukut is already up to date. Your settings are kept." => "سُكوت محدّث بالفعل. إعداداتك محفوظة.",
            "Sukut opened. Activate its audio worker, then press Start on the phone." => "تم فتح سُكوت. فعّل عامل الصوت، ثم اضغط زر التشغيل في الهاتف.",
            "Tools ready. Refresh devices." => "الأدوات جاهزة. حدّث قائمة الأجهزة.",
            "Sukut is installed and ready for activation." => "سُكوت مثبّت وجاهز للتفعيل.",
            "Sukut audio worker is already ready. Press Start on the phone." => "عامل الصوت جاهز بالفعل. اضغط زر التشغيل في الهاتف.",
            "Sukut audio worker is ready. You can unplug USB, then press Start on the phone. Repeat activation after a phone restart." => "عامل الصوت جاهز. يمكنك فصل USB والضغط على زر التشغيل في الهاتف. أعد التفعيل بعد إعادة تشغيل الهاتف.",
            PhoneTools.ComputerShortcutReady => "عامل الصوت جاهز. اختر وضعًا واضغط التشغيل في الهاتف. يمكنك فصل USB.",
            PhoneTools.ComputerShortcutUnavailable => "عامل الصوت جاهز، لكن تعذّر إعداد اختصار خيارات المطوّر. يمكنك استخدام عامل الصوت دون الاختصار.",
            "Sukut audio worker deactivated. Activate it again before using Start." => "أُلغي تفعيل عامل الصوت. فعّله مجدّدًا قبل استخدام زر التشغيل.",
            _ => result } : result; } }
        catch (Exception error) { if (message != null) {
            message.Severity = InfoBarSeverity.Warning;
            string detail = error.Message.Length > 800 ? error.Message[..800] + "…" : error.Message;
            message.Message = error switch {
                OperationCanceledException => L("Timed out. Check the phone and connection, then retry.", "انتهت المهلة. تحقّق من الهاتف والاتصال، ثم أعد المحاولة."),
                System.Net.Http.HttpRequestException => L("Couldn’t download the tools. Check this PC’s internet connection, then retry.", "تعذّر تنزيل الأدوات. تحقّق من اتصال الحاسوب بالإنترنت، ثم أعد المحاولة."),
                _ when detail == "Sukut audio worker is no longer ready. Keep the phone unlocked and activate again."
                    => L("Sukut audio worker is no longer ready. Keep the phone unlocked and activate again.", "عامل الصوت في سُكوت لم يعد جاهزًا. أبقِ الهاتف مفتوحًا وأعد التفعيل."),
                _ when detail.Contains("INSTALL_FAILED_USER_RESTRICTED", StringComparison.OrdinalIgnoreCase)
                    => L("Approve the installation on your phone. On Xiaomi, enable Install via USB in Developer options, then retry.", "وافق على التثبيت في هاتفك. في شاومي، فعّل «التثبيت عبر USB» من خيارات المطوّر، ثم أعد المحاولة."),
                _ when detail.Contains("INSTALL_FAILED_UPDATE_INCOMPATIBLE", StringComparison.OrdinalIgnoreCase)
                    => L("The installed app has a different signing key. Use an update signed with the same key; your installed app and data have been kept.", "للنسخة المثبّتة مفتاح توقيع مختلف. استخدم تحديثًا بالمفتاح نفسه. احتُفظ بالتطبيق المثبّت وبياناته."),
                _ when detail.Contains("Sukut APK", StringComparison.OrdinalIgnoreCase)
                    => L("The included Sukut APK is missing or damaged. Use the complete Sukut folder.", "ملف تثبيت سُكوت المرفق مفقود أو تالف. استخدم مجلد سُكوت كاملًا."),
                _ when detail.Contains("Sukut is not installed", StringComparison.OrdinalIgnoreCase)
                    => L("Sukut is not installed. Choose Install Sukut first.", "سُكوت غير مثبّت. اختر تثبيت سُكوت أولًا."),
                _ => detail
            };
        } }
        finally {
            _phoneBusy = false; SetPhoneActionsEnabled(true);
            _=RefreshPhoneQuietly();
            if (_phoneProgress != null) _phoneProgress.Visibility = Visibility.Collapsed;
            if (_phoneProgressLabel != null) _phoneProgressLabel.Visibility = Visibility.Collapsed;
            if (_phoneMessage != null && message != null && !ReferenceEquals(message, _phoneMessage))
            { _phoneMessage.Severity = message.Severity; _phoneMessage.Message = message.Message; _phoneMessage.IsOpen = message.IsOpen; }
        }
    }
}
