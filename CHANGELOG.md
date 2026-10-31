# What’s new

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
