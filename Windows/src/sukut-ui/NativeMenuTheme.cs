using System.Runtime.InteropServices;
using Windows.UI.ViewManagement;

namespace Sukut;

// Keep the standard HMENU renderer, but opt the process/window into Windows'
// dark menus. These private exports are also used by Microsoft PowerToys:
// https://github.com/microsoft/PowerToys/blob/main/src/modules/ZoomIt/ZoomIt/Utility.cpp
// They are optional: an unavailable export leaves a working native menu.
internal static class NativeMenuTheme
{
    private enum PreferredAppMode { Default, AllowDark }
    private static bool _unavailable;
    private static bool _applying;
    [DllImport("uxtheme.dll", EntryPoint = "#135", ExactSpelling = true)]
    [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
    private static extern PreferredAppMode SetPreferredAppMode(PreferredAppMode mode);
    [DllImport("uxtheme.dll", EntryPoint = "#133", ExactSpelling = true)]
    [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
    [return: MarshalAs(UnmanagedType.I1)]
    private static extern bool AllowDarkModeForWindow(IntPtr window, [MarshalAs(UnmanagedType.I1)] bool allow);
    [DllImport("uxtheme.dll", EntryPoint = "#132", ExactSpelling = true)]
    [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
    [return: MarshalAs(UnmanagedType.I1)]
    private static extern bool ShouldAppsUseDarkMode();
    [DllImport("uxtheme.dll", EntryPoint = "#104", ExactSpelling = true)]
    [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
    private static extern void RefreshImmersiveColorPolicyState();
    [DllImport("uxtheme.dll", EntryPoint = "#136", ExactSpelling = true)]
    [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
    private static extern void FlushMenuThemes();

    internal static void Apply(IntPtr owner)
    {
        if (_unavailable || _applying || !OperatingSystem.IsWindowsVersionAtLeast(10, 0, 18362)) return;
        _applying = true;
        try
        {
            RefreshImmersiveColorPolicyState();
            bool highContrast = new AccessibilitySettings().HighContrast;
            SetPreferredAppMode(highContrast ? PreferredAppMode.Default : PreferredAppMode.AllowDark);
            AllowDarkModeForWindow(owner, !highContrast && ShouldAppsUseDarkMode());
            FlushMenuThemes();
        }
        catch (EntryPointNotFoundException) { _unavailable = true; }
        catch (DllNotFoundException) { _unavailable = true; }
        finally { _applying = false; }
    }
}
