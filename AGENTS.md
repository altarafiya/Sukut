# Sukut development handoff — read first

This is the AI development prompt and repository map. Read the whole file before
editing. The README is the shared product page for Windows, Android and Linux.
Don't confuse a published binary with a source-build prerequisite.

## Current state and layout

- Windows **0.5.1** source (Store package 0.5.1.0; the published build is 0.4.2): C# WinUI 3
  window, native C++20 tray host (`Sukut.exe`) and CPU audio engine (`SukutEngine.dll`), VB-CABLE routing.
- Android **0.5.1 / code 62**, package `com.altarafiyah.sukut`, internal namespace
  `com.osama.cleanwave`; Kotlin/Java, framework Views only, ARM64. Sukut runs its own
  ADB-activated audio worker (Wireless debugging on Android 11+, or USB from a PC).
- Linux **0.5.1**: GTK 4/libadwaita window (`sukut`), `sukut-host` with the engine,
  PipeWire routing, tray (with a layer-shell flyout) and phone tools. Packaging: `Linux/debian`,
  `Linux/flatpak`, `Linux/snap`. See `Linux/README.md` and `Linux/PLAN.md`.
- Icons: `tools/export-branding.py` writes every platform's resources from
  `assets/branding/sukut-app-icon.png` (the hand, app icons) and `assets/branding/system-glyph/`
  (the original س, tray, Android tile and notification).
- The Advanced Optimized access code is not in the source: only a salted SHA-256. Tests use a
  stand-in code; pass the real one as `SUKUT_ADVANCED_CODE` (or `-e advancedCode` for the
  Android UI test) to test it.

| Path | Contents |
| --- | --- |
| `app/src/main` | Android UI, setup, worker control plane, preferences |
| `app/src/engine` | Production separator, streaming DSP, output mixer (build type `engine`) |
| `app/src/test*`, `app/src/androidTest` | Unit and instrumentation code |
| `app/vendor` | Runtime AAR provenance and licences. Not models |
| `Windows/src/sukut-ui` | WinUI pages, phone flow, assets |
| `Windows/src/sukut-host` | Native tray host, device notifications, tray popup |
| `Windows/src/{audio,live,realtime,dsp,model,desktop}` | Native pipeline and contracts |
| `Windows/tests`, `Windows/scripts`, `Windows/store`, `Windows/packaging` | Tests, build/package scripts, Store kit tools |
| `Linux/src/{audio,host,window}` | PipeWire bridge, host, GTK window |
| `Linux/scripts`, `Linux/tests` | Build, stage tests and stand-ins (fake phone, fake desktop) |
| `tools` | Android build helper, phone activation scripts, diagnostics |

## Deliberately absent inputs

The repository does not build a working audio release on its own. These are kept
by the maintainer and must never be committed, invented, downloaded from elsewhere,
or extracted from a release APK, ZIP, MSIX or `.deb`:

- the trained separator and the engine assets (`separator.tflite`,
  `window_576_q15.bin`, `window_960_q15.bin`) under `app/src/engine/assets` and
  `Windows/assets/engine`. They ship sealed;
- the sealing implementation: `Windows/src/model/AssetSeal.h`,
  `app/src/main/java/com/osama/cleanwave/AssetSeal.kt` (and its test), `tools/seal-assets.py`;
- `app/vendor/boringssl` (pinned upstream source; see `app/vendor/BORINGSSL-PROVENANCE.md`),
  prebuilt `app/src/main/jniLibs`, and the LiteRT runtime AARs (see `LITERT-PROVENANCE.json`);
- the Android setup-guide images (`app/src/main/assets/setup-guide`);
- test fixtures derived from model output, signing keys and passwords, and the signed APK
  that Windows and Linux bundle under `Assets/Phone`.

The build entry points stop early with a clear error when these are missing.
Release binaries are GitHub Release assets, not Git files. The maintainer authorized
publishing full binaries; that doesn't authorize adding model files to the repository.
Public wording is "Sukut's separation model". Don't describe where it came from.

## Build environment

Android: OpenJDK 17, Android SDK 36, build-tools 36.0.0 and the Gradle wrapper.
`tools/build-sukut-android.ps1` builds the `engine` type. Signing values come from
an external properties file (`SUKUT_ANDROID_SIGNING_PROPERTIES` / `sukutSigningProperties`).
Permanent certificate SHA-256 (GitHub APKs):
`C39F95966ED2DF9F79C26BE311269116121CE5BE1C0DE59FEF34E7FE51E83FC5`.
Google Play re-signs its copy, so the two can't update each other; never silently
uninstall a user's app or delete its data.

Windows: Visual Studio Build Tools 2022 (C++, Windows SDK), .NET 8, CMake 3.24+,
TensorFlow Lite 2.16.1, KissFFT 131.1.0. Read `Windows/deps/DEPENDENCIES.lock.md`.
Copy `Windows/local.build.example.json` to the ignored `Windows/local.build.json`.
`Windows/scripts/build-sukut.ps1` builds; `package-sukut.ps1` makes the portable ZIP;
`Windows/store/prepare-store.ps1 -Build` makes the Store kit. The portable is unsigned.

Linux: Debian 13 or newer with PipeWire 1.0+. `Linux/scripts/setup-debian.sh`,
`seed-deps.sh`, `build-stage1.sh`; `build-deb.sh` builds the package offline.
Tests that create virtual outputs switch the default output; warn the user first.

## Product contracts

- Names: Sukut / سُكوت. Slogan: Enjoy the internet. Without the music. /
  استمتع بالإنترنت، بلا موسيقى. Developed by الطرفية.
- English and Arabic, system/light/dark, connected Arabic text; never split Arabic shaping.
- Off restores original audio. Music Filter: voice 0 dB, music exact mute. Optimized:
  music exact mute, background -20…+10 dB. Strict: music and background exact mute.
  Gain sliders commit on release. Don't replace exact mute with a tiny gain.
- Windows: a newly connected physical output (USB, HD Audio, Bluetooth) is selected
  automatically with a notification; Stop restores Windows to the chosen device.
  Never force volume to 100%, clear every app route, or overwrite a newer user choice.
- Linux phone tools work entirely in the window: no terminal instructions in the UI.
  USB access is granted through one polkit prompt; Sukut never handles passwords.
- Model/window bytes, sample rate, DSP math and quality come before latency or size.
  Don't quantize, substitute models, or hide real dropouts.

## Workflow

1. Inspect the relevant code first. Ask only for genuinely missing input.
2. Never reinstall drivers, change global audio or power policies, or touch the
   user's phone while they are using it.
3. Run the relevant bounded checks. A missing model, device or hardware is a
   documented limitation, never a fabricated pass.
4. Bump the affected app's version and update the English/Arabic changelogs.
5. Publishing, uploading or changing repository visibility needs the maintainer's instruction.

Tests: `Windows/tests/Sukut.UnitTests` (77), native `sukut_endpoint_tests`,
`sukut_host_routing_tests`, `sukut_tray_tests`; Android focused unit tests under
`app/src/testEngine`; Linux `Linux/scripts/test-stage2.sh` … `test-stage8.sh`.
