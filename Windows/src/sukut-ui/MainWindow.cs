using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Media;

namespace Sukut;

// A FrameworkElement root gives compiled x:Load bindings their own namescope.
// Window itself has no FindName; ShellView owns the deferred page tree.
public sealed class MainWindow : Window
{
    private readonly ShellView _view;
    private readonly MicaBackdrop _backdrop;
    public MainWindow()
    {
        Title = "سُكوت · Sukut";
        _view = new ShellView(this);
        Content = _view;
        _backdrop = new MicaBackdrop { Kind = Microsoft.UI.Composition.SystemBackdrops.MicaKind.Base };
        if (!new Windows.UI.ViewManagement.AccessibilitySettings().HighContrast) SystemBackdrop = _backdrop;
        _view.InitializeWindow();
    }
}
