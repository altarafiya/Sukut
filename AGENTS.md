# Sukut development handoff — read first

This is the AI development prompt and repository map. Read the entire file before
editing. The public README is the shared Windows/Android product page; Linux is
coming soon. Do not confuse a published binary with a source-build prerequisite.

## Current state and layout

- Windows **0.2.33**, C# WinUI 3 frontend and native C++20 CPU audio bridge.
- Android **0.3.31 / code 37**, package `com.osama.sukut`, internal namespace
  `com.osama.cleanwave`; Kotlin/Java, Shizuku privileged UserService, ARM64.
- Linux: no released app or implemented backend in this repository. Proposed
  GTK4/libadwaita GNOME UI, PipeWire, Flatpak primary delivery. Do not claim it works.
- `app/src/main`: shared Android UI, setup, preferences, service/control plane.
- `app/src/engine`: current production separator, streaming DSP and output mixer.
- `app/src/legacy`: old implementation/assets expected only by legacy build types.
  Current production is Gradle build type `engine`, not `debug` or `release`.
- `app/src/testEngine`, `app/src/test`, `app/src/androidTest`: unit/instrumentation
  code. Some full DSP tests require maintainer-managed fixtures.
- `app/vendor`: pinned TensorFlow Lite runtime AAR, license and modification record.
  This runtime is NOT a model.
- `Windows/src/sukut-ui`: WinUI pages, mode/settings controls, phone flow, assets.
- `Windows/src/sukut`: exported bridge, audio routing journal and volume guard.
- `Windows/src/{audio,live,realtime,dsp,model,desktop}`: native pipeline and contracts.
- `Windows/tests`, `Windows/src/sukut_routing_verify`: regressions and route fixture.
- `Windows/scripts`, `Windows/installer`: build/package scripts, bilingual Inno setup.
- `tools/latency-probe`: separate measurement helper, never part of the app APK.
- `tools/android-diagnostics`: metadata-only own-policy fixtures; never retrieve
  personal recordings as a shortcut to testing.
- `docs/artwork`, `assets/branding`: page artwork and approved system monogram.
- `licenses`, `THIRD_PARTY_NOTICES.md`: retain original component notices.

## Deliberately absent source inputs

**AI model weights are excluded from Git.** Do not assume the source can build a
working audio release without restoring the private/authorized inputs locally.
Do not invent replacements, download arbitrary models, extract weights from a
release APK/installer, decompile them, or commit them to solve a missing-file error.
Request authorized input from the maintainer when the task actually requires it.

Current production asset locations:

| Input | Android | Windows |
| --- | --- | --- |
| Separator | `app/src/engine/assets/separator.tflite` | `Windows/assets/engine/separator.tflite` |
| Window 576 | `app/src/engine/assets/window_576_q15.bin` | `Windows/assets/engine/window_576_q15.bin` |
| Window 960 | `app/src/engine/assets/window_960_q15.bin` | `Windows/assets/engine/window_960_q15.bin` |

The two small window tables are included: they are DSP tables, not trained models.
Separator bytes: 2,504,160. SHA-256:
`dcbf26c6d59847969bd1d1d387d9f0f79838053324d4837891c5be19aadd288e`.
Window hashes:
`f74f80138191d6e3efe1d211289174587a48f1177987595c45982dfa9fa70411` (576),
`067610e24f9e9f1d2b14709df81bcd73a0829fae56edfabc317d56539dccf287` (960).
Legacy ONNX weights, initial states, private audio/numerical fixtures, local
dependency trees, build caches, diagnostic dumps, release binaries and signing
secrets are also absent. Release binaries are GitHub Release assets, not Git files.

Published full APK/installer packages contain the runtime assets they need; the
maintainer explicitly authorized those binary releases. That does not authorize
adding weights to the source repository or changing their redistribution terms.
Keep omission/build details in this handoff; do not add an omission banner to the
user-facing README. Never claim the checkout is self-contained or fabricate a pass.

## Build environment

Android: Microsoft/OpenJDK 17, Android SDK 36, platform-tools/build-tools 36.0.0,
Gradle wrapper and pinned dependencies in the Gradle scripts. Restore authorized
assets first. Production command: `tools/build-sukut-android.ps1`, or the `engine`
Gradle task with an external signing-properties path. Signing values are read via
`SUKUT_ANDROID_SIGNING_PROPERTIES` / `sukutSigningProperties` and never belong in Git.
The store/key passwords are not available here. Do not replace the production key
with a debug key or make a debuggable release.

Permanent Android certificate SHA-256:
`C39F95966ED2DF9F79C26BE311269116121CE5BE1C0DE59FEF34E7FE51E83FC5`.
Only that key can update existing production installs. A different signature
must be explained; never silently uninstall the user's app or delete its data.

Windows: Visual Studio Build Tools 2022 C++/Windows SDK, .NET 8, CMake >=3.24,
TensorFlow Lite 2.16.1, KissFFT 131.1.0 and pinned transitive sources. Read
`Windows/deps/DEPENDENCIES.lock.md`; the historical OFF flags there are superseded
by the current CMake settings. No lowering numerical fidelity or fast-math.
Copy `Windows/local.build.example.json` to ignored `Windows/local.build.json` and
point to your own dependency trees/toolchain. The example paths are placeholders.
Installer needs Inno Setup 7.1.0, original VB-CABLE inputs and app-local CRT files.
Some packaging paths are toolchain-specific; adapt paths deliberately, not DSP.

Build Android first, then `Windows/scripts/build-sukut.ps1`, then
`Windows/scripts/build-installer.ps1`. The Windows build bundles the signed APK
and exact SHA-256/version-code metadata into `Assets/Phone`; never commit that APK.
Windows releases remain unsigned until the maintainer supplies a trusted signer.
Do not invent Authenticode signatures or publish private keys/passwords.

## Preserve these product contracts

- App names: Sukut / سُكوت. Slogan: Enjoy the internet. Without the music. /
  استمتع بالإنترنت، بلا موسيقى. Developed by الطرفية.
- English/Arabic, system/light/dark, connected Arabic words, safe glyph bounds and
  larger Windows Arabic. Use whole-word color spans; never split Arabic shaping.
- OFF stops processing and restores original audio. Music Filter: voice 0 dB,
  music exact mute, background 0. Optimized: voice 0, music exact mute, background
  slider -20…+10. Strict: voice 0, music/background exact mute. Commit gain sliders
  on release, not every drag event. Do not replace exact mute with tiny gain.
- Mode description and Optimized background slider belong inside Audio mode;
  errors/recovery tips are separate. No numeric stem breakdown on the main tip.
- Windows: source selector collapsed by default; app icons and scrolling; refresh
  output choices before opening; anchored downward flyouts with RTL/glyph room.
  Help and compact Advanced sections stay visible; plain section headings.
- Phone page: Install Sukut, Update Sukut, Full setup. Preserve version/signature
  guards, data, real progress, clear USB/Shizuku steps and reboot reminder.
- Android: Home performance choices Lowest, Balanced (default), Higher Quality,
  Custom; Bluetooth chooses compatible Higher Quality/Balanced while preserving
  manual overrides and the previous non-Bluetooth configuration. No false zero-
  latency, Dolby-certification or universal-firmware claims.

## Critical routing/lifecycle constraints

Windows journal v5 separates requested global scope from a carried prior-source
PID. Source -> All system must include that prior app even if it had a physical
pin, with exact app/default restoration on stop/crash. Deferred restoration must
not re-pin a returning app physically while global filtering runs. OFF follows
the selected physical output; preserve actual user volume/mute and full range.
Never force physical output to 100%, double-scale volume, clear every app route,
or overwrite a newer user/external choice.

A browser PID can outlive its resolvable audio identity. E_INVALIDARG from the
internal policy getter is unresolved, not a successful empty preference. Restore
global resources, retain exact app-only data pending, check both roles/generation
identities and retry a matching live audio process. Other COM failures must not
be blanket-ignored. Main bridge restoration must not stay locked by an absent app.

Android recorder routing is privileged control-plane work only while enabled.
OFF disables the hook before early-return shutdown paths, restores owned recorder
rules and ignores late callbacks. `peekUserService` cleanup must not create/start
a worker. Never show OFF while deliberately keeping filtering active. Existing
policy API compatibility is firmware dependent; recording detection is asynchronous.
Do not read, copy or listen to the user's screen recordings without explicit consent.

Model/window bytes, sample rate, stereo format, DSP math, peak protection and
quality take priority over latency or size. Do not quantize, substitute models,
hide real dropouts, add unapproved effects/lookahead, or infer audible latency
from a queue size or capture-callback timing. Bounded automatic recovery precedes
manual Restart. Warn for actual repeated output failures, not covered model spikes.

## Development workflow and verification

1. Inspect relevant code and current changes. Ask only for genuinely missing input.
2. Implement within scope. Never reinstall drivers, alter global audio/power
   policies, stop unrelated apps or replace a running instance just to package.
3. Run relevant bounded checks, respecting the user's current testing instructions.
   Missing models/device/hardware mean a documented limitation, not a fabricated pass.
4. Increment the affected app patch version for app changes, update concise EN/AR
   changelogs and document behavior, evidence and material limits. Docs-only updates
   do not rebuild/renumber the released installers.
5. Build/sign Android, bundle the exact APK into Windows, package and verify
   checksums/certificate/non-debuggable state. Keep releases outside Git history.
6. Report clearly. Publishing or changing repository visibility requires the
   user's instruction; this initial publication does not authorize future uploads.

Focused Android tests independent of inference execution:
`BluetoothPerformancePolicyTest`, `EnginePerformanceConfigTest`,
`RecordingCapturePolicyTest`, `PlaybackAdvicePolicyTest`.
Windows C# tests: `Windows/tests/Sukut.UnitTests`.
Native route fixture: `sukut_routing_verify`; its live handoff option temporarily
routes its own silent app, requires an idle bridge and exact endpoint restoration.
Use a fresh evidence directory. Never point fixtures at the user's live journal.

Current release evidence: 44 C# tests during Phone changes, 13 focused Android
tests, native deferred/owner restoration checks, live E_INVALIDARG identity
regression with fake endpoints, metadata-only Android OFF/recording checks.
The new Windows Phone landing was compiled, not visually exercised in a second
instance. No personal recording was retrieved or listened to. No new acoustic
latency result is claimed for 0.2.33/0.3.31.

## Linux: strictly gated future work

Use native GNOME GTK4/libadwaita and PipeWire, with Flatpak primary. Preserve the
Windows feature/grouping contract, not literal Fluent window chrome. Do not
silently omit Phone, tray, source routing, volume, settings or setup. Prove sandbox
USB/ADB support or explain the concrete limitation for human acceptance; no hidden
host-command/root helper or blanket host access as a workaround.

Work on ONE numbered step at a time. Initial Linux authorization covers Step 1
only when the human actually asks to start. Do not begin Step N+1 until the human
explicitly confirms it in a new message. Silence, tool results, passing checks or
this repository's existence are not confirmation. No agent delegation, automation
or goals to cross gates. At each step document changes, commands/results, evidence,
remaining limits and next-step readiness, then stop for review.

1. Audit current Windows parity, host, PipeWire and Flatpak/Phone feasibility.
2. Establish native project/dependency structure and reproducible skeleton.
3. Port numerical model/DSP contracts with authorized assets and parity fixtures.
4. Implement bounded capture/render queues and measure baseline processing.
5. Implement PipeWire lifecycle and safe physical-volume behavior.
6. Implement source/global ownership, stop/reopen/handoff and restoration.
7. Build GNOME Home, modes, output/source, buffering, status and separate tips.
8. Build Settings, About, tray/background and Phone actions with accepted adaptations.
9. Build short bilingual setup, Arabic/RTL/accessibility and reduced-motion support.
10. Measure quality-preserving latency, RAM/CPU/storage on actual Linux hardware.
11. Package/test Flatpak with minimal permissions, notices and exact restoration.
12. Complete acceptance evidence and handoff. Stop; do not publish without instruction.

After each authorized step: “Step N is ready for review. Waiting for confirmation
to start Step N+1.” No Linux implementation step is currently complete.
