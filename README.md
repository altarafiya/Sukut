<p align="center"><strong>English</strong> · <a href="README.ar.md">Arabic</a></p>

<p align="center"><img src="Linux/data/icons/hicolor/256x256/apps/io.github.altarafiya.Sukut.png" width="80" height="80" alt="Sukut icon"></p>

<h1 align="center">Sukut</h1>
<p align="center"><strong>Enjoy the internet. Without the music.</strong></p>

![Sukut: music filtering for Windows, Android and Linux.](docs/artwork/hero-dark-en.svg)

Sukut mutes music from app audio while keeping speech. It works in real time, on your device.

## Download

| Windows | Android | Linux |
| :---: | :---: | :---: |
| [<img src="docs/artwork/stores/microsoft-en.svg" height="44" width="161" alt="Download from Microsoft Store">](https://apps.microsoft.com/detail/9NL67C3CS7QR) | [<img src="docs/artwork/stores/google-play-en.png" height="64" width="165" alt="Get it on Google Play">](https://play.google.com/store/apps/details?id=com.altarafiyah.sukut) | [<img src="docs/artwork/stores/snap-en.svg" height="44" width="143" alt="Get it from the Snap Store">](https://snapcraft.io/sukut) |
| Available now | **Available now** | **Stable** |

**Direct downloads**

| Platform | Download | Needs |
| --- | --- | --- |
| Windows | [ZIP 0.5.8](https://github.com/altarafiya/Sukut/releases/download/v0.5.7/Sukut-0.5.8-Windows-x64.zip) | Windows 10/11, x64, [VB-CABLE](https://vb-audio.com/Cable/) |
| Android | [APK 0.5.8](https://github.com/altarafiya/Sukut/releases/download/v0.5.7/Sukut-0.5.8-Android-arm64.apk) | Android 10+, ARM64 |
| Linux | [Flatpak 0.5.7](https://github.com/altarafiya/Sukut/releases/download/v0.5.7/Sukut-0.5.7-Linux-x86_64.flatpak) · [.deb 0.5.7](https://github.com/altarafiya/Sukut/releases/download/v0.5.7/Sukut-0.5.7-Linux-amd64.deb) | x64, PipeWire. See [package requirements](Linux/README.md#packages) |

On Ubuntu, use Snap. The stable Snap Store version is 0.5.7.
[All releases and checksums](https://github.com/altarafiya/Sukut/releases).

## Audio modes

| Mode | What you hear |
| --- | --- |
| Off | Original audio. |
| Music Filter | Speech and other sounds, with music muted. |
| Optimized | Music muted, with adjustable background sound. |
| Strict | Speech, with music and background muted. |

Results vary when sounds overlap. Strict may also remove sound effects.

Choose English or Arabic, light or dark themes, and your app icon. Windows and Linux also let you choose the source app and audio output.

## Setup

- **Windows:** Install from Microsoft Store, or extract the ZIP and open `Sukut.exe`. Install [VB-CABLE](https://vb-audio.com/Cable/), then follow the welcome guide. The ZIP is unsigned.
- **Android:** Install from [Google Play](https://play.google.com/store/apps/details?id=com.altarafiyah.sukut), or install the APK. Follow the guide to activate through Wireless debugging (Android 11+) or USB from Sukut on Windows or Linux (Android 10+).
- **Other Linux:** Download the Flatpak above, then run `flatpak install --user ./Sukut-0.5.7-Linux-x86_64.flatpak`. Open Sukut from your apps.
- **Ubuntu:** Run `sudo snap install sukut`, then open Sukut. Follow any audio permission steps shown in the app. For other packages, see the [Linux guide](Linux/README.md#packages).

The GitHub APK and Google Play app have different signatures. Uninstall one before switching to the other.

## Support

[What's new](CHANGELOG.md) · [Report a problem](https://github.com/altarafiya/Sukut/issues/new/choose) · [Privacy](https://altarafiya.github.io/Sukut/privacy.html)

Developed by Altarafiyah. [Build guide](AGENTS.md) · [Third-party notices](THIRD_PARTY_NOTICES.md).
