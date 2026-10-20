# Sukut Windows installer

Build `../scripts/build-installer.ps1` after building Android and publishing Windows.
The script requires Inno Setup 7.1.0 at `../deps/inno-7.1.0/ISCC.exe`, validates
the current Android APK, its bundled checksum, the pinned VB-CABLE archive and
the vendor setup signature, stages the app without an extra ZIP, and compiles
`../dist/installer-final/Sukut-0.2.33-Setup.exe`. Build records are generated locally; see the root AGENTS.md.

## Appearance and language

Native Inno modern / Windows 11 controls with system light/dark support. Both
English and Arabic are offered on launch. Arabic is RTL; Inno performs mirroring
once after `InitializeWizard`, so labels must start with their default left
alignment. Pre-setting right alignment would mirror it back to the left.

Arabic setup, buttons, progress, confirmations and removal copy use plain
Arabic in `Languages/Arabic-Sukut.isl` and `Sukut.iss`. Extra vowel marks were
removed in0.2.33; only the product name keeps its familiar damma (سُكوت).
Arabic body/button text is13pt, welcome heading26pt and driver heading18pt.
English sizes remain10/22/16pt. Native dialog scaling makes room for larger text. Rare platform error
messages use the credited upstream Inno translation; Windows and original
VB-Audio license/driver screens remain the authors' text. Arabic slogans also
appear in both theme artworks. See [artwork provenance](ARTWORK.md).

Private IBM Plex Sans Arabic Regular/SemiBold fonts provide connected Arabic
and safe marks. The colored developer wordmark is “الطرفية”, in a private
static Handjet face, with separate gold/purple ornaments. Arabic credit:
“تطوير:”; English: “Developed by”. The entire word stays one shaping run.
These fonts are local to setup and released on exit; no system font changes.
The font OFL licenses also ship in the app assets.

## Installation

- Per-user app in `%LOCALAPPDATA%\Programs\Sukut`; Start menu and optional
  Desktop shortcut. App/runtime settings stay writable without admin rights.
- Existing Sukut must be exited from the tray; setup never forcibly kills it.
- Standard VB-CABLE is detected by its actual `VBAudioVACMME` service, also
  covering disabled or renamed endpoints. Existing driver/settings are kept.
- When absent, extract the complete unchanged official pack45 and verify the
  x64 setup SHA-256. Only the signed original driver installer is elevated,
  with Windows consent; setup checks the service afterward. Reboot is offered,
  never forced. Sukut is not launched before that reboot.
- Sukut removal preserves user-created runtime settings and the shared driver.
- Installer is unsigned: no Sukut code-signing certificate is available.

`/SELFTEST=<absolute-json-path> /LANG=english /VERYSILENT` extracts/checks the
vendor payload and detects the existing service without installing or elevating
anything; early exit is intentional. `/PREVIEW=1` blocks installation from the
Ready page. These are development diagnostics, not recipient workflows.

## VB-Audio identity and conditions

VB-CABLE is made by **VB-Audio**, donationware. Contributions are welcome:
[website](https://vb-audio.com/Cable/) ·
[donation](https://shop.vb-audio.com/en/win-apps/11-vb-cable.html).

The author's [licensing page](https://vb-audio.com/Services/licensing.htm),
“VB-CABLE Distribution with other product”, allows embedding the **standard**
VB-CABLE with its identity, donation link and applicable donationware model.
The original older archive readme has additional integration/agreement wording;
it is preserved unchanged and shown when installing the driver. Keep this
distinction in any public distribution review; A+B/C+D are not included. This
private build does not establish Audio-model redistribution rights. No store
or public upload is performed.

Archive/vendor hashes and source URLs: `../deps/installer/sources.json`.
Fresh-machine installation of an absent driver, UAC/reboot, and uninstall have
not been exercised on this machine; the existing shared driver is not removed
for testing. Do not claim those branches passed based on a detection self-test.
