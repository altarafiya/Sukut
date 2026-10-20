using System.Runtime.InteropServices;
using Microsoft.UI.Xaml;
using Microsoft.Win32;
using Windows.UI.ViewManagement;

namespace Sukut;

public sealed class TrayIcon : IDisposable
{
    private readonly IntPtr _hwnd;
    private readonly Microsoft.UI.Dispatching.DispatcherQueue _dispatcher;
    private readonly string _assets;
    private readonly SubclassProc _procedure;
    private readonly Action _open, _off, _exit, _appearanceChanged;
    private readonly uint _taskbarCreated, _showMessage;
    // Cache one system-size handle per state/theme; polling never reloads files.
    private readonly IntPtr[] _icons = new IntPtr[4];
    private bool _active, _disposed, _darkTaskbar, _highContrast;
    private int _dpi;
    private string _openLabel = "Open Sukut", _offLabel = "OFF", _exitLabel = "Exit";
    private Data _data;
    private const uint Callback = 0x8000 + 91;
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)] private struct Data
    {
        public int Size; public IntPtr Window; public uint Id, Flags, CallbackMessage; public IntPtr Icon;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)] public string Tip;
        public uint State, StateMask;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)] public string Info;
        public uint Version;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)] public string InfoTitle;
        public uint InfoFlags; public Guid Guid; public IntPtr BalloonIcon;
    }
    private delegate IntPtr SubclassProc(IntPtr hwnd, uint message, IntPtr w, IntPtr l, UIntPtr id, IntPtr data);
    [DllImport("shell32.dll", CharSet = CharSet.Unicode)] private static extern bool Shell_NotifyIcon(uint message, ref Data data);
    [DllImport("comctl32.dll")] private static extern bool SetWindowSubclass(IntPtr hwnd, SubclassProc proc, UIntPtr id, IntPtr data);
    [DllImport("comctl32.dll")] private static extern bool RemoveWindowSubclass(IntPtr hwnd, SubclassProc proc, UIntPtr id);
    [DllImport("comctl32.dll")] private static extern IntPtr DefSubclassProc(IntPtr hwnd, uint message, IntPtr w, IntPtr l);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern IntPtr LoadImage(IntPtr instance, string file, uint type, int x, int y, uint flags);
    [DllImport("user32.dll")] private static extern bool DestroyIcon(IntPtr icon);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern uint RegisterWindowMessage(string name);
    [DllImport("user32.dll")] private static extern uint GetDpiForWindow(IntPtr window);
    [DllImport("user32.dll")] private static extern int GetSystemMetricsForDpi(int index, uint dpi);
    [DllImport("user32.dll")] private static extern uint GetSysColor(int index);

    public TrayIcon(Window window, string assets, Action open, Action off, Action exit, Action appearanceChanged)
    {
        _hwnd = WinRT.Interop.WindowNative.GetWindowHandle(window); _assets = assets; _dispatcher = window.DispatcherQueue;
        NativeMenuTheme.Apply(_hwnd);
        _open = open; _off = off; _exit = exit; _appearanceChanged = appearanceChanged;
        _procedure = Handle; _taskbarCreated = RegisterWindowMessage("TaskbarCreated"); _showMessage = RegisterWindowMessage("Sukut.ShowWindow");
        ReadAppearance(); LoadIcons();
        _data = new Data { Size = Marshal.SizeOf<Data>(), Window = _hwnd, Id = 1, Flags = 1 | 2 | 4 | 0x80, CallbackMessage = Callback, Icon = CurrentIcon(), Tip = "سُكوت Sukut", Info = "", InfoTitle = "" };
        if (!SetWindowSubclass(_hwnd, _procedure, (UIntPtr)91, IntPtr.Zero) || !Shell_NotifyIcon(0, ref _data))
        {
            RemoveWindowSubclass(_hwnd, _procedure, (UIntPtr)91); FreeIcons();
            throw new InvalidOperationException("Sukut tray icon could not be created");
        }
        _data.Version = 4; Shell_NotifyIcon(4, ref _data);
    }
    private void LoadIcons()
    {
        int dpi = (int)GetDpiForWindow(_hwnd);
        if (dpi == 0) dpi = 96;
        int size = Math.Max(16, GetSystemMetricsForDpi(49, (uint)dpi));
        string[] files = ["Tray-on-dark.ico", "Tray-off-dark.ico", "Tray-on-light.ico", "Tray-off-light.ico"];
        var loaded = new IntPtr[files.Length];
        try
        {
            for (int i = 0; i < files.Length; i++)
            {
                loaded[i] = LoadImage(IntPtr.Zero, Path.Combine(_assets, files[i]), 1, size, size, 0x10);
                if (loaded[i] == IntPtr.Zero) throw new InvalidOperationException("A Sukut tray icon is missing: " + files[i]);
            }
        }
        catch
        {
            foreach (var icon in loaded) if (icon != IntPtr.Zero) DestroyIcon(icon);
            throw;
        }
        FreeIcons(); Array.Copy(loaded, _icons, loaded.Length); _dpi = dpi;
    }
    private void FreeIcons()
    {
        for (int i = 0; i < _icons.Length; i++)
        {
            if (_icons[i] != IntPtr.Zero) DestroyIcon(_icons[i]);
            _icons[i] = IntPtr.Zero;
        }
    }
    private void ReadAppearance()
    {
        // The taskbar follows Windows mode, independently of Sukut's app theme.
        try
        {
            using var key = Registry.CurrentUser.OpenSubKey(@"Software\Microsoft\Windows\CurrentVersion\Themes\Personalize");
            _darkTaskbar = key?.GetValue("SystemUsesLightTheme") is int value && value == 0;
        }
        catch { _darkTaskbar = false; }
        _highContrast = new AccessibilitySettings().HighContrast;
        if (_highContrast)
        {
            uint background = GetSysColor(5); // COLOR_WINDOW in the contrast palette
            _darkTaskbar = (background & 255) * 299 + ((background >> 8) & 255) * 587
                + ((background >> 16) & 255) * 114 < 128000;
        }
    }
    private IntPtr CurrentIcon() => _icons[(_darkTaskbar ? 0 : 2) + (_active && !_highContrast ? 0 : 1)];
    public void SetActive(bool active) { if (_disposed || _active == active) return; _active = active; SwapIcon(); }
    public void SetLabels(string open, string off, string exit) { _openLabel = open; _offLabel = off; _exitLabel = exit; }
    public void SetTip(string tip)
    {
        tip = tip.Length > 127 ? tip[..127] : tip;
        if (_disposed || _data.Tip == tip) return;
        _data.Tip = tip; Shell_NotifyIcon(1, ref _data);
    }
    private void SwapIcon() { var icon = CurrentIcon(); if (_data.Icon == icon) return; _data.Icon = icon; Shell_NotifyIcon(1, ref _data); }
    private IntPtr Handle(IntPtr hwnd, uint message, IntPtr w, IntPtr l, UIntPtr id, IntPtr data)
    {
        if (message == _taskbarCreated) { ReadAppearance(); _data.Icon = CurrentIcon(); Shell_NotifyIcon(0, ref _data); _data.Version = 4; Shell_NotifyIcon(4, ref _data); }
        if (message == _showMessage) { _dispatcher.TryEnqueue(() => _open()); return IntPtr.Zero; }
        if (message is 0x1A or 0x31A) // WM_SETTINGCHANGE / WM_THEMECHANGED
        {
            NativeMenuTheme.Apply(hwnd);
            _dispatcher.TryEnqueue(() =>
            {
                if (_disposed) return;
                ReadAppearance(); SwapIcon();
                _appearanceChanged();
            });
        }
        if (message == 0x2E0 && (int)GetDpiForWindow(_hwnd) != _dpi) { LoadIcons(); _data.Icon = IntPtr.Zero; SwapIcon(); }
        if (message == 5) _dispatcher.TryEnqueue(() => _appearanceChanged());
        if (message == Callback)
        {
            uint action = (uint)(l.ToInt64() & 0xffff);
            if (action is 0x202 or 0x203 or 0x400 or 0x401) _dispatcher.TryEnqueue(() => _open());
            else if (action is 0x205 or 0x7b)
            {
                using var menu = new NativeTrayMenu(_openLabel, _offLabel, _exitLabel);
                uint choice = menu.Show(_hwnd);
                if (choice == 1) _dispatcher.TryEnqueue(() => _open()); else if (choice == 2) _dispatcher.TryEnqueue(() => _off()); else if (choice == 3) _dispatcher.TryEnqueue(() => _exit());
            }
            return IntPtr.Zero;
        }
        return DefSubclassProc(hwnd, message, w, l);
    }
    public void Dispose()
    {
        if (_disposed) return; _disposed = true;
        Shell_NotifyIcon(2, ref _data); RemoveWindowSubclass(_hwnd, _procedure, (UIntPtr)91); FreeIcons();
    }
}
