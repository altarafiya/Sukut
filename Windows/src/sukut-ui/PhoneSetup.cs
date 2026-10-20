using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Documents;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Media.Imaging;

namespace Sukut;

public sealed partial class ShellView
{
    private const int PhoneStepCount = 7;
    private enum PhoneTask { Choices, Install, Update, FullSetup }
    private PhoneTask _phoneTask;
    private readonly PhoneTools _phoneTools = new();
    private List<PhoneDevice> _phoneKnown = [];
    private ComboBox? _phoneDevices;
    private InfoBar? _phoneMessage;
    private StackPanel? _phoneActions;
    private StackPanel? _phonePageHost;
    private TextBlock? _phoneStepLabel;
    private Button? _phoneBack, _phoneNext;
    private Button? _phoneSukutUpdate;
    private PhoneDevice? _phoneSelected;
    private int _phoneStep;
    private bool _phoneBusy;
    private ProgressBar? _phoneProgress;
    private TextBlock? _phoneProgressLabel;

    private static Style PhoneText(string key) => (Style)Application.Current.Resources[key];
    private static Brush? PhoneBrush(string key) => new TextBlock { Style = PhoneText(key) }.Foreground;
    private static TextBlock PhoneBody(string text) => new() { Text = text, TextWrapping = TextWrapping.Wrap };

    private void PhonePanel_Loaded(object sender, RoutedEventArgs args) { FitPageViewport(); RenderPhone(); }

    private void RenderPhone()
    {
        if (PhoneContent == null) return;
        PhonePanel.FontFamily = (FontFamily)Application.Current.Resources[Arabic ? "SukutArabicFontFamily" : "ContentControlThemeFontFamily"];
        PhoneContent.Children.Clear();
        PhoneContent.Children.Add(new TextBlock { Text = L("Set up your phone", "إعداد هاتفك"), Style = PhoneText("TitleTextBlockStyle") });
        PhoneContent.Children.Add(PhoneBody(L("Connect your unlocked Android phone by USB, then choose an option.", "وصّل هاتف أندرويد مفتوح القفل عبر USB، ثم اختر ما تحتاجه.")));
        _phonePageHost = new StackPanel { Spacing = 14 };
        PhoneContent.Children.Add(_phonePageHost);

        var footer = new Grid { ColumnSpacing = 8, Margin = new Thickness(0, 6, 0, 0) };
        footer.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        footer.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        footer.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        _phoneBack = new Button { Content = L("Back", "السابق"), MinWidth = 110 };
        _phoneBack.Click += (_, _) => { if (_phoneBusy) return; if (_phoneTask == PhoneTask.FullSetup && _phoneStep > 0) { _phoneStep--; RenderPhonePage(); } else SelectPhoneTask(PhoneTask.Choices); };
        _phoneStepLabel = new TextBlock { HorizontalAlignment = HorizontalAlignment.Center, VerticalAlignment = VerticalAlignment.Center, Style = PhoneText("CaptionTextBlockStyle") };
        _phoneNext = new Button { MinWidth = 130, Style = (Style)Application.Current.Resources["AccentButtonStyle"] };
        _phoneNext.Click += (_, _) => { if (_phoneBusy) return; if (_phoneStep < PhoneStepCount - 1) { _phoneStep++; RenderPhonePage(); } else SelectPhoneTask(PhoneTask.Choices); };
        Grid.SetColumn(_phoneStepLabel, 1); Grid.SetColumn(_phoneNext, 2);
        footer.Children.Add(_phoneBack); footer.Children.Add(_phoneStepLabel); footer.Children.Add(_phoneNext);
        PhoneFooter.Child = footer;
        RenderPhonePage();
    }

    private void SelectPhoneTask(PhoneTask task)
    {
        if (_phoneBusy) return;
        _phoneTask = task;
        if (task == PhoneTask.FullSetup) _phoneStep = 0;
        RenderPhonePage();
    }

    private bool PhoneShowsSukutUpdate => _phoneTask == PhoneTask.Update || _phoneTask == PhoneTask.FullSetup && _phoneStep == 5;

    private void PhoneChoicesPage()
    {
        _phoneActions = new StackPanel { Spacing = 12 };
        _phonePageHost!.Children.Add(_phoneActions);
        PhoneChoice(PhoneTask.Install, "\uE896", "ModeHalalText", L("Install Sukut", "تثبيت سُكوت"),
            L("Add Sukut to your phone. Already set up USB debugging? Start here.", "ثبّت سُكوت في هاتفك. ابدأ هنا إذا كان تصحيح USB جاهزًا."), "PhoneInstallOption");
        PhoneChoice(PhoneTask.Update, "\uE895", "ModeOptimizedText", L("Update Sukut", "تحديث سُكوت"),
            L("Install the included update and keep your settings.", "ثبّت التحديث المرفق مع الاحتفاظ بإعداداتك."), "PhoneUpdateOption");
        PhoneChoice(PhoneTask.FullSetup, "\uE713", "ModeOffText", L("Full setup", "الإعداد الكامل"),
            L("First time? Follow USB debugging, Shizuku and Sukut step by step.", "للمرة الأولى؟ اتبع خطوات تصحيح USB وإعداد Shizuku وسُكوت."), "PhoneFullSetupOption");
    }

    private void PhoneChoice(PhoneTask task, string glyph, string color, string title, string detail, string id)
    {
        var button = new Button { HorizontalAlignment = HorizontalAlignment.Stretch, HorizontalContentAlignment = HorizontalAlignment.Stretch, Padding = new Thickness(18), CornerRadius = new CornerRadius(10) };
        var row = new Grid { ColumnSpacing = 16 };
        row.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        row.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        row.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        var icon = new FontIcon { Glyph = glyph, FontSize = 28, VerticalAlignment = VerticalAlignment.Center, Foreground = PhoneBrush(color) };
        var words = new StackPanel { Spacing = 5 };
        words.Children.Add(new TextBlock { Text = title, Style = PhoneText(color), FontSize = 19, TextWrapping = TextWrapping.Wrap });
        words.Children.Add(new TextBlock { Text = detail, Style = PhoneText("CaptionTextBlockStyle"), TextWrapping = TextWrapping.Wrap });
        var arrow = new FontIcon { Glyph = L("\uE76C", "\uE76B"), FontSize = 14, VerticalAlignment = VerticalAlignment.Center };
        Grid.SetColumn(words, 1); Grid.SetColumn(arrow, 2);
        row.Children.Add(icon); row.Children.Add(words); row.Children.Add(arrow); button.Content = row;
        Microsoft.UI.Xaml.Automation.AutomationProperties.SetName(button, title);
        Microsoft.UI.Xaml.Automation.AutomationProperties.SetAutomationId(button, id);
        button.Click += (_, _) => SelectPhoneTask(task);
        _phoneActions!.Children.Add(button);
    }

    private void PhoneQuickAppPage(bool update)
    {
        var page = PhonePage(update ? "\uE895" : "\uE896", update ? "ModeOptimizedText" : "ModeHalalText", update ? L("Update Sukut", "تحديث سُكوت") : L("Install Sukut", "تثبيت سُكوت"));
        page.Children.Add(PhoneBody(update
            ? L("Update an existing Sukut with the included APK. Your settings stay saved; newer versions are kept.", "حدّث سُكوت المثبّت بملف التطبيق المرفق. إعداداتك محفوظة، ولن نستبدل نسخة أحدث.")
            : L("Install the included Android app. If Sukut is already installed, use Update Sukut.", "ثبّت تطبيق أندرويد المرفق. إذا كان سُكوت مثبّتًا بالفعل، اختر تحديث سُكوت.")));
        var primary = PhoneToolActions(page, update ? "Update Sukut" : "Install Sukut", update ? "تحديث سُكوت" : "تثبيت سُكوت", async device => {
            string result = await _phoneTools.InstallSukut(device, update);
            if (update) await RefreshSukutUpdate(device);
            return result;
        }, update ? "\uE895" : "\uE896");
        if (update) _phoneSukutUpdate = primary;
        page.Children.Add(PhoneBody(L("USB debugging must be enabled. Approve any prompt on the phone.", "يجب تفعيل تصحيح USB. وافق على أي طلب يظهر في الهاتف.")));
        var help = new Button { HorizontalAlignment = HorizontalAlignment.Stretch };
        SetPhoneButtonLabel(help, "\uE713", L("Need help? Full setup", "تحتاج إلى مساعدة؟ الإعداد الكامل"));
        help.Click += (_, _) => SelectPhoneTask(PhoneTask.FullSetup);
        _phoneActions!.Children.Add(help);
        QueueSukutUpdateCheck();
    }

    private void RenderPhonePage()
    {
        if (_phonePageHost == null) return;
        _phoneStep = Math.Clamp(_phoneStep, 0, PhoneStepCount - 1);
        _phonePageHost.Children.Clear();
        _phoneMessage = null; _phoneActions = null; _phoneDevices = null; _phoneSukutUpdate = null;
        _phoneProgress = null; _phoneProgressLabel = null;
        PhoneScroll.ChangeView(null, 0, null, true);
        PhoneFooter.Visibility = _phoneTask == PhoneTask.Choices ? Visibility.Collapsed : Visibility.Visible;
        if (_phoneTask == PhoneTask.Choices) PhoneChoicesPage();
        else if (_phoneTask != PhoneTask.FullSetup) PhoneQuickAppPage(_phoneTask == PhoneTask.Update);
        else switch (_phoneStep)
        {
            case 0: PhoneOverviewPage(); break;
            case 1: PhoneDeveloperPage(); break;
            case 2: PhoneDebuggingPage(); break;
            case 3: PhoneInstallPage(); break;
            case 4: PhoneStartPage(); break;
            case 5: PhoneSukutPage(); break;
            default: PhoneAllowPage(); break;
        }
        if (_phoneStepLabel != null) _phoneStepLabel.Text = _phoneTask == PhoneTask.FullSetup ? L($"Step {_phoneStep + 1} of {PhoneStepCount}", $"الخطوة {_phoneStep + 1} من {PhoneStepCount}") : "";
        if (_phoneNext != null) { _phoneNext.Visibility = _phoneTask == PhoneTask.FullSetup ? Visibility.Visible : Visibility.Collapsed; _phoneNext.Content = _phoneStep == PhoneStepCount - 1 ? L("Done", "تمّ") : L("Next", "التالي"); }
        SetPhoneActionsEnabled(!_phoneBusy);
        ArabicTypography.Apply(PhonePanel, Arabic);
        QueueTypography();
    }

    private StackPanel PhonePage(string glyph, string color, string heading)
    {
        var page = new StackPanel { Spacing = 12 };
        var head = new Grid { ColumnSpacing = 12 };
        head.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        head.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        var icon = new FontIcon { Glyph = glyph, FontSize = 26, VerticalAlignment = VerticalAlignment.Center };
        if (PhoneBrush(color) is { } mark) icon.Foreground = mark;
        var title = new TextBlock { Text = heading, Style = PhoneText(color), FontSize = 22, TextWrapping = TextWrapping.Wrap, VerticalAlignment = VerticalAlignment.Center };
        Grid.SetColumn(title, 1);
        head.Children.Add(icon); head.Children.Add(title);
        page.Children.Add(head);
        _phonePageHost!.Children.Add(page);
        return page;
    }

    private static TextBlock PhoneRich(params (string Text, string? Color)[] parts)
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

    private static StackPanel PhoneBullets(params (string Glyph, string Color, string Title, string Detail)[] items)
    {
        var list = new StackPanel { Spacing = 10 };
        foreach (var item in items)
        {
            var row = new Grid { ColumnSpacing = 10 };
            row.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
            row.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
            var icon = new FontIcon { Glyph = item.Glyph, FontSize = 18, VerticalAlignment = VerticalAlignment.Center };
            if (PhoneBrush(item.Color) is { } mark) icon.Foreground = mark;
            var text = new StackPanel { Spacing = 2 };
            text.Children.Add(new TextBlock { Text = item.Title, Style = PhoneText(item.Color), TextWrapping = TextWrapping.Wrap });
            text.Children.Add(new TextBlock { Text = item.Detail, Style = PhoneText("CaptionTextBlockStyle"), TextWrapping = TextWrapping.Wrap });
            Grid.SetColumn(text, 1);
            row.Children.Add(icon); row.Children.Add(text);
            list.Children.Add(row);
        }
        return list;
    }

    private static Border PhoneNote(string color, string text)
    {
        var border = new Border { Padding = new Thickness(12), CornerRadius = new CornerRadius(8), BorderThickness = new Thickness(1) };
        if (PhoneBrush(color) is { } mark) border.BorderBrush = mark;
        var row = new Grid { ColumnSpacing = 10 };
        row.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        row.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        var icon = new FontIcon { Glyph = "\uE946", FontSize = 18, VerticalAlignment = VerticalAlignment.Center };
        if (PhoneBrush(color) is { } mark2) icon.Foreground = mark2;
        var body = PhoneBody(text);
        Grid.SetColumn(body, 1);
        row.Children.Add(icon); row.Children.Add(body);
        border.Child = row;
        return border;
    }

    private static Image PhonePicture(string picture) => new()
    {
        Source = new BitmapImage(new Uri("ms-appx:///Assets/Setup/" + picture)) { DecodePixelWidth = 520 },
        MaxHeight = 330,
        Stretch = Stretch.Uniform,
        HorizontalAlignment = HorizontalAlignment.Center,
    };

    private void PhoneOverviewPage()
    {
        var page = PhonePage("\uE8EA", "ModeOptimizedText", L("What you need", "ما تحتاجه"));
        page.Children.Add(PhoneRich(
            (L("Shizuku runs on your phone and lets Sukut change audio ", "يعمل Shizuku في هاتفك ويمكّن سُكوت من تغيير الصوت "), null),
            (L("without root", "دون روت"), "ModeHalalText"),
            (L(". After every phone restart, start Shizuku again using the PC or wireless method. Installation and pairing usually stay saved.", ". بعد كل إعادة تشغيل للهاتف، شغّل Shizuku مجددًا بالحاسوب أو بالطريقة اللاسلكية. لا يلزم عادةً تكرار التثبيت أو الإقران."), null)));
        page.Children.Add(PhoneBullets(
            ("\uE945", "ModeOptimizedText", L("A USB data cable", "كابل USB للبيانات"), L("A charging-only cable will not work.", "الكابل المخصّص للشحن فقط لن يعمل.")),
            ("\uE774", "ModeHalalText", L("Internet, once", "إنترنت لمرة واحدة"), L("This PC downloads the official Shizuku tools.", "ينزّل هذا الحاسوب أدوات Shizuku الرسمية.")),
            ("\uE823", "ModeOffText", L("About three minutes", "حوالي ثلاث دقائق"), L("Keep the phone unlocked while you follow the steps.", "أبقِ الهاتف مفتوحًا أثناء اتباع الخطوات."))));
        page.Children.Add(PhoneNote("ModeHalalText", L("You approve USB debugging on your phone. This guide installs Shizuku and Sukut, then starts Shizuku.", "توافق أنت على تصحيح USB في هاتفك. يثبّت هذا الدليل Shizuku وسُكوت، ثم يشغّل Shizuku.")));
    }

    private void PhoneDeveloperPage()
    {
        var page = PhonePage("\uE713", "ModeOptimizedText", L("Enable developer options", "تفعيل خيارات المطوّر"));
        page.Children.Add(PhoneRich(
            (L("On your phone open ", "في هاتفك افتح "), null),
            (L("Settings → About phone", "الإعدادات ← حول الهاتف"), "ModeOptimizedText"),
            (L(", then tap ", "، ثم اضغط "), null),
            (L("Build number", "رقم الإصدار"), "ModeOptimizedText"),
            (L(" seven times. On Xiaomi tap ", " سبع مرات. وفي شاومي اضغط "), null),
            (L("OS version", "إصدار النظام"), "ModeOptimizedText"),
            (L(" instead. Enter your phone PIN if asked, then open ", " بدلًا منها. أدخل رمز القفل إذا طُلب، ثم افتح "), null),
            (L("Developer options", "خيارات المطوّر"), "ModeOptimizedText"),
            (L(", which is often under Additional settings.", "، وهي غالبًا ضمن الإعدادات الإضافية."), null)));
        page.Children.Add(PhonePicture("phone-developer-options.png"));
        page.Children.Add(PhoneNote("ModeOptimizedText", L("You will see a warning that these options are for developers. That is normal.", "ستظهر رسالة تحذير بأن هذه الخيارات للمطوّرين. هذا طبيعي.")));
    }

    private void PhoneDebuggingPage()
    {
        var page = PhonePage("\uE72E", "ModeOptimizedText", L("Allow USB debugging", "السماح بتصحيح USB"));
        page.Children.Add(PhoneRich(
            (L("In ", "في "), null),
            (L("Developer options", "خيارات المطوّر"), "ModeOptimizedText"),
            (L(" turn on ", " شغّل "), null),
            (L("USB debugging", "تصحيح USB"), "ModeOptimizedText"),
            (L(". Connect the phone to this PC with the cable and tap ", ". وصّل الهاتف بهذا الحاسوب بالكابل ثم اضغط "), null),
            (L("Allow", "السماح"), "ModeOptimizedText"),
            (L(" on the phone. Keep the phone unlocked during setup.", " في الهاتف. أبقِ الهاتف مفتوحًا أثناء الإعداد."), null)));
        page.Children.Add(PhonePicture("phone-usb-debugging.png"));
        page.Children.Add(PhoneNote("ModeOptimizedText", L("If installing Shizuku is blocked, also allow Install via USB on the phone.", "إذا حُظر تثبيت Shizuku، اسمح أيضًا بـ«التثبيت عبر USB» في الهاتف.")));
    }

    private void PhoneInstallPage()
    {
        var page = PhonePage("\uE896", "ModeHalalText", L("Install Shizuku", "تثبيت Shizuku"));
        page.Children.Add(PhoneRich(
            (L("Download the official tools once on this PC, connect the phone, then install ", "نزّل الأدوات الرسمية مرة واحدة على هذا الحاسوب، وصّل الهاتف، ثم ثبّت "), null),
            (L("Shizuku 13.5.4", "Shizuku 13.5.4"), "ModeHalalText"),
            (L(". The internet is needed only for this step.", ". الإنترنت مطلوب في هذه الخطوة فقط."), null)));
        PhoneToolActions(page, "Install Shizuku 13.5.4", "تثبيت Shizuku 13.5.4", device => _phoneTools.Install(device, PhoneDownloadProgress()));
    }

    private void PhoneStartPage()
    {
        var page = PhonePage("\uE768", "ModeHalalText", L("Start Shizuku", "تشغيل Shizuku"));
        page.Children.Add(PhoneRich(
            (L("The button opens Shizuku on your phone and starts it over USB. Keep the phone unlocked and approve any prompt.", "يفتح الزر Shizuku في هاتفك ويشغّله عبر USB. أبقِ الهاتف مفتوحًا ووافق على أي طلب يظهر."), null)));
        PhoneToolActions(page, "Open and start Shizuku", "فتح Shizuku وتشغيله", async device => { await _phoneTools.Open(device); return await _phoneTools.Start(device); });
        page.Children.Add(PhoneNote("ModeOptimizedText", L("After every phone restart, reconnect and repeat this Start step, or start Shizuku wirelessly. You do not need to reinstall it.", "بعد كل إعادة تشغيل للهاتف، أعد توصيله وكرّر خطوة التشغيل هذه، أو شغّل Shizuku لاسلكيًا. لا تحتاج إلى إعادة تثبيته.")));
    }

    private void PhoneAllowPage()
    {
        var page = PhonePage("\uE73E", "ModeHalalText", L("Allow Sukut on your phone", "السماح لسُكوت في هاتفك"));
        page.Children.Add(PhoneRich(
            (L("Open ", "افتح "), null),
            (L("Sukut", "سُكوت"), "ModeHalalText"),
            (L(" on the phone and allow its Shizuku request. Once Shizuku is running you can unplug the USB cable.", " في الهاتف واسمح بطلب Shizuku. بعد تشغيله يمكنك فصل كابل USB."), null)));
        PhoneToolActions(page, "Open Sukut on the phone", "فتح سُكوت في الهاتف", device => _phoneTools.OpenSukut(device));
        var restart = new Button { HorizontalAlignment = HorizontalAlignment.Stretch };
        var restartLabel = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 8 };
        restartLabel.Children.Add(new FontIcon { Glyph = "\uE72C", FontSize = 16 });
        restartLabel.Children.Add(PhoneBody(L("Start again", "البدء مجدّدًا")));
        restart.Content = restartLabel;
        Microsoft.UI.Xaml.Automation.AutomationProperties.SetName(restart, L("Start phone setup again", "بدء دليل إعداد الهاتف مجدّدًا"));
        restart.Click += (_, _) => { if (!_phoneBusy) { _phoneStep = 0; RenderPhonePage(); } };
        _phoneActions!.Children.Add(restart);
        page.Children.Add(PhoneNote("ModeOptimizedText", L("Start Shizuku again after restarting the phone. You can unplug the cable; keep debugging enabled while using Shizuku.", "شغّل Shizuku مجددًا بعد إعادة تشغيل الهاتف. يمكنك فصل الكابل؛ أبقِ التصحيح مفعّلًا أثناء استخدام Shizuku.")));
    }

    private void PhoneSukutPage()
    {
        var page = PhonePage("\uE896", "ModeOptimizedText", L("Install or update Sukut", "تثبيت سُكوت أو تحديثه"));
        page.Children.Add(PhoneBody(L("Install the included Android app, or update an older Sukut without removing your settings. A newer installed version is kept.", "ثبّت تطبيق أندرويد المرفق، أو حدّث نسخة سُكوت القديمة مع الاحتفاظ بإعداداتك. لن نستبدل نسخة أحدث مثبّتة في الهاتف.")));
        page.Children.Add(PhoneNote("ModeOffText", L("Keep the phone unlocked. If Android asks, approve the installation on the phone.", "أبقِ الهاتف مفتوحًا. وافق على طلب التثبيت في الهاتف إذا ظهر.")));
        PhoneToolActions(page, "Check and install Sukut", "التحقّق من سُكوت وتثبيته", async device => {
            string result = await _phoneTools.InstallSukut(device);
            await RefreshSukutUpdate(device); return result;
        });
        _phoneSukutUpdate = PhoneActionButton("Update Sukut to the latest version", "تحديث سُكوت إلى أحدث نسخة", async () => {
            var device = await GetPhone();
            string result = await _phoneTools.InstallSukut(device, update: true);
            await RefreshSukutUpdate(device); return result;
        });
        _phoneSukutUpdate.Visibility = Visibility.Collapsed;
        _phoneActions!.Children.Add(_phoneSukutUpdate);
        QueueSukutUpdateCheck();
    }

    private void QueueSukutUpdateCheck()
    {
        if (PhoneShowsSukutUpdate && _phoneTools.AdbReady && _phoneSelected?.State == "device")
            DispatcherQueue.TryEnqueue(async () => { if (PhoneShowsSukutUpdate && !_phoneBusy && _phoneSelected is { } device) await PhoneAction(async () => { await RefreshSukutUpdate(device); return L("Sukut checked. Use Update if an older version is installed.", "تم التحقّق من سُكوت. استخدم زر التحديث إذا كانت النسخة المثبّتة أقدم."); }); });
    }

    private async Task RefreshSukutUpdate(PhoneDevice device)
    {
        var status = await _phoneTools.SukutStatus(device);
        if (!PhoneShowsSukutUpdate || _phoneSukutUpdate == null || _phoneSelected?.Serial != device.Serial) return;
        string label = status.IsCurrent ? L("Sukut is up to date", "سُكوت محدّث بالفعل") : status.InstalledCode == null ? L("Install Sukut first", "ثبّت سُكوت أولًا") : L($"Update Sukut to {status.BundledVersion}", $"تحديث سُكوت إلى {status.BundledVersion}");
        SetPhoneButtonLabel(_phoneSukutUpdate, "\uE895", label);
        _phoneSukutUpdate.Visibility = _phoneTask == PhoneTask.Update || status.InstalledCode != null ? Visibility.Visible : Visibility.Collapsed;
        _phoneSukutUpdate.IsEnabled = status.CanUpdate;
    }

    private Button PhoneToolActions(StackPanel page, string en, string ar, Func<PhoneDevice, Task<string>> action, string? glyph = null)
    {
        _phoneActions = new StackPanel { Spacing = 12 };
        var devicesRow = new Grid { ColumnSpacing = 8 };
        devicesRow.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        devicesRow.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        devicesRow.Children.Add(PhoneDevicePicker());
        var refresh = PhoneActionButton("Find phone", "البحث عن الهاتف", RefreshPhoneDevices, "\uE721");
        Grid.SetColumn(refresh, 1); devicesRow.Children.Add(refresh);
        _phoneActions.Children.Add(devicesRow);
        var primary = PhoneActionButton(en, ar, async () => await action(await GetPhone()), glyph);
        primary.Style = (Style)Application.Current.Resources["AccentButtonStyle"];
        primary.MinHeight = 42;
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
        string item = transfer.Item == "USB tools" ? L("USB tools", "أدوات USB") : "Shizuku";
        if (transfer.Total is > 0)
        {
            _phoneProgress.Value = Math.Clamp(100d * transfer.Received / transfer.Total.Value, 0, 100);
            _phoneProgressLabel.Text = L($"Downloading {item} · {received:0.0} / {transfer.Total / 1048576d:0.0} MB", $"تنزيل {item} · {received:0.0} / {transfer.Total / 1048576d:0.0} م.ب");
        }
        else _phoneProgressLabel.Text = L($"Downloading {item} · {received:0.0} MB", $"تنزيل {item} · {received:0.0} م.ب");
    });

    private async Task<PhoneDevice> GetPhone()
    {
        if (!_phoneTools.AdbReady) await _phoneTools.Prepare(PhoneDownloadProgress(), includeShizuku: false);
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
        var content = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 8, HorizontalAlignment = HorizontalAlignment.Center };
        content.Children.Add(new FontIcon { Glyph = glyph, FontSize = 17 });
        content.Children.Add(PhoneBody(text)); button.Content = content;
        Microsoft.UI.Xaml.Automation.AutomationProperties.SetName(button, text);
    }

    private Button PhoneActionButton(string en, string ar, Func<Task<string>> action, string? glyph = null)
    {
        var button = new Button { Content = L(en, ar), HorizontalAlignment = HorizontalAlignment.Stretch };
        if (glyph != null) SetPhoneButtonLabel(button, glyph, L(en, ar));
        button.Click += async (_, _) => await PhoneAction(action);
        return button;
    }

    private ComboBox PhoneDevicePicker()
    {
        _phoneDevices = new ComboBox { HorizontalAlignment = HorizontalAlignment.Stretch, DisplayMemberPath = "Label", PlaceholderText = L("Choose phone", "اختر الهاتف"), MaxDropDownHeight = 240, ItemsSource = _phoneKnown, SelectedItem = _phoneSelected };
        _phoneDevices.SelectionChanged += async (_, _) => {
            if (_phoneDevices?.SelectedItem is not PhoneDevice device) return;
            _phoneSelected = device;
            if (!_phoneBusy && PhoneShowsSukutUpdate && device.State == "device" && _phoneSukutUpdate?.IsLoaded == true)
                await PhoneAction(async () => { await RefreshSukutUpdate(device); return L("Sukut checked. Use Update if an older version is installed.", "تم التحقّق من سُكوت. استخدم زر التحديث إذا كانت النسخة المثبّتة أقدم."); });
        };
        return _phoneDevices;
    }

    private PhoneDevice PhoneSelected() => _phoneDevices?.SelectedItem as PhoneDevice ?? _phoneSelected ?? throw new IOException(L("Select your phone first.", "اختر هاتفك أولًا."));

    private async Task<string> RefreshPhoneDevices()
    {
        if (!_phoneTools.AdbReady) await _phoneTools.Prepare(PhoneDownloadProgress(), includeShizuku: false);
        var devices = await _phoneTools.Devices();
        _phoneKnown = devices;
        _phoneSelected = devices.Count == 1 ? devices[0] : devices.Find(x => x.Serial == _phoneSelected?.Serial);
        if (_phoneDevices != null) { _phoneDevices.ItemsSource = null; _phoneDevices.ItemsSource = _phoneKnown; _phoneDevices.SelectedItem = _phoneSelected; }
        if (devices.Count == 0) throw new IOException(L("No phone found. Unlock it, check the USB data cable, and allow USB debugging. A manufacturer USB driver may be needed.", "لم يُعثر على هاتف. افتح قفله، وتحقّق من كابل البيانات، واسمح بتصحيح USB. قد تحتاج إلى تعريف USB من الشركة المصنّعة."));
        if (PhoneShowsSukutUpdate && _phoneSelected?.State == "device") await RefreshSukutUpdate(_phoneSelected);
        return L("Phone found. Select it, then use the button below. If it says unauthorized, allow USB debugging on the phone first.", "عُثر على الهاتف. اختره واستخدم الزر أدناه. إذا ظهر unauthorized، وافق على تصحيح USB في الهاتف أولًا.");
    }

    private void SetPhoneActionsEnabled(bool enabled)
    {
        if (_phoneActions != null) { _phoneActions.IsHitTestVisible = enabled; _phoneActions.Opacity = enabled ? 1 : .6; }
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
        try { string result = await action(); if (message != null) { message.Severity = InfoBarSeverity.Success; message.Message = Arabic ? result switch {
            "Sukut is already installed. Your settings are kept." => "سُكوت مثبّت بالفعل. إعداداتك محفوظة.",
            "Sukut installed. Continue to open it on the phone." => "تم تثبيت سُكوت. تابع لفتحه في الهاتف.",
            "Sukut updated. Your settings are kept." => "تم تحديث سُكوت. إعداداتك محفوظة.",
            "Sukut is already up to date. Your settings are kept." => "سُكوت محدّث بالفعل. إعداداتك محفوظة.",
            "Sukut opened. Allow its Shizuku request on the phone." => "تم فتح سُكوت. وافق على طلب Shizuku في الهاتف.",
            "Tools ready. Refresh devices." => "الأدوات جاهزة. حدّث قائمة الأجهزة.",
            "Shizuku 13.5.4 or newer is already installed." => "Shizuku 13.5.4 أو أحدث مثبت بالفعل.",
            "Shizuku installed. Open it once, then choose Start Shizuku." => "تم تثبيت Shizuku. افتحه مرة، ثم اختر تشغيل Shizuku.",
            "Shizuku opened on the phone." => "تم فتح Shizuku في الهاتف.",
            "Shizuku is running. Open Sukut on the phone and allow its Shizuku request. Start Shizuku again after a phone restart." => "Shizuku يعمل. افتح سُكوت في الهاتف واسمح بطلب الوصول. شغّل Shizuku مجددًا بعد إعادة تشغيل الهاتف.",
            _ => result } : result; } }
        catch (Exception error) { if (message != null) {
            message.Severity = InfoBarSeverity.Warning;
            string detail = error.Message.Length > 800 ? error.Message[..800] + "…" : error.Message;
            message.Message = error switch {
                OperationCanceledException => L("Timed out. Check the phone and connection, then retry.", "انتهت المهلة. تحقّق من الهاتف والاتصال، ثم أعد المحاولة."),
                System.Net.Http.HttpRequestException => L("Couldn’t download the tools. Check this PC’s internet connection, then retry.", "تعذّر تنزيل الأدوات. تحقّق من اتصال الحاسوب بالإنترنت، ثم أعد المحاولة."),
                _ when detail.Contains("INSTALL_FAILED_USER_RESTRICTED", StringComparison.OrdinalIgnoreCase)
                    => L("Approve the installation on your phone. On Xiaomi, enable Install via USB in Developer options, then retry.", "وافق على التثبيت في هاتفك. في شاومي، فعّل «التثبيت عبر USB» من خيارات المطوّر، ثم أعد المحاولة."),
                _ when detail.Contains("INSTALL_FAILED_UPDATE_INCOMPATIBLE", StringComparison.OrdinalIgnoreCase)
                    => L("The installed app has a different signing key. Use an update signed with the same key; your installed app and data have been kept.", "للنسخة المثبّتة مفتاح توقيع مختلف. استخدم تحديثًا بالمفتاح نفسه. احتُفظ بالتطبيق المثبّت وبياناته."),
                _ when detail.Contains("Shizuku did not start", StringComparison.OrdinalIgnoreCase)
                    => L("Shizuku did not start. Open it on the phone once, keep the USB cable connected, then retry.", "لم يبدأ Shizuku. افتحه في الهاتف مرة واحدة، وأبقِ كابل USB موصولًا، ثم أعد المحاولة."),
                _ when detail.Contains("Sukut APK", StringComparison.OrdinalIgnoreCase)
                    => L("The included Sukut APK is missing or damaged. Use the complete Sukut folder.", "ملف تثبيت سُكوت المرفق مفقود أو تالف. استخدم مجلد سُكوت كاملًا."),
                _ when detail.Contains("Sukut is not installed", StringComparison.OrdinalIgnoreCase)
                    => L("Sukut is not installed. Choose Install Sukut first.", "سُكوت غير مثبّت. اختر تثبيت سُكوت أولًا."),
                _ => detail
            };
        } }
        finally {
            _phoneBusy = false; SetPhoneActionsEnabled(true);
            if (_phoneProgress != null) _phoneProgress.Visibility = Visibility.Collapsed;
            if (_phoneProgressLabel != null) _phoneProgressLabel.Visibility = Visibility.Collapsed;
            if (_phoneMessage != null && message != null && !ReferenceEquals(message, _phoneMessage))
            { _phoneMessage.Severity = message.Severity; _phoneMessage.Message = message.Message; }
        }
    }
}
