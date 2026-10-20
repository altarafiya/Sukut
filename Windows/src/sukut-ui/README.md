# Sukut WinUI 3 — 0.2.27

Layout: `ShellView.xaml`; shared shell, navigation and device refresh:
`ShellView.xaml.cs`. Home typography: `HomeTypography.cs`. Window/work-area sizing:
`WindowSizing.cs`, `PageLayout.cs`. Mode descriptions/sliders: `ModeControls.cs`.
Buffer menu/setup control: `BufferingControl.xaml/.cs`, `BufferingSettings.cs`.
First-run wizard: `SetupFlow.cs`. Phone wizard: `PhoneSetup.cs`; verified download,
ADB and package operations: `PhoneTools.cs`. About text: `Changelog.cs`.

`Engine.cs` marshals the existing native API off the UI thread. `SettingsStore.cs`
serializes saved preferences. `AudioRestartPolicy.cs` and `AutomaticRestart.cs`
handle bounded retries. `TrayIcon.cs` and `NativeTrayMenu.cs` own the native tray.

All voice presets stay 0 dB; Optimized music muted/background −20…+10 dB.
Sliders commit on release. Model, buffer durations and gain curves are unchanged.
Closed-source restoration is deferred safely by AudioRouting; Poll handles the
ended source with normal OFF and keeps the UI ready. `Assets/Phone/Sukut.apk` is the current Android APK
plus a SHA-256 file; do not replace it without updating that checksum.

Build: `../../scripts/build-sukut.ps1`. Current source and checks:
`../../docs/redesign/V0226_ARABIC_INSTALLER.md`. Old source README entries
are preserved in `../../../docs/history/2026-10-18-before-polish/` from the repo root.

Home Source app is OutputSourceAppChoice inside OutputFlyout; RenderSourceAppChoices
keeps it synchronized with Advanced/AppChoice. App_Changed handles both controls.

SourceAppIcons.cs supplies Windows thumbnails with Fluent fallback,24px decode
and a48-entry path cache. DeviceDropdowns.cs resets the child ComboBox closed
and cancels pending reopening when the parent flyout closes.

Shared Arabic typography: ArabicTypography.cs; HomeTypography.cs wires page/popup events. Arabic captions14/body16 DIP, real semibold, safe bounds. Loaded handlers cover templates; no per-poll traversal.
