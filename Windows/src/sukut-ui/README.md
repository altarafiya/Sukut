# Sukut WinUI 3 — 0.4

The main window uses a native NavigationView for Audio, Phone and Settings. Source: ShellView.xaml/.cs. Desktop sizing and responsive content measurement: WindowSizing.cs and PageLayout.cs. HomePresentation.cs owns the compact Start/Stop presentation and responsive side-by-side playback panel; HomeTypography.cs and ArabicTypography.cs apply language typography only on load/change.

Default 1040 × 720 DIP; minimum 420 × 480, clamped to the work area. Mode rows, device/source lists and compact buffering controls adapt to the navigation content width. Deferred pages use x:Load. Navigation has no slide/fade or idle animation. Optimized's slider commits on release; voice gains, background range, buffer durations and engine commands are unchanged.

The tray belongs entirely to Windows/src/sukut-host (Sukut.exe), which owns SukutEngine.dll. TrayCatalog provides device-specific output glyphs and an on-demand visible-window/audio-session merge for source apps. No idle scanning or GUI process was added; layout and menus remain unchanged. HostLink connects the on-demand window; closing to tray ends the GUI process. Engine.cs/HostEngine.cs and audio restart/routing policies remain unchanged.

Phone UI: PhoneSetup.cs/PhoneConnectionCard.cs. PhoneTools.cs and PhoneWorkerActivation.cs provide the existing verified USB workflow. Assets/Phone bundles Android 0.4/code54/com.altarafiyah.sukut plus APK/bootstrap SHA256 files; update hashes whenever assets change. Old com.osama.sukut installs are separate apps.

Build: Windows/scripts/build-sukut.ps1; package: Windows/scripts/package-sukut.ps1. Both infer their default output directory from the project version. Both recognize Android 0.4. UI-only publish can point SukutNativeDirectory to a verified existing native folder; keep host, engine and sealed assets together.

Details, backup, verification and remaining release checks: docs/release-0.4/WINDOWS.md and work/windows-release-0.4/VERIFICATION.json. The portable package uses stable 0.4 metadata. No installer or Store publication is performed by this GUI update.

SourceAppDiscovery.cs merges on-demand audio-session results with visible running desktop apps. It does not alter the engine or capture routing. SourceAppIcons.cs keeps at most 48 small cached icons. The horizontal card list supports native keyboard selection and horizontal scrolling; explicit refresh requests are not dropped behind device refreshes. No new idle poll is used.

Decorative phone banners were removed; steps use plain titles and functional icons. Phone setup has five steps, starting with Developer options. The fixed footer is the only phone Back command; shortcuts follow setup steps and page history. Native overlay navigation reserves its own row at minimum width. ModeHelp.cs and BufferingHelp.cs build one responsive, two-column help dialog on demand with a fixed Done button; Audio mode is enclosed in its own panel.

Phone connection status uses a compact fixed bottom strip, separate from the scrolling guide and step actions. Functional icons use shared 24 DIP metrics; secondary affordances use 16 DIP. Audio output owns the shared device/app refresh button.

Status is independent of the power action: grey power Off, green play On, and a larger redrawn vector sleeping face for Standby. The vector has no idle animation or bitmap allocation. Source selection retains the horizontal list instead of rebinding it and resetting scrolling.
