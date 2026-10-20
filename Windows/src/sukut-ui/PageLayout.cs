using Microsoft.UI.Xaml;

namespace Sukut;

public sealed partial class ShellView
{
    private static string ReleaseVersion => typeof(ShellView).Assembly.GetName().Version?.ToString(3) ?? "0.2.33";

    private void PageViewport_SizeChanged(object sender, SizeChangedEventArgs args) => FitPageViewport();

    private void FitPageViewport()
    {
        if (Navigation == null || PageHost == null || Navigation.ActualWidth <= Navigation.CompactPaneLength) return;
        // Constrain the content before a deferred page can ask the navigation
        // presenter/ScrollViewer to measure an oversized, centered child.
        double width = Math.Max(0, Navigation.ActualWidth - Navigation.CompactPaneLength - PageHost.Margin.Left - PageHost.Margin.Right - 8);
        if (double.IsNaN(PageHost.Width) || Math.Abs(PageHost.Width - width) > .5) PageHost.Width = width;
        foreach (var panel in new FrameworkElement?[] { HomePanel, SettingsPanel, AboutPanel, PhonePanel })
            if (panel != null) panel.Width = width;
        if (AboutContent != null) AboutContent.Width = Math.Min(560, width);
    }
}
