using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Media.Imaging;

namespace Sukut;

public sealed partial class ShellView
{
    private bool _guideScreenshotOpen;

    private async void GuideScreenshot_Click(object sender, RoutedEventArgs args)
    {
        if (_guideScreenshotOpen || sender is not Button { Tag: string name }) return;
        if (name is not ("vb-cable-panel.png" or "vb-cable-options-menu.png" or "windows-volume-mixer.png")) return;
        _guideScreenshotOpen = true;
        try
        {
            var picture = new Image
            {
                Source = new BitmapImage(new Uri("ms-appx:///Assets/Setup/" + name)) { DecodePixelWidth = name == "vb-cable-options-menu.png" ? 344 : 1000 },
                Stretch = Stretch.Uniform,
                HorizontalAlignment = HorizontalAlignment.Center,
                Width = name == "vb-cable-options-menu.png" ? 344 : Math.Max(280, Math.Min(800, Root.ActualWidth - 160))
            };
            var dialog = new ContentDialog
            {
                XamlRoot = Root.XamlRoot,
                RequestedTheme = RequestedTheme,
                Title = L("Real screenshot", "صورة فعلية"),
                CloseButtonText = L("Close", "إغلاق"),
                DefaultButton = ContentDialogButton.Close,
                Content = new ScrollViewer
                {
                    MaxHeight = Math.Max(180, Math.Min(520, Root.ActualHeight - 180)),
                    HorizontalScrollBarVisibility = ScrollBarVisibility.Auto,
                    VerticalScrollBarVisibility = ScrollBarVisibility.Auto,
                    Content = picture
                }
            };
            dialog.Opened += (_, _) => ArabicTypography.Apply(dialog, Arabic);
            await dialog.ShowAsync();
        }
        catch (Exception error) { Error(L("Could not open screenshot: ", "تعذّر فتح الصورة: ") + error.Message); }
        finally { _guideScreenshotOpen = false; }
    }
}
