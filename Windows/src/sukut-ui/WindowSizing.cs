using System.Runtime.InteropServices;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Windows.Graphics;

namespace Sukut;

internal static class WindowSizing
{
    private sealed class DpiState { internal uint Value; internal int WorkWidth, WorkHeight; }
    private static readonly System.Runtime.CompilerServices.ConditionalWeakTable<Window, DpiState> States = new();
    [DllImport("user32.dll")] private static extern uint GetDpiForWindow(IntPtr window);
    internal static void Initialize(Window window)
    {
        IntPtr handle = WinRT.Interop.WindowNative.GetWindowHandle(window);
        double scale = GetDpiForWindow(handle) / 96.0;
        int width = (int)Math.Round(840 * scale), height = (int)Math.Round(780 * scale);
        UpdateMinimum(window);
        var work = DisplayArea.GetFromWindowId(window.AppWindow.Id, DisplayAreaFallback.Nearest).WorkArea;
        width = Math.Min(width, work.Width); height = Math.Min(height, work.Height);
        window.AppWindow.MoveAndResize(new RectInt32(work.X + Math.Max(0, (work.Width - width) / 2), work.Y + Math.Max(0, (work.Height - height) / 2), width, height));
    }
    internal static void UpdateMinimum(Window window)
    {
        uint dpi = GetDpiForWindow(WinRT.Interop.WindowNative.GetWindowHandle(window));
        var work = DisplayArea.GetFromWindowId(window.AppWindow.Id, DisplayAreaFallback.Nearest).WorkArea;
        var state = States.GetOrCreateValue(window);
        if (state.Value == dpi && state.WorkWidth == work.Width && state.WorkHeight == work.Height) return;
        state.Value = dpi; // Record before setters can raise a nested window event.
        state.WorkWidth = work.Width; state.WorkHeight = work.Height;
        double scale = dpi / 96.0;
        if (window.AppWindow.Presenter is OverlappedPresenter presenter)
        {
            int width = Math.Min((int)Math.Round(700 * scale), work.Width), height = Math.Min((int)Math.Round(560 * scale), work.Height);
            presenter.PreferredMinimumWidth = width;
            presenter.PreferredMinimumHeight = height;
        }
    }
}
