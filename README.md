<p align="center">
  <img src="docs/artwork/logo.png" width="104" alt="Sukut logo">
</p>

<h1 align="center">Sukut · سُكوت</h1>
<p align="center"><strong>Enjoy the internet. Without the music.</strong><br>استمتع بالإنترنت، بلا موسيقى.</p>

<p align="center">
  <a href="https://github.com/altarafiya/Sukut/releases/latest"><img alt="Windows 0.4.2" src="https://img.shields.io/badge/Windows-0.4.2-8A6944?style=flat-square"></a>
  <a href="https://github.com/altarafiya/Sukut/releases/latest"><img alt="Android 0.4.2" src="https://img.shields.io/badge/Android-0.4.2-4E8878?style=flat-square"></a>
  <a href="https://github.com/altarafiya/Sukut/releases/latest"><img alt="Linux beta" src="https://img.shields.io/badge/Linux-Beta-6B7FA8?style=flat-square"></a>
</p>

![Sukut — quieter music, clearer speech](docs/artwork/hero.png)

Sukut filters music from audio playing in your apps. Choose a mode, keep watching,
and hear the speech you came for. Audio processing happens on your device.

سُكوت يرشّح الموسيقى من صوت تطبيقاتك. اختر وضعًا، وتابع ما تشاهده بكلام أوضح.
تجري معالجة الصوت على جهازك.

## Download

| Platform | Download | Requirements |
| --- | --- | --- |
| Windows | **[Sukut 0.4.2 for Windows (ZIP)](https://github.com/altarafiya/Sukut/releases/download/v0.4.2/Sukut-0.4.2-Windows-x64.zip)** | Windows 10/11, x64, [VB-CABLE](https://vb-audio.com/Cable/) |
| Android | **[Sukut 0.4.2 for Android (APK)](https://github.com/altarafiya/Sukut/releases/download/v0.4.2/Sukut-0.4.2-Android-arm64.apk)** | Android 10+, ARM64 |
| Linux (beta) | **[Sukut 0.4.0 beta 6 (.deb)](https://github.com/altarafiya/Sukut/releases/download/v0.4.2/Sukut-0.4.0-beta.6-Linux-amd64.deb)** | x64, PipeWire. Built on PikaOS 4; a Debian 13 build will follow |

[All releases and checksums →](https://github.com/altarafiya/Sukut/releases)

## Four audio modes

| Mode | What you hear |
| --- | --- |
| **Off** | Original audio; processing stops. |
| **Music Filter** | Music muted; speech and other sounds stay. |
| **Optimized** | Music muted, with a background slider to suit what you are watching. |
| **Strict** | Music and background muted, focusing on speech. |

Results depend on the recording: mixed sounds can overlap. Strict can also remove
sound effects. More buffering can help with short interruptions, but adds delay.

## Get started

**Windows**

1. Install [VB-CABLE](https://vb-audio.com/Cable/) if you don't have it, then restart.
2. Extract the whole ZIP folder and open `Sukut.exe`. Exit an older Sukut in the tray first.
3. Follow the welcome guide, choose your speakers or headphones, and pick a mode.

Bluetooth headphones work too. When a new audio device connects, Sukut switches to it
and shows a notification; turning Sukut off returns Windows sound to that device.
Sukut keeps working in the tray when you close its window. The ZIP is not publisher-signed.

**Android**

1. Install the APK and open Sukut.
2. Follow the setup guide to activate Sukut's audio worker: on Android 11+ through
   Wireless debugging on the phone itself, or over USB from Sukut on Windows or Linux.
3. Choose a mode in the app, its notification or Quick Settings.

The GitHub APK and the Google Play app are signed differently. To switch from one to
the other, uninstall the one you have first.

**Linux (beta)**

1. Open the `.deb` with your software installer, or run
   `sudo apt install ./Sukut-0.4.0-beta.6-Linux-amd64.deb`.
2. Open Sukut from your apps and follow the welcome guide.
3. The Phone page installs and activates the Android app over USB, all from the window.

The source includes Linux **0.4.0 beta 7**. It switches to newly connected headphones and
returns to the previous output when they disconnect. If you use EasyEffects, follow the
[setup steps](Linux/README.md#sukut-with-easyeffects), also available under Audio output in beta 7.

## Made for everyday listening

- English and Arabic, with light, dark and system themes.
- Source-app selection, Bluetooth output switching and phone setup on Windows and Linux.
- Android tablet layouts, notification controls and a Quick Settings tile.
- Adjustable background sound, amplification and buffering.
- Local processing, with no cloud audio upload.

## Updates and support

See [what's new](CHANGELOG.md). Found a problem? [Open an issue](https://github.com/altarafiya/Sukut/issues/new/choose)
with your app version, device/output, and the steps that caused it.
Privacy policy: <https://altarafiya.github.io/Sukut/privacy.html>.

Development layout and build guidance are in [AGENTS.md](AGENTS.md).
Model files, sealed engine assets and signing material are not in this repository.
Third-party components retain their [notices and licenses](THIRD_PARTY_NOTICES.md).

<p align="center"><strong>Developed by الطرفية</strong></p>
