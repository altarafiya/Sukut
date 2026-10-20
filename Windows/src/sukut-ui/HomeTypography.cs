using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;

namespace Sukut;

public sealed partial class ShellView
{
    // Applied only when Home loads or language/theme is changed, not per poll.
    private void ApplyHomeTypography()
    {
        if (HomePanel == null) return;
        var face = (FontFamily)Application.Current.Resources[Arabic ? "SukutArabicFontFamily" : "ContentControlThemeFontFamily"];
        HomePanel.FontFamily = face;
        OutputButton.FontFamily = BufferingButton.FontFamily = face;
        OffCard.FontFamily = HalalCard.FontFamily = OptimizedCard.FontFamily = StrictCard.FontFamily = face;
        TextBlock[] labels = [HeaderSubtitle, OutputLabel, BufferingLabel, ModeHeading, StatusLabel, OffLabel, HalalLabel, OptimizedLabel, StrictLabel, ModeDetailTitle, ModeDetailDescription];
        foreach (var label in labels) label.FontFamily = face;
    }

    private void Typography_Loaded(object sender, RoutedEventArgs args)
    {
        if (sender is DependencyObject element) ArabicTypography.Apply(element, Arabic);
    }

    private void ApplyPageTypography()
    {
        ArabicTypography.Apply(Root, Arabic);
        ArabicTypography.Apply(OutputFlyout?.Content, Arabic);
        ArabicTypography.Apply(BufferingFlyout?.Content, Arabic);
        if (Root.XamlRoot != null)
            foreach (var popup in Microsoft.UI.Xaml.Media.VisualTreeHelper.GetOpenPopupsForXamlRoot(Root.XamlRoot))
                ArabicTypography.Apply(popup.Child, Arabic);
    }

    private void TypographyPopup_Opened(object sender, object args) => QueueTypography();

    private void QueueTypography() => DispatcherQueue.TryEnqueue(
        Microsoft.UI.Dispatching.DispatcherQueuePriority.Low, ApplyPageTypography);
}
