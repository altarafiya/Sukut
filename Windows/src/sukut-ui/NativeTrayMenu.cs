using System.Runtime.InteropServices;

namespace Sukut;

// Windows owns fonts, metrics, painting, accessibility and menu interaction.
// No owner-draw messages, custom brushes or application palette are involved.
internal sealed class NativeTrayMenu : IDisposable
{
    private IntPtr _menu;
    private readonly bool _rtl;
    [StructLayout(LayoutKind.Sequential)] private struct Point { public int X, Y; }
    [DllImport("user32.dll")] private static extern IntPtr CreatePopupMenu();
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern bool AppendMenu(IntPtr menu, uint flags, UIntPtr id, string? label);
    [DllImport("user32.dll")] private static extern bool SetMenuDefaultItem(IntPtr menu, uint item, bool byPosition);
    [DllImport("user32.dll")] private static extern uint TrackPopupMenuEx(IntPtr menu, uint flags, int x, int y, IntPtr owner, IntPtr parameters);
    [DllImport("user32.dll")] private static extern bool DestroyMenu(IntPtr menu);
    [DllImport("user32.dll")] private static extern bool GetCursorPos(out Point point);
    [DllImport("user32.dll")] private static extern int GetSystemMetrics(int index);
    [DllImport("user32.dll")] private static extern bool SetForegroundWindow(IntPtr owner);
    [DllImport("user32.dll")] private static extern bool PostMessage(IntPtr owner, uint message, IntPtr w, IntPtr l);

    internal NativeTrayMenu(string open, string off, string exit)
    {
        _rtl = open.Any(c => c is >= '\u0600' and <= '\u06ff');
        _menu = CreatePopupMenu();
        if (_menu == IntPtr.Zero) return;
        AppendMenu(_menu, 0, (UIntPtr)1, open);
        AppendMenu(_menu, 0, (UIntPtr)2, off);
        AppendMenu(_menu, 0x800, UIntPtr.Zero, null);
        AppendMenu(_menu, 0, (UIntPtr)3, exit);
        SetMenuDefaultItem(_menu, 1, false);
    }
    internal uint Show(IntPtr owner)
    {
        if (_menu == IntPtr.Zero || !GetCursorPos(out var point)) return 0;
        NativeMenuTheme.Apply(owner);
        SetForegroundWindow(owner);
        uint flags = 0x100 | 0x80 | 0x2; // RETURNCMD, NONOTIFY, RIGHTBUTTON
        if (GetSystemMetrics(40) != 0) flags |= 0x8; // system menu drop alignment
        if (_rtl) flags |= 0x8000; // TPM_LAYOUTRTL
        try { return TrackPopupMenuEx(_menu, flags, point.X, point.Y, owner, IntPtr.Zero); }
        finally { PostMessage(owner, 0, IntPtr.Zero, IntPtr.Zero); }
    }
    public void Dispose()
    {
        if (_menu == IntPtr.Zero) return;
        DestroyMenu(_menu); _menu = IntPtr.Zero;
    }
}
