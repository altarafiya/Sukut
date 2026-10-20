using Microsoft.UI.Xaml;
using System.Runtime.InteropServices;

namespace Sukut;

public partial class App : Application
{
    internal static string DiagnosticStage = "application";
    private MainWindow? _window;
    private Mutex? _instance;
    private bool _ownsInstance;
    public App()
    {
        InitializeComponent();
        UnhandledException += (_, args) =>
        {
            try { File.AppendAllText(Path.Combine(AppContext.BaseDirectory, "sukut-crash.log"), DateTimeOffset.Now + " UNHANDLED " + DiagnosticStage + " " + args.Message + " " + args.Exception + Environment.NewLine + Environment.StackTrace + Environment.NewLine); }
            catch { }
        };
    }
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern uint RegisterWindowMessage(string name);
    [DllImport("user32.dll")] private static extern bool PostMessage(IntPtr hwnd, uint message, IntPtr w, IntPtr l);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern int MessageBox(IntPtr hwnd, string text, string caption, uint flags);
    protected override void OnLaunched(LaunchActivatedEventArgs args)
    {
        _instance = new Mutex(false, @"Local\SukutWinUI");
        try { _ownsInstance = _instance.WaitOne(0); }
        catch (AbandonedMutexException) { _ownsInstance = true; }
        if (!_ownsInstance)
        {
            PostMessage((IntPtr)0xffff, RegisterWindowMessage("Sukut.ShowWindow"), IntPtr.Zero, IntPtr.Zero);
            _instance.Dispose(); _instance = null; Exit(); return;
        }
        try
        {
            _window = new MainWindow();
            // A handled WinUI close means hide-to-tray, not process shutdown.
            _window.Closed += (_, eventArgs) => { if (!eventArgs.Handled) ReleaseInstance(); };
            _window.Activate();
        }
        catch (Exception error)
        {
            try { File.AppendAllText(Path.Combine(AppContext.BaseDirectory, "sukut-crash.log"), DateTimeOffset.Now + " " + error + Environment.NewLine); } catch { }
            MessageBox(IntPtr.Zero, error.Message, "سُكوت · Sukut", 0x10);
            ReleaseInstance(); Exit();
        }
    }
    internal void ReleaseInstance()
    {
        if (_instance == null) return;
        if (_ownsInstance) { _instance.ReleaseMutex(); _ownsInstance = false; }
        _instance.Dispose(); _instance = null;
    }
}
