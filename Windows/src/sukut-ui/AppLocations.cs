using System.Runtime.InteropServices;

namespace Sukut;

// MSIX installation folders are read-only. Audio assets remain in the package;
// preferences and the routing journal belong to this user's application data.
internal static class AppLocations
{
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
    private static extern int GetCurrentPackageFullName(ref uint length, IntPtr packageFullName);

    internal static bool IsPackaged
    {
        get
        {
            uint length = 0;
            return GetCurrentPackageFullName(ref length, IntPtr.Zero) == 122;
        }
    }

    internal static string ResourceRoot { get; } = FindResourceRoot();
    internal static string DataRoot { get; } = IsPackaged
        ? Path.Combine(Windows.Storage.ApplicationData.Current.LocalFolder.Path, "Sukut")
        : ResourceRoot;

    internal static string RuntimeDirectory => Path.Combine(DataRoot, "runtime");

    private static string FindResourceRoot()
    {
        DirectoryInfo? parent = new(AppContext.BaseDirectory);
        for (int depth = 0; depth < 9 && parent != null; depth++, parent = parent.Parent)
            if (File.Exists(Path.Combine(parent.FullName, "sukut-package.json")) ||
                File.Exists(Path.Combine(parent.FullName, "docs", "route.json")))
                return parent.FullName;
        throw new InvalidOperationException("Sukut audio files are missing. Keep its app and assets folders together.");
    }

    internal static void WriteCrash(string message)
    {
        try
        {
            Directory.CreateDirectory(RuntimeDirectory);
            File.AppendAllText(Path.Combine(RuntimeDirectory, "sukut-crash.log"),
                DateTimeOffset.Now + " " + message + Environment.NewLine);
        }
        catch { }
    }
}
