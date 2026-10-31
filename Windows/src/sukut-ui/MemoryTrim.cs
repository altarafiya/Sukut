using System.Runtime.InteropServices;

namespace Sukut;

// Minimized: hand the window's pages back to Windows; they return on demand
// when it is shown again. Audio runs in the host, so this never touches it.
public sealed partial class ShellView
{
    private bool _trimmedWhileMinimized;

    private void TrimWhenMinimized()
    {
        if (!_visible || !Minimized) { _trimmedWhileMinimized = false; return; }
        if (_trimmedWhileMinimized) return;
        _trimmedWhileMinimized = true;
        GC.Collect(2, GCCollectionMode.Aggressive, blocking: true, compacting: true);
        SetProcessWorkingSetSize(GetCurrentProcess(), -1, -1);
    }

    [DllImport("kernel32.dll")] private static extern IntPtr GetCurrentProcess();
    [DllImport("kernel32.dll")] private static extern bool SetProcessWorkingSetSize(IntPtr process, nint minimum, nint maximum);
}
