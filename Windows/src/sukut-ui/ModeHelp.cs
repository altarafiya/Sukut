using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace Sukut;

// The phone's "?" sheet for audio modes: one coloured card per mode with its
// icon and short points, plus how Start/Stop works. Built only when opened.
public sealed partial class ShellView
{
    private const string MusicFilterIcon = "F1 M13,3 L21,1 L21,12 C21,14.2 19.2,16 17,16 C14.8,16 13,14.5 13,12.5 C13,10.6 14.8,9 17,9 C17.7,9 18.4,9.2 19,9.5 L19,5 L13,6.5 L13,18 C13,20.2 11.2,22 9,22 C6.8,22 5,20.5 5,18.5 C5,16.6 6.8,15 9,15 C9.7,15 10.4,15.2 11,15.5 L11,3.5 Z M2.7,1.3 L22.7,21.3 L21.3,22.7 L1.3,2.7 Z";
    private bool _modeHelpOpen;

    private async void ModeHelp_Click(object sender, RoutedEventArgs args)
    {
        if (_modeHelpOpen || Root.XamlRoot == null) return;
        _modeHelpOpen = true;
        var body = new StackPanel { Spacing = 10 };
        body.Children.Add(HelpCard(MusicFilterIcon, null, "ModeHalalBrush", L("Music Filter", "مرشّح الموسيقى"),
            L("Removes music while keeping speech and other sounds.", "يزيل الموسيقى مع إبقاء الكلام والأصوات الأخرى."),
            L("A good starting point for everyday listening.", "خيار مناسب للاستماع اليومي."),
            L("Music Filter is not perfect. Some instruments may still be heard.", "مرشّح الموسيقى ليس مثاليًا. قد تبقى بعض الآلات مسموعة.")));
        body.Children.Add(HelpCard(null, "", "ModeOptimizedBrush", L("Optimized", "محسّن"),
            L("Removes music and lets you choose how much background sound remains.", "يزيل الموسيقى ويتيح لك تحديد مقدار أصوات الخلفية."),
            L("Use the Background level slider: lower for a quieter background, higher for more ambience.", "استخدم شريط «مستوى الخلفية»: خفّضه لخلفية أهدأ، وارفعه لسماع أصوات أكثر.")));
        body.Children.Add(HelpCard(null, "", "ModeStrictBrush", L("Strict", "صارم"),
            L("Focuses on speech by muting music and background sound.", "يركّز على الكلام بكتم الموسيقى وأصوات الخلفية."),
            L("Some sound effects may also disappear.", "قد تختفي بعض المؤثرات الصوتية أيضًا.")));
        body.Children.Add(HelpCard(null, "", "SukutAccentBrush", L("Start and stop", "التشغيل والإيقاف"),
            L("Choose a mode, then click Start. Click Stop to turn filtering off. Selecting a mode while stopped does not start audio.",
              "اختر وضعًا، ثم اضغط «تشغيل». اضغط «إيقاف» لإيقاف الترشيح. اختيار الوضع أثناء التوقف لا يبدأ معالجة الصوت.")));
        var columns = new Grid { ColumnSpacing = 16, RowSpacing = 16 };
        columns.ColumnDefinitions.Add(new() { Width = new GridLength(1, GridUnitType.Star) });
        columns.ColumnDefinitions.Add(new() { Width = new GridLength(1, GridUnitType.Star) });
        columns.RowDefinitions.Add(new() { Height = GridLength.Auto });
        columns.RowDefinitions.Add(new() { Height = GridLength.Auto });
        StackPanel Section(string title, StackPanel content)
        {
            var section = new StackPanel { Spacing = 12 };
            var heading = new TextBlock { Text = title, FontSize = 18, FontWeight = Microsoft.UI.Text.FontWeights.SemiBold, TextWrapping = TextWrapping.Wrap };
            Microsoft.UI.Xaml.Automation.AutomationProperties.SetHeadingLevel(heading, Microsoft.UI.Xaml.Automation.Peers.AutomationHeadingLevel.Level2);
            section.Children.Add(heading); section.Children.Add(content); return section;
        }
        var modes = Section(L("Audio modes", "أوضاع الصوت"), body);
        var buffering = Section(L("Audio buffering", "التخزين المؤقت للصوت"), BuildBufferingHelp());
        columns.Children.Add(modes); columns.Children.Add(buffering);
        var scroll = new ScrollViewer { Content = columns, HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled, VerticalScrollBarVisibility = ScrollBarVisibility.Auto };
        void LayoutHelp()
        {
            double width = Math.Clamp(Root.ActualWidth - 96, 260, 880);
            bool wide = width >= 640;
            columns.Width = width;
            columns.ColumnDefinitions[1].Width = wide ? new GridLength(1, GridUnitType.Star) : new GridLength(0);
            columns.ColumnSpacing = wide ? 16 : 0;
            Grid.SetColumn(buffering, wide ? 1 : 0); Grid.SetRow(buffering, wide ? 0 : 1);
            scroll.MaxHeight = Math.Clamp(Root.ActualHeight - 200, 140, 480);
        }
        SizeChangedEventHandler resized = (_, _) => LayoutHelp();
        var dialog = new ContentDialog
        {
            XamlRoot = Root.XamlRoot, RequestedTheme = Root.ActualTheme,
            Title = L("Audio help", "دليل الصوت"), CloseButtonText = L("Done", "تم"), DefaultButton = ContentDialogButton.Close,
            Content = scroll,
            FlowDirection = Arabic ? FlowDirection.RightToLeft : FlowDirection.LeftToRight,
            Language = Arabic ? "ar" : "en", FontFamily = FontFamily,
        };
        dialog.Resources["ContentDialogMaxWidth"] = 960d;
        LayoutHelp(); Root.SizeChanged += resized;
        ArabicTypography.Apply(dialog, Arabic);
        try { await dialog.ShowAsync(); }
        finally { Root.SizeChanged -= resized; _modeHelpOpen = false; }
    }

    // XAML with {ThemeResource} so the colours follow the dialog's (Sukut's) theme,
    // not the system default an Application.Resources lookup would return.
    private static Border HelpCard(string? path, string? glyph, string brushKey, string title, params string[] points)
    {
        static string X(string value) => System.Security.SecurityElement.Escape(value) ?? "";
        string tone = $"{{ThemeResource {brushKey}}}";
        string icon = path != null
            ? $"<PathIcon Data=\"{path}\" Width=\"20\" Height=\"20\" FlowDirection=\"LeftToRight\" VerticalAlignment=\"Center\" Foreground=\"{tone}\"/>"
            : $"<FontIcon Glyph=\"{X(glyph!)}\" FontSize=\"20\" VerticalAlignment=\"Center\" Foreground=\"{tone}\"/>";
        var xaml = new System.Text.StringBuilder();
        xaml.Append("<Border xmlns=\"http://schemas.microsoft.com/winfx/2006/xaml/presentation\" Padding=\"14,12\" CornerRadius=\"8\" BorderThickness=\"1\"")
            .Append(" Background=\"{ThemeResource CardBackgroundFillColorDefaultBrush}\" BorderBrush=\"{ThemeResource CardStrokeColorDefaultBrush}\"><StackPanel Spacing=\"6\">")
            .Append("<Grid ColumnSpacing=\"10\"><Grid.ColumnDefinitions><ColumnDefinition Width=\"Auto\"/><ColumnDefinition Width=\"*\"/></Grid.ColumnDefinitions>").Append(icon)
            .Append($"<TextBlock Grid.Column=\"1\" TextWrapping=\"Wrap\" Text=\"{X(title)}\" FontSize=\"16\" FontWeight=\"SemiBold\" VerticalAlignment=\"Center\" Foreground=\"{tone}\" AutomationProperties.HeadingLevel=\"Level3\"/></Grid>");
        foreach (string point in points)
            xaml.Append("<Grid ColumnSpacing=\"8\"><Grid.ColumnDefinitions><ColumnDefinition Width=\"12\"/><ColumnDefinition Width=\"*\"/></Grid.ColumnDefinitions>")
                .Append($"<TextBlock Text=\"•\" FontWeight=\"Bold\" Foreground=\"{tone}\"/>")
                .Append($"<TextBlock Grid.Column=\"1\" Text=\"{X(point)}\" TextWrapping=\"Wrap\" Foreground=\"{{ThemeResource TextFillColorSecondaryBrush}}\"/></Grid>");
        xaml.Append("</StackPanel></Border>");
        return (Border)Microsoft.UI.Xaml.Markup.XamlReader.Load(xaml.ToString());
    }
}
