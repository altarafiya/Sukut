using System.Runtime.InteropServices;
using Microsoft.UI.Xaml;

namespace Sukut;

/// <summary>
/// Messages from the tray host to this window: "show yourself" (tray "Open
/// Sukut", a second launch) and "Sukut is exiting" (tray "Exit"). Also relays
/// Windows theme/size changes the window reacts to.
/// </summary>
internal sealed class HostLink : IDisposable
{
    private readonly IntPtr _hwnd;
    private readonly Microsoft.UI.Dispatching.DispatcherQueue _dispatcher;
    private readonly SubclassProc _procedure;
    private readonly uint _show, _exit;
    private readonly Action _onShow, _onExit, _onAppearance;
    private bool _disposed;
    private delegate IntPtr SubclassProc(IntPtr hwnd, uint message, IntPtr w, IntPtr l, UIntPtr id, IntPtr data);
    [DllImport("comctl32.dll")] private static extern bool SetWindowSubclass(IntPtr hwnd, SubclassProc proc, UIntPtr id, IntPtr data);
    [DllImport("comctl32.dll")] private static extern bool RemoveWindowSubclass(IntPtr hwnd, SubclassProc proc, UIntPtr id);
    [DllImport("comctl32.dll")] private static extern IntPtr DefSubclassProc(IntPtr hwnd, uint message, IntPtr w, IntPtr l);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern uint RegisterWindowMessage(string name);

    internal IntPtr Handle => _hwnd;

    internal HostLink(Window window, Action onShow, Action onExit, Action onAppearance)
    {
        _hwnd = WinRT.Interop.WindowNative.GetWindowHandle(window);
        _dispatcher = window.DispatcherQueue;
        _onShow = onShow; _onExit = onExit; _onAppearance = onAppearance;
        _show = RegisterWindowMessage("Sukut.ShowWindow");
        _exit = RegisterWindowMessage("Sukut.HostExit");
        _procedure = Procedure;
        SetWindowSubclass(_hwnd, _procedure, (UIntPtr)92, IntPtr.Zero);
    }

    private IntPtr Procedure(IntPtr hwnd, uint message, IntPtr w, IntPtr l, UIntPtr id, IntPtr data)
    {
        if (message == _show) { _dispatcher.TryEnqueue(() => _onShow()); return IntPtr.Zero; }
        if (message == _exit) { _dispatcher.TryEnqueue(() => _onExit()); return IntPtr.Zero; }
        if (message is 0x1A or 0x31A or 5) _dispatcher.TryEnqueue(() => { if (!_disposed) _onAppearance(); }); // WM_SETTINGCHANGE, WM_THEMECHANGED, WM_SIZE
        return DefSubclassProc(hwnd, message, w, l);
    }

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        RemoveWindowSubclass(_hwnd, _procedure, (UIntPtr)92);
    }
}
