# Sukut for Linux

The plan is in [PLAN.md](PLAN.md): Debian 13 first, with stages and owner gates.

## Where it stands (29 October 2026)

| Stage | State |
|---|---|
| 0: Debian machine and offline dependencies | **Done.** Debian 13 in WSL 2 on this PC. The tools are installed, and the dependencies are seeded with every pinned hash verified |
| 1: engine without audio, compared with Windows | **Done, accepted by the owner on 29 October.** All four verifiers pass on Linux. DSP audio is byte-identical to Windows. The model's recurrent step differs from Windows, but Linux is the one that matches TFLite's reference interpreter. See [evidence/stage1-2026-10-29/SUMMARY.md](evidence/stage1-2026-10-29/SUMMARY.md) |
| 2: PipeWire pass-through without the model | **Done in WSL, accepted by the owner on 29 October.** WSL turned out to run PipeWire 1.4.2 and WirePlumber 0.5.8, so it was tested there with virtual outputs: 21 of 21 checks pass (sample-exact audio, pre-volume capture, mute, no gaps, default restored after Off, a crash and a newer choice). Listening on real speakers is still to do. See [evidence/stage2-2026-10-29/SUMMARY.md](evidence/stage2-2026-10-29/SUMMARY.md) |
| 3: the engine live on PipeWire | **Done in WSL, accepted by the owner on 29 October.** The same engine as Windows runs between PipeWire capture and playback. 20 of 20 checks pass in all three modes: the live output equals an offline replay bit for bit, the output device receives it exactly, there are no gaps, and Standby works. Music is reduced by 41–60 dB and speech is unchanged, measured on synthetic music. Listening on real music still needs hardware. See [evidence/stage3-2026-10-29/SUMMARY.md](evidence/stage3-2026-10-29/SUMMARY.md) |
| 4: routing ownership | **Done in WSL, accepted by the owner on 29 October.** 28 of 28 checks pass, verified in the actual PipeWire graph. Sukut can filter one app (the app quitting ends the session, as on Windows), follow an output you pick while it runs, move on when its output is removed, and undo everything after a crash with `--restore`. See [evidence/stage4-2026-10-29/SUMMARY.md](evidence/stage4-2026-10-29/SUMMARY.md) |
| 5: the host (tray, menu, socket, notification) | **Done in WSL, accepted by the owner on 29 October.** `sukut-host` owns the engine, the tray and the window's socket. 55 of 55 checks pass with stand-ins for the panel and the notification server: the full menu in English and Arabic, On, modes, buffering and source app from the menu, Standby, the closed-app rule, closing to the tray with the notification, the no-tray fallback, single instance, autostart, and Exit, SIGTERM and crash restore. Real panels and RealtimeKit need a real desktop session. See [evidence/stage5-2026-10-29/SUMMARY.md](evidence/stage5-2026-10-29/SUMMARY.md) |
| 6: the window | **Done in WSL, accepted by the owner on 29 October.** The GNOME-native window (owner's decision): sidebar, Audio with the Playback card, Settings with About, the help dialog and first-run setup, in English and Arabic, light, dark and high contrast. 30 screenshots and 4 keyboard checks pass on a headless display, and 10 of 10 checks of the real window with the real host. Orca and real-desktop checks need hardware. See [evidence/stage6-2026-10-29/SUMMARY.md](evidence/stage6-2026-10-29/SUMMARY.md) |
| 7: phone tools | **Done in WSL, accepted by the owner on 29 October.** The Phone page (Full setup in 5 steps, Activate, Install or update, USB help, the connection card) and the same flows as Windows, using Debian's `adb` and the same signed APK and activation script. 24 of 24 checks pass against a stand-in phone, plus 13 screenshots and the keyboard check. The live check on your phone moves to stage 9 on real Debian. See [evidence/stage7-2026-10-29/SUMMARY.md](evidence/stage7-2026-10-29/SUMMARY.md) |
| 8: Debian package | **Done in WSL, accepted by the owner on 30 October.** `Linux/dist/sukut_0.4.0~beta.1-1_amd64.deb` (10.8 MB), built offline and hardened with no debug data. lintian has no errors or warnings, and the desktop file and AppStream listing are valid. Install, the installed window and engine, upgrade, remove and purge all pass. See [evidence/stage8-2026-10-29/SUMMARY.md](evidence/stage8-2026-10-29/SUMMARY.md) |
| 9–10 | Not started. **To continue on the Debian machine, start a new chat with [START_NEW_CHAT_LINUX.md](START_NEW_CHAT_LINUX.md)** |

### Open problem: the phone tools on a real phone (31 October)

The owner reports that Android activation does not work on real Linux. Their requirement: **every Android feature must work from the window; the user must never need the terminal.**

Known gaps:

- `adb devices` can report a phone as `no permissions` (USB access not yet granted). `PhoneTools::ParseDevices` ignores that state, so the page says to connect the phone.
- The USB help page shows a terminal command (`usermod -aG plugdev`).
- `adb` is only Recommended, not a hard dependency.

The fix plan is in [START_NEW_CHAT_LINUX.md](START_NEW_CHAT_LINUX.md) section 2.

### Changes to the shared engine for Linux

Each change is guarded, so Windows compiles exactly what it did before. The Windows build and its model, DSP and efficiency verifiers were re-run after the change and pass.

- `Windows/src/realtime/ProcessingThreadTask.cpp`: the Windows MMCSS code is now under `#if defined(_WIN32)`. Linux raises only the processing thread's nice value (falling back to normal priority if refused) and reads thread CPU time with `CLOCK_THREAD_CPUTIME_ID`.
- Stage 5 added a Linux-only `SetPriorityRequest` hook to the same file and its header. The thread calls it when the kernel refuses the nice value; the host points it at RealtimeKit.
- `dsp_verify`, `efficiency_verify` and `realtime_verify`: `wmain` on Windows, `main` on Linux, with the same body.
- `cleanwave_offline` (the WAV-file tool) parses UTF-16 arguments throughout. It is not built on Linux yet, because stage 1 doesn't need it.

### Windows reference

`reference/windows-2026-10-28/` holds the outputs of the Windows model, DSP and efficiency verifiers on the sealed files: 30 files, 0.3 MB, built on 28 October 2026.

A second Windows run is byte-identical to it, and the comparison script catches a single flipped bit.

## Running stages 0 and 1 on this PC (WSL)

WSL is a quick way to build and check the engine (stages 0–1). Audio, tray and desktop stages need a real Debian 13 install or a VM with GNOME.

1. **Install Debian in WSL.** Run this in an administrator PowerShell, then restart Windows when asked:

   ```bash
   wsl --install -d Debian
   ```

   Open "Debian" from the Start menu and create a Linux user.
2. **Check the version:**

   ```bash
   cat /etc/os-release
   ```

   It must say Debian 13 (trixie). If it says 12 (bookworm), upgrade first: change `bookworm` to `trixie` in `/etc/apt/sources.list` (or `/etc/apt/sources.list.d/debian.sources`), then run `sudo apt update && sudo apt full-upgrade`.
3. **Run the stages from the project folder:**

   ```bash
   cd /mnt/c/Users/osama/Documents/Sukut
   sh Linux/scripts/setup-debian.sh
   sh Linux/scripts/seed-deps.sh
   sh Linux/scripts/build-stage1.sh
   ```

| Script | What it does |
|---|---|
| `setup-debian.sh` | Installs the compiler, CMake, Ninja and tools (asks for your sudo password) |
| `seed-deps.sh` | Copies the pinned engine dependencies from the Windows build on `D:` into `~/sukut-deps`, checking each archive's SHA-256. Its only download is NEON_2_SSE (a small header library TFLite needs on x86 Linux), checked against TFLite's own pinned hash |
| `build-stage1.sh` | Builds in `~/sukut-build/stage1`, checks that the sealed files match on Windows and Android, runs the four verifiers, and compares the outputs with the Windows reference |
| `test-stage2.sh` | Builds `sukut_pw_passthrough` and runs the 21 stage 2 checks against the session's PipeWire, using two temporary virtual outputs. It changes the default output during the test and restores it |
| `test-stage3.sh` | Builds `sukut_pw_filter` and `sukut_offline_replay`, then runs the 20 stage 3 checks in all three modes (about 3 minutes). It also changes and restores the default output |
| `test-stage4.sh` | Runs the 28 routing checks with the engine on, using two virtual outputs and two stand-in apps. It removes and recreates its own virtual outputs, and changes and restores the default |
| `test-stage5.sh` | Builds `sukut-host` and runs the 55 host checks on a private session bus (`dbus-run-session`), with stand-ins for the panel's tray watcher and the notification server and a fake window command (about 2 minutes). Settings and state go to `~/sukut-stage5`, not your own `~/.config`. It also changes and restores the default output |
| `test-stage6.sh` | Builds the window (`sukut`) and host, takes the screenshots and keyboard checks with the window's `--capture` mode, then runs the 10 window-and-host checks. Everything runs on GTK's headless Broadway display, so no window appears, and no mode is started. About 4 minutes |
| `build-deb.sh` | Builds `Linux/dist/sukut_<version>_amd64.deb` offline (about 10 minutes; TensorFlow Lite is compiled with Debian's hardening flags) |
| `test-stage8.sh` | Checks the built package: contents, sealed files, no debug data, hardening, lintian, desktop and AppStream |
| `test-stage8-install.sh` | Installs, uses, upgrades, removes and purges the package, in three phases: `install` and `finish` as root, `user` in your normal session. Installs only `sukut` (never the Recommends) |
| `make-app-icons.ps1` | Windows only, run once: the rounded app icons at 16–512 px from the phone icon |
| `test-stage7.sh` | Runs the 24 phone-tool checks against a stand-in phone (`tests/fake_adb.py`; no real phone or USB), then the Phone page's screenshots and keyboard check on the headless display. About 3 minutes |

The test scripts keep a hand-written `SUMMARY.md` in their evidence folder and replace everything else the run generates.

**Trying it by ear on a Linux PC with sound:**

```bash
~/sukut-build/stage1/bin/sukut_pw_filter --engine Windows/assets/engine --mode 1
```

Run it from the project folder. It filters everything playing until Ctrl+C, then gives the default output back.

- Add `--app <process id>` to filter only one app; the session ends when that app closes.
- If it is ever killed, `sukut_pw_filter --restore` puts the default back and releases the app.

### WSL notes from the first run

- **Clock.** The Windows clock must be correct, or apt rejects Debian's signatures as "not live" yet. On 29 October 2026 this PC was 8 hours behind and had never synced. Turning on automatic time fixed it, but a few hours later it was 8 hours behind again. A clock that jumps backwards also makes Ninja treat old object files as newer than edited sources: delete the affected `CMakeFiles/<target>.dir` folders to force a rebuild.
- **`/tmp`.** It is cleared whenever the WSL instance idles out, so keep logs in your home folder.
- **sudo without a password.** `wsl -d Debian -u root -- apt-get …` installs packages as root, so no Linux password is needed.
- **Running from Windows PowerShell 5.1.** Use `wsl -d Debian --exec sh <script>`. Inline `sh -c "…"` commands lose their double quotes, and `$(…)` is expanded by WSL's own shell first.

The build script writes its results to `Linux/evidence/stage1-<date>/`. The comparison ends with one of:

- `RESULT=IDENTICAL to Windows`: stage 1 passes.
- `RESULT=DIFFERENT`: the engine runs, but some outputs differ numerically from Windows. The report shows the largest error per file, and you decide (PLAN.md section 4.4).
- `RESULT=FAIL`: a verifier failed or an output is missing.
