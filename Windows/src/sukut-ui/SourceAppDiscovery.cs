using System.Diagnostics;

namespace Sukut;

// On-demand UI discovery only. The engine still captures the selected PID and
// its descendants; no capture, routes or audio-session lifetime changes here.
internal static class SourceAppDiscovery
{
    internal static List<SourceApp> Merge(IEnumerable<SourceApp> sessions, IEnumerable<SourceApp> windows)
    {
        var seen = new HashSet<uint>();
        return sessions.Concat(windows).Where(app => app.Pid != 0 &&
            app.Name is not ("codex-computer-use" or "TextInputHost" or "SukutWindow" or "Sukut") && seen.Add(app.Pid))
            .OrderBy(app => app.Name, StringComparer.CurrentCultureIgnoreCase).ToList();
    }

    internal static Task<List<SourceApp>> RunningWindows() => Task.Run(() =>
    {
        var result = new List<SourceApp>();
        using var current = Process.GetCurrentProcess();
        int session = current.SessionId;
        foreach (var process in Process.GetProcesses())
        {
            using (process)
            {
                try
                {
                    if (process.Id == Environment.ProcessId || process.SessionId != session ||
                        process.MainWindowHandle == IntPtr.Zero || string.IsNullOrWhiteSpace(process.MainWindowTitle)) continue;
                    string name = process.ProcessName;
                    if (name is "Sukut" or "SukutWindow" or "explorer" or "ApplicationFrameHost" or "SystemSettings" or "TextInputHost" or "codex-computer-use") continue;
                    // An inaccessible or terminated process cannot be selected reliably.
                    if (string.IsNullOrEmpty(process.MainModule?.FileName)) continue;
                    string display = name switch { "msedge" => "Microsoft Edge", "chrome" => "Google Chrome", "firefox" => "Firefox", "steam" => "Steam", "steamwebhelper" => "Steam", "ProtonVPN.Client" => "Proton VPN", _ => name };
                    result.Add(new SourceApp { Pid = checked((uint)process.Id), Name = display });
                }
                catch { /* Protected or closing windows do not hide other apps. */ }
            }
        }
        return result;
    });
}
