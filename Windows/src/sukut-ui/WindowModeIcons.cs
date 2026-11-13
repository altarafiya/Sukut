using System.Runtime.InteropServices;

namespace Sukut;
// The window and its taskbar button show the app icon (the phone's). Only the
// tray keeps the س status icon. Two HICONs owned by this window, sized per DPI.
internal sealed class WindowModeIcons : IDisposable
{
    private readonly IntPtr _window;
    private IntPtr _small,_large;
    private uint _dpi;
    internal WindowModeIcons(IntPtr window)=>_window=window;
    internal void SetMode(int mode,bool refreshTheme=false)
    {
        uint dpi=GetDpiForWindow(_window); if(dpi==0)dpi=96;
        if(dpi==_dpi && _small!=IntPtr.Zero)return;
        _dpi=dpi;
        string path=AppIcons.CurrentIconPath();
        Replace(ref _small,LoadImage(IntPtr.Zero,path,1,GetSystemMetricsForDpi(49,dpi),GetSystemMetricsForDpi(50,dpi),0x10),0);
        Replace(ref _large,LoadImage(IntPtr.Zero,path,1,GetSystemMetricsForDpi(11,dpi),GetSystemMetricsForDpi(12,dpi),0x10),1);
    }
    // The user chose another app icon: load it again at the current DPI.
    internal void Refresh(){_dpi=0;SetMode(0);}
    private void Replace(ref IntPtr current,IntPtr next,int kind)
    {
        if(next==IntPtr.Zero)return;
        SendMessage(_window,0x80,(IntPtr)kind,next);
        if(current!=IntPtr.Zero)DestroyIcon(current);current=next;
    }
    public void Dispose(){SendMessage(_window,0x80,IntPtr.Zero,IntPtr.Zero);SendMessage(_window,0x80,(IntPtr)1,IntPtr.Zero);if(_small!=IntPtr.Zero)DestroyIcon(_small);if(_large!=IntPtr.Zero)DestroyIcon(_large);_small=_large=IntPtr.Zero;}
    [DllImport("user32.dll",CharSet=CharSet.Unicode)] private static extern IntPtr LoadImage(IntPtr instance,string path,uint type,int cx,int cy,uint flags);
    [DllImport("user32.dll")] private static extern bool DestroyIcon(IntPtr icon);
    [DllImport("user32.dll")] private static extern uint GetDpiForWindow(IntPtr window);
    [DllImport("user32.dll")] private static extern int GetSystemMetricsForDpi(int index,uint dpi);
    [DllImport("user32.dll")] private static extern IntPtr SendMessage(IntPtr window,uint message,IntPtr w,IntPtr l);
}
