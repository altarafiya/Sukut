using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace Sukut;

public sealed partial class ShellView
{
    private static string ReleaseVersion => typeof(ShellView).Assembly
        .GetCustomAttributes(typeof(System.Reflection.AssemblyInformationalVersionAttribute), false)
        .OfType<System.Reflection.AssemblyInformationalVersionAttribute>().FirstOrDefault()?.InformationalVersion.Split('+')[0]
        ?? "0.4";

    private void PageViewport_SizeChanged(object sender, SizeChangedEventArgs args) { UpdateDesktopChrome(); FitPageViewport(); if (_page == 1) UpdateBrandInView(); }

    private void DesktopNavigation_DisplayModeChanged(NavigationView sender, NavigationViewDisplayModeChangedEventArgs args)
    {
        UpdateDesktopChrome(); FitPageViewport();
    }

    private void UpdateDesktopChrome()
    {
        if (PageHost == null || DesktopNavigation == null) return;
        // Keep the rail compact at launch; only the user's menu command opens it.
        var paneMode = Root.ActualWidth > 0 && Root.ActualWidth < 620
            ? NavigationViewPaneDisplayMode.LeftMinimal : NavigationViewPaneDisplayMode.LeftCompact;
        if (DesktopNavigation.PaneDisplayMode != paneMode) {
            DesktopNavigation.PaneDisplayMode = paneMode;
            DesktopNavigation.IsPaneOpen = false;
        }
        // In Minimal mode the native menu toggle overlays the content's leading
        // corner. Reserve its row so it cannot cover the page Back command.
        double top = DesktopNavigation.DisplayMode == NavigationViewDisplayMode.Minimal ? 52 : 16;
        if (PageHost.Margin.Top != top) PageHost.Margin = new Thickness(24, top, 24, 0);
    }

    // Home uses the window's width for two columns; Settings and Phone keep a
    // comfortable reading width, centred.
    private const double HomeMaxWidth = 1040, PageMaxWidth = 760;

    private void FitPageViewport()
    {
        if (Root == null || PageHost == null || Root.ActualWidth <= 0) return;
        // Constrain the content before a deferred page can ask its ScrollViewer
        // to measure an oversized, centered child.
        double available = Math.Max(0, DesktopContent.ActualWidth - PageHost.Margin.Left - PageHost.Margin.Right);
        double width = Math.Min(_page == 0 ? HomeMaxWidth : PageMaxWidth, available);
        if (double.IsNaN(PageHost.Width) || Math.Abs(PageHost.Width - width) > .5) PageHost.Width = width;
        foreach (var panel in new FrameworkElement?[] { HomePanel, SettingsPanel, PhonePanel })
            if (panel != null) panel.Width = width;
    }
}
