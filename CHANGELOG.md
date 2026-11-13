# What’s new

## 0.5.2 (Android, Linux; Windows to follow)

- Choose your app icon: five designs and six background colours, in Settings. The default is the white and blue Seen on black.
- Android: a shorter activation guide, and the in-app icon no longer shows a dark frame.
- Linux: left-click the tray icon for the quick panel; right-click for Open and Exit. A refreshed Phone page.
- The "Sukut is in the tray" notice appears only the first time.

## 0.5.1 (Android, Linux; Windows to follow)

**All platforms**

- New app icon. The tray, the Android Quick Settings tile and the notification keep the س icon.

**Android**

- Activation one step at a time: choose Computer (recommended) or this phone.
- Clearer recovery when the audio worker stops, with an optional Debugging tile (new icon).
- Fixed filtering on Android 12 and 13 ([#1](https://github.com/altarafiya/Sukut/issues/1)).
- The Quick Settings tile now starts Sukut while the app is closed.
- Corrected Arabic wording and grammar.

**Linux**

- Stable 0.5.1 as `.deb` (Ubuntu 26.04+, PikaOS 4), Flatpak and snap.
- Right-click the tray icon for a quick panel with power, modes, mix, buffering, output and source app (on supported Wayland desktops; GNOME uses its own tray menu).
- Bundles Android 0.5.1 for the Phone page.

## Linux 0.4.0 beta 11 (Snap)

- Icons now show on KDE Plasma and other desktops whose icon theme isn't in the snap.

## Linux 0.4.0 beta 10 (Snap)

- Sukut is on the Snap Store, in the beta channel. It brings its own libraries, so it doesn't depend on your system's PipeWire version.
- If the snap isn't allowed to use audio yet, setup and the main window show one command to copy.

## Linux 0.4.0 beta 8 (Flatpak)

- First Flatpak build of Sukut for Linux, as one file that installs across distributions.
- The first time the Phone page needs it, Sukut downloads Google's adb and checks it.
- Start with system isn't available in the Flatpak yet.
- On computers where USB can't be opened from the sandbox, the Phone page points to Wireless debugging (Android 11+).

## Linux 0.4.0 beta 7 (Windows 0.4.2 and Android 0.4.2 unchanged)

**Linux (beta)**

- New headphones (Bluetooth or USB) are used as soon as they connect, with a notification.
  When they disconnect, Sukut goes back to the previous output.
- If your chosen output is missing when you press Start, Sukut plays on the default output
  and moves to it when it appears.
- If you change the output in your sound settings while Sukut is off, the next Start uses it.
- Picking the current output in your sound settings no longer lets apps skip Sukut.
- EasyEffects: Sukut stops an audio loop at once and shows how to fix it. Apps pinned to
  Easy Effects Sink now go through Sukut and get their pin back when it stops.
  **Using EasyEffects** under Audio output shows the setup steps.
- Built on PikaOS 4; a Debian 13 package will follow.

## Windows 0.4.2 · Android 0.4.2 · Linux 0.4.0 beta 6

**Windows**

- Bluetooth headphones and speakers work, including switching to them while filtering is on.
- When a new audio device connects, Sukut switches to it automatically and shows a notification.
- Turning Sukut off returns Windows sound to your current device, even if an earlier one was disconnected.
- Tray: a background-level slider for Optimized mode, and a buffering icon that matches your choice.
- New desktop layout: native navigation, boxed modes, app cards with icons, one help button,
  a shorter setup and a five-step phone setup.

**Android**

- Sukut runs its own audio worker; Shizuku is no longer needed. Activate it on the phone
  with Wireless debugging (Android 11+), or over USB from Sukut on Windows or Linux.
- Pair directly from setup, recover a stale worker after reinstalling, and fixed duplicated
  audio in screen recordings on Android 16.
- New package name `com.altarafiyah.sukut`; tablet layouts, notification and Quick Settings controls.

**Linux (beta)**

- First Linux release: GNOME-style window, tray, PipeWire routing, source-app filtering,
  Standby and the Phone page, in English and Arabic.
- The Phone page grants USB access with one system prompt, so no terminal is needed.
- Built on PikaOS 4; a Debian 13 package will follow.

## Windows 0.2.33 · Android 0.3.31

- Windows: fixes source-app routing errors and keeps Edge included when returning
  to All system audio.
- Windows Phone page: Install Sukut, Update Sukut or Full setup, with concise
  English/Arabic instructions and icons.
- Android: improved OFF cleanup and screen-recorder routing. Late callbacks do
  not reactivate processing after OFF.
- The Windows installer includes this signed Android update and offers VB-CABLE
  only when needed.

## Earlier improvements

- Rebuilt the Windows interface with WinUI 3 and Windows 11 styling.
- Added English/Arabic, themes, illustrated setup, tray controls and phone setup.
- Introduced Music Filter, Optimized with background adjustment, and Strict.
- Improved buffering, bounded recovery, volume handling, memory and package size.
- Added Android tablet layouts, Bluetooth-aware output settings and simpler
  performance choices.

Linux: coming soon, with GNOME design and Flatpak distribution planned.
