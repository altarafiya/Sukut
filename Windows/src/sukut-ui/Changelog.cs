using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace Sukut;

public sealed partial class ShellView
{
    private void RenderChangelog()
    {
        // The version is its own LTR, English run: inside Arabic text it would
        // otherwise be reordered and shaped with Arabic-Indic digits.
        var header = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 6 };
        header.Children.Add(new TextBlock { Text = L("What’s new ·", "الجديد ·") });
        header.Children.Add(new TextBlock { Text = ReleaseVersion, Language = "en-US", FlowDirection = FlowDirection.LeftToRight });
        ChangelogExpander.Header = header;
        Microsoft.UI.Xaml.Automation.AutomationProperties.SetName(ChangelogExpander, L($"What’s new {ReleaseVersion}", $"الجديد {ReleaseVersion}"));
        ChangelogIntro.Visibility=Visibility.Collapsed;
        string[] changes = [
            L("Bluetooth playback support and automatic switching when a physical audio device connects, with a notification.", "دعم تشغيل الصوت عبر البلوتوث والتبديل التلقائي عند توصيل جهاز صوت فعلي، مع إشعار."),
            L("Stopping returns Windows playback to the selected device; disconnected old devices no longer block restoration.", "يعيد الإيقاف تشغيل صوت ويندوز إلى الجهاز المحدد، ولا تمنع الأجهزة القديمة المفصولة استعادة الصوت."),
            L("Optimized mode has a background-level slider in the tray. Audio buffering uses the matching icon and colour.", "يتضمّن الوضع المحسّن شريطًا لمستوى الخلفية في منطقة الإشعارات، ويتغيّر رمز التخزين المؤقت ولونه بحسب الخيار."),
            L("Native Windows navigation, compact Start/Stop and a taller, responsive desktop layout.", "تنقّل ويندوز الأصلي وتشغيل وإيقاف مضغوط وتخطيط مكتبي أطول ومتجاوب."),
            L("Horizontal source-app cards with icons. Open desktop apps appear even before they play sound.", "بطاقات أفقية بأيقونات التطبيقات المصدر، وتظهر التطبيقات المفتوحة قبل تشغيل الصوت."),
            L("Illustrated phone setup in five clear steps, with larger buttons and Back to the previous step.", "إعداد هاتف مصوّر في خمس خطوات واضحة، مع أزرار أكبر ورجوع إلى الخطوة السابقة."),
            L("Output devices stay visible, with compact buffering choices and clear mode help.", "تظل أجهزة الإخراج ظاهرة، مع خيارات تخزين مؤقت مضغوطة وشرح واضح للأوضاع."),
            L("Low-memory native tray, a sleeping face for Standby, and a speaker icon for audio output.", "منطقة إشعارات أصلية خفيفة على الذاكرة، ووجه نائم للاستعداد وأيقونة مكبّر للصوت."),
            L("Includes Android 0.4 and USB activation for Sukut’s own audio worker, without Shizuku.", "يتضمّن أندرويد 0.4 وتفعيل عامل صوت سُكوت عبر USB دون Shizuku.")
        ];
        ChangelogCurrent.Children.Clear();
        foreach(string change in changes) {
            var row=new Grid {ColumnSpacing=10};row.ColumnDefinitions.Add(new(){Width=GridLength.Auto});row.ColumnDefinitions.Add(new(){Width=new GridLength(1,GridUnitType.Star)});
            var bullet=new TextBlock {Text="•",Foreground=PhoneBrush("ModeHalalText")};
            var text=new TextBlock {Text=change,TextWrapping=TextWrapping.Wrap};Grid.SetColumn(text,1);
            row.Children.Add(bullet);row.Children.Add(text);ChangelogCurrent.Children.Add(row);
        }
        foreach (var item in new[] { ChangelogWinUI, ChangelogMemory, ChangelogLatency, ChangelogModes, ChangelogTray, ChangelogGuide, ChangelogRouting, ChangelogDesign, ChangelogMeasurementNote }) item.Visibility = Visibility.Collapsed;
    }
}
