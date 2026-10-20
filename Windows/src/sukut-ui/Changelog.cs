namespace Sukut;

public sealed partial class ShellView
{
    private void RenderChangelog()
    {
        ChangelogExpander.Header = L($"What’s new · {ReleaseVersion}", $"الجديد · {ReleaseVersion}");
        ChangelogIntro.Text = L("What’s new · " + ReleaseVersion, "الجديد · " + ReleaseVersion);
        ChangelogCurrent.Text = L("Phone: Install, Update or Full setup.\nFixes source-app routing and switching back to All system audio.\nIncludes Android 0.3.31 with improved OFF and screen-recorder cleanup.", "الهاتف: تثبيت، تحديث أو إعداد كامل.\nإصلاح توجيه صوت التطبيقات والعودة إلى صوت النظام بالكامل.\nيتضمّن أندرويد 0.3.31 مع تحسين إيقاف المعالجة واستعادة صوت تسجيل الشاشة.");
        foreach (var item in new[] { ChangelogWinUI, ChangelogMemory, ChangelogLatency, ChangelogModes, ChangelogTray, ChangelogGuide, ChangelogRouting, ChangelogDesign, ChangelogMeasurementNote }) item.Visibility = Microsoft.UI.Xaml.Visibility.Collapsed;
    }
}
