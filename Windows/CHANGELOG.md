## Windows 0.4.2 — Bluetooth routing and tray controls, 2026-11-01

- Bluetooth outputs (BTHENUM, BTHHFENUM, BTHLEDEVICE) pass route validation. Endpoint errors name the device that failed.
- A newly connected physical output is selected automatically, also while filtering, and a notification confirms the switch. Connections that arrive during Sukut's own routing command are queued, not dropped.
- Stop restores Windows to the selected output, falling back to a connected physical device when the saved one is gone.
- Tray: buffering glyph and colour follow the preset. A native Optimized background slider (-20 to +10 dB) commits once on release. It is created only while needed.
- Portable `Windows/dist/Sukut-0.4.2-win-x64.zip` and Store kit `Store/Microsoft-Store-0.4.2`. Details: `docs/release-0.4.2/WINDOWS.md`. No upload or submission.

## Windows 0.4.1 — Microsoft Store update, 2026-10-30

- Prepare the x64 Microsoft Store upload as package 0.4.1.0, preserving the existing Sukut identity.
- Include the latest GUI polish: shorter setup, no volume-control notice, accent-preserving Start hover, unclipped buffering borders and a collapsed default sidebar.
- Native tray, audio engine, model and Android 0.4/code54 companion remain unchanged.
- Build and static package checks pass, including uploaded resources and managed versions. English/Arabic update notes and checksums are in `Store/Microsoft-Store-0.4.1`. No upload, installation or Store certification was performed.

## Windows 0.4 — GUI polish, 2026-10-30

- Remove the volume-keys setup page and Check volume control notice; adjust setup progress and Back/Next navigation.
- Keep Start/Stop's accent on hover, inset buffering focus borders to avoid clipped corners, and start with collapsed navigation.
- Cursor-free English/Arabic 4K Store graphics and five cleaned screenshots, edited with built-in imagegen. Native tray, engine and model unchanged.
- Revised portable and verification: `docs/release-0.4/WINDOWS-POLISH-2026-10-30.md`. No new Store submission.

## Windows 0.4 — 2026-10-29

- Separate state icons for On/Off/Standby, with a redrawn sleeping face; Start/Stop remains an action.
- Source-card selection keeps horizontal scroll position.
- Native Windows navigation, compact Start/Stop, mode rows and a responsive 1040 × 720 desktop window.
- One top-right Audio help button explains modes and buffering in responsive columns.
- More space separates the Audio buffering heading from its three choices.
- A matching divider separates Audio output from Source app.
- Phone connection status moves to a compact bottom strip; setup functional icons share one size.
- Shared Audio output refresh updates both output devices and source apps.
- Horizontal source-app cards with icons. Open desktop apps appear before playing sound; audio-session apps are retained. Explicit refresh no longer loses app requests during a device refresh.
- Five clean phone-setup steps, beginning with Developer options; plain titles replace decorative icon banners. Larger actions, fixed footer spacing and a single Back button that returns to the previous step.
- Page Back follows navigation history; Escape and Alt+Left support setup-step navigation. The compact menu no longer covers Back.
- Tray output icons match the selected headphones/headset, monitor or speakers. The source list also includes visible open apps before they play sound; it refreshes on demand without new idle scanning. Tray layout, menus and audio routing remain unchanged.
- Version and portable metadata now say 0.4, without a beta suffix. Companion Android remains 0.4/code54/com.altarafiyah.sukut.
- Microsoft Store update candidate preserves the existing identity, uses 0.4.0.0 and includes the engine's four app-local VC143 dependencies. It has not been submitted or certified.
- Engine, DSP, model assets, routing, gains and buffer durations are preserved. App icons use a bounded cache; discovery runs on demand, without new idle polling.
- Verification and scope evidence: docs/release-0.4/WINDOWS.md and work/windows-release-0.4/VERIFICATION.json.

## Windows 0.4.0-beta.14 — desktop GUI candidate, 2026-10-29

- Native Windows navigation for Audio, Phone and Settings, with a compact rail/overlay at small widths.
- Compact Start/Stop beside status; full-width mode rows with concise descriptions and smaller buffering controls.
- Playback devices and source apps remain visible beside the modes; narrow windows stack the playback panel below.
- Smaller Fluent card/toolbar corners, an actual-content viewport and a 1040 × 720 default window. English/Arabic and light/dark/high-contrast theme resources retained.
- Native tray, menus, engine, routing, gains and buffer durations preserved. No new idle animation or polling.
- Bundles Android 0.4/code54/com.altarafiyah.sukut; old app IDs remain separate installs. Packaging accepts the stable Android version and uses current-version default directories.
- Source backup and exact scope hashes recorded. See docs/beta-0.4/WINDOWS-beta14.md and work/windows-desktop-0.4/VERIFICATION.json for completed checks and remaining release limits.

## Windows and Android 0.4 beta — in development

## 0.4.0-beta.13 — portable test copy, 2026-10-28

- Desktop window without the side bar, 960 × 680 (minimum 420 × 480).
  - The header spans the top, with round Phone and Settings buttons.
  - The leading side has the power card, the three mode tiles and Optimized's slider.
  - The trailing side has an always-visible Playback box: Audio buffering tiles, the Audio output list and the Source app list (with a refresh button). The lists refresh when the window is activated.
  - Below 720 px the box moves under the modes.
  - Settings and Phone keep a 760 px reading width, with a round Back button (Esc / Alt+Left).
- The mode tips under the tiles are replaced by a ? button. It opens a dialog like the phone's: Music Filter, Optimized, Strict, and Start and stop, each with its colour, icon and points.
- Phone palette with Fluent theme resources (dark, light, high contrast): Off = accent ring, On = solid accent, Attention = amber.
- Standby (5 s with nothing playing) shows a sleeping face on the power button, with "Nothing is playing. Sukut rests to save power and wakes as soon as sound starts." The tray shows a small sleeping face beside "Standby · nothing playing".
- About moves to the end of Settings, like the phone. The rainbow credit animates only while that card is on screen.
- The setting "Keep running in system tray" is now "Close to tray", with a one-line explanation.
- When the window closes to the tray, the tray shows a notification: Sukut is in the tray and uses under 20 MB. If filtering is on, it adds "plus the audio filter".
- While the window is open, clicking the tray icon brings the window forward. The flyout opens only when no window is open.
- The tray flyout's power circle is replaced by a Fluent On/Off switch, mirrored in Arabic.
- Memory:
  - With filtering off, the host returns unused pages after the window or flyout closes: 16.4 → 1.4 MB working set, 0.7 MB private.
  - Direct2D and DirectWrite now load only when the flyout first opens.
  - A minimized window trims itself: 34 → 8.5 MB private working set.
  - The host never trims while filtering, so audio is unaffected.
- Icon: the phone icon with rounded corners for the app, window, taskbar, About and notification. The window no longer replaces its taskbar icon with the tinted س. The tray icon stays س.
- Fixes:
  - Moving Optimized's slider while Off no longer hides it.
  - The Music Filter icon no longer mirrors in Arabic.
  - The Arabic "What's new" header keeps the version in Latin digits and in order.
- Backups: `Backups/2026-10-28-windows-before-phone-design/` (beta.12 UI) and `Backups/2026-10-28-windows-beta13-compact/` (the compact single-column beta.13). Details: `docs/beta-0.4/WINDOWS-beta13.md`.

## Shared engine changes for Linux — 2026-10-28

- `src/realtime/ProcessingThreadTask.cpp`: the MMCSS code is now under `#if defined(_WIN32)`, with a POSIX branch beside it. Windows behaviour is unchanged.
- `dsp_verify`, `efficiency_verify` and `realtime_verify` keep `wmain` on Windows and gain `main` for Linux.
- Rebuilt on Windows: the model, DSP and efficiency verifiers still pass on the sealed files.

## After 0.4.0-beta.12 — not yet built (2026-10-28)

- Store the model and DSP window files encrypted; the engine reads each into one buffer and decrypts it once at start. Audio output is byte-identical; about 2.5 MB more private memory while the engine runs.
- Rename the model's internal tensors neutrally and clear its converter metadata. Weights, size and results are unchanged.
- Engine messages say "Sukut model" / "Sukut window".
- The model and DSP verifiers and the efficiency check pass on the sealed files.

## 0.4.0-beta.12 — 2026-10-28

- Install/update Android beta.12 and activate Sukut's own audio worker over USB.
- Remove Shizuku download, repair and startup dependency.
- Verify worker protocol, installed app version and live connection before reporting activation.
- Add deactivation, idempotent activation and concise English/Arabic phone setup.
- Preserve the existing Windows engine, native tray and presentation.
- Windows activation/deactivation/reactivation and own-UID silence engine tests passed on Xiaomi 14T Pro / Android 16. Broader live-content, Bluetooth and reboot checks remain.


### Windows 0.4.0-beta.11 — 2026-10-27

- Replace the bright native scrollbar in tray lists with a slim rounded thumb painted by the existing tray renderer. It follows dark/light appearance, mirrors for Arabic, widens on hover and appears only when needed.
- Keep native listbox keyboard/accessibility support, add wheel-delta accumulation, draggable thumb and page clicks. Reserve text space for the thumb; do not create another window, timer, bitmap cache or GUI process.
- Repaint only changed hover rows. Move updated tray models instead of copying their strings twice; free the buffering-choice storage on close.
- Reuse the Home power-button gradient until its colour changes, avoid resetting an unchanged tooltip, and avoid repeated taskbar-theme registry reads on every status poll. Theme changes still refresh icons immediately.
- Add native regressions for scrolling at 100/150/200% DPI, RTL/LTR, dark/light, long-list reachability and repeated open/render/close resource usage.
- Retain beta.10's Home redesign, Shizuku 13.5.4 repair and instant page/tray opening. Android remains beta.8/code45.

### Windows 0.4.0-beta.10 — 2026-10-27

- Redesign Home around a large power button and three mode cards, inspired by Android. Remember the selected mode while off.
- Use violet for Music Filter, amber for Optimized and rose for Strict across Home, the tray buttons and the taskbar/tray icons. Own and release small native icons; no image cache.
- Remove tray opening animation and page translation to eliminate the empty rectangle and navigation shake. Retain immediate dropdowns and hover feedback.
- Increase the initial window to 860 × 820 DIPs, constrained to the monitor work area. Keep setup pictures collapsed until requested.
- Replace verbose phone status with a connected-phone card: model, USB state, Android version and a clear Shizuku repair warning.
- Fix Shizuku now restores the owner-confirmed 13.5.4/code1049 on every Android version, including Android 16. Replace the beta.9 Android-version rule; other builds cannot be reported as ready. Verify downloads before replacement; preserve Sukut data.
- Simplify Settings shortcuts to VB-CABLE and Windows Sound. Add a short coloured headphone-effects note there and in setup.
- Show the in-app changelog as bullet points. Bundle the unchanged signed Android beta.8/code45.

### Windows 0.4.0-beta.9 — 2026-10-27

- Replace the tray buffering slider with the same inline dropdown used for output/source: Lowest, Balanced, Maximum. Keep icons, hover feedback, keyboard selection and close-time resource release.
- Reduce the taskbar gap from 24 to 8 DIPs. Replace timer-based movement with a short native Windows slide, respecting reduced motion.
- Right-click remains a native HMENU, now using Windows dark/light menu theming; no extra UI process or image cache.
- Phone landing: Full setup first; a Tools box with matching Fix Shizuku and Install or update Sukut cards. Retain USB help and the illustrated full guide.
- Fix Shizuku checks Android and the installed version, verifies the official replacement before removing a different version, stops the old server, installs/verifies, opens and starts it. Recovery policy: 13.5.4/code1049 for Android 15 and earlier; 13.6.0/code1086 for Android 16+. Replacing Shizuku resets its grants; Sukut data stays intact.
- Smart Sukut setup installs a missing app, updates an older one in place, keeps current/newer versions and opens the app. Signing-key failures never trigger an uninstall.
- Brief composition-only page transitions keep opacity at one. No initial Home entrance fade; reduced motion is respected.
- Add per-app Google Play auto-update instructions and explain the unresolved upstream HyperOS permission report for Shizuku 13.6.0.
- Android remains beta.8/code45, bundled unchanged. Local beta only; no Store upload or installer replacement.

### Windows 0.4.0-beta.8 — 2026-10-27

- Follow Windows playback-device changes (including newly connected headphones) from the native host, even while the window is closed; retain the active filter mode and synchronize the selected output in the window and tray.
- Left-click opens a native flyout with a short slide-up animation, buffering slider and device/source lists that expand inside its body. Right-click opens Open Sukut / Exit. Removed Exit from the flyout.
- Accent-coloured round-thumb slider, lightweight font icons, 24-DIP taskbar gap and hover highlights on device/source controls and list rows. Cache text formats while open, move the drawn surface during the short slide, and batch expansion layout into one redraw. Apply buffering only after the slider gesture completes; release flyout resources on close. Respect Windows' reduced-motion setting.
- Move Audio output and Audio buffering above Audio mode; reduce the initial window height to 560 DIPs. Remove Home entrance animation and mode/buffering check badges; give the explanation text full font bounds and safe padding.
- Open output/source lists immediately using cached results; refresh devices and app icons in the background.
- Versioned host/window settings synchronization prevents delayed window saves from undoing a tray or device change.
- Android remains 0.4.0-beta.8 (code 45), bundled unchanged. Local beta only.

### Windows 0.4.0-beta.7 — 2026-10-26

- New native tray host, Sukut.exe: runs the audio engine and stays in the tray using about 16 MB (3 MB private) while idle. It keeps filtering and automatically restarts audio after faults (three attempts) while no window is open.
- New Windows 11-style tray flyout (left- or right-click): switch Off, Music Filter, Optimized or Strict, see the status, open Sukut or exit. English/Arabic, light/dark, Windows accent colour, keyboard and screen-reader friendly. Double-click opens the window.
- The window (app\SukutWindow.exe) is a separate process: closing it frees all of its memory while filtering continues. With "Keep running in system tray" off, closing exits Sukut and restores audio routes.
- Standby: after 5 seconds of digital silence the AI rests and resumes on the first sound; shown as "Standby".
- Less cymbal and hi-hat sparkle above 16 kHz when music is muted.
- XNNPACK tested and not used: slower and not numerically identical on the test PC.
- Smoother mode cards with a check on the selected mode; faster window start (ReadyToRun).
- Includes the signed Android 0.4.0-beta.8 (indigo look, س icon, one-screen Home, plain-language Playback choices, Standby).

### Earlier 0.4 betas

- Windows: direct USB help, Install Shizuku and Start Shizuku steps; no full guide required.
- Windows: fix the volume-tip action layout and wrapping phone buttons; load guide pictures on demand.
- Windows: detect the installed Shizuku starter and wait for it to start, with a clear retry message.
- Includes the matching Android beta with a refreshed layout, clearer performance options and conservative silence standby.
- Android: grouped mode choices, clearer performance controls and simpler reconnect actions.
- Android: losing Shizuku no longer repeats completed setup.
- Android: long-press the Quick Settings tile to open Sukut.
- Android: fewer health IPC calls and background refreshes, with unchanged audio processing.
- Windows: restore original routes on Exit, including pending source-app routes.
- Windows: refresh transient offline phone connections and prefer the connected device.
- Both: language choices always read English and العربية.
- Windows includes the matching Android 0.4 beta APK.

All changes remain part of 0.4 beta until the owner explicitly requests release.
Device playback, battery measurements and live UI checks remain pending until validated.

## Windows 0.2.33 / Android 0.3.31 — 2026-10-20

- Windows restores global audio even when a browser audio identity is temporarily unavailable; exact app preferences remain pending safely.
- Keeps Edge included when returning from Source app to All system audio.
- Phone offers Install Sukut, Update Sukut and Full setup, with short descriptions and icons.
- Android OFF disables recorder routing and cleans existing workers; late callbacks cannot reactivate it.
- Signed Android 0.3.31/code37 is bundled in the Windows installer. Models, DSP, audio formats and buffers are unchanged.
- Linux GNOME/Flatpak instructions and current reference snapshot refreshed; no Linux implementation step started.

## Windows 0.2.32 — 2026-10-20

- Phone tab starts with Install Sukut, Update Sukut and Full setup, with short English/Arabic descriptions and icons.
- Quick install/update skip the full guide; updates keep data and never downgrade or install a missing app silently.
- Full setup retains the USB/Shizuku guide and fixed navigation, with Back/Done returning to choices.
- Includes the Edge/source-to-All-system fix and the same signed Android 0.3.30 APK.
- Linux GNOME/Flatpak instructions and source references refreshed; no Linux implementation stage advanced.

## Windows 0.2.31 — 2026-10-20

- Keep the previous source app in the filtered mix when switching to All system audio (including Edge/YouTube).
- Restore its saved per-app route and system defaults on Stop/crash.
- Defer closed-app physical-route cleanup while global filtering is active.
- OFF lets a selected source follow the chosen Windows default instead of leaving a physical pin.
- Same Android 0.3.30 companion, models, DSP, gains and buffer sizes.

## Windows 0.2.30 / Android 0.3.30 — 2026-10-19

- Restore saved audio routes when a previously selected source app reopens.
- Windows Phone guide offers a settings-preserving update for an older Sukut.
- Android screen-recording capture includes only Sukut output while filtering is active on supported firmware.
- Restore normal recorder rules on OFF; keep isolation across an engine restart.
- Latest production-signed Android companion bundled in Windows. Same model/DSP/buffering.

## Windows 0.2.29 / Android 0.3.28 — 2026-10-19

- Rounded Arabic «س» icon for Android Quick Settings and notifications.
- Matching Windows tray icon with light/dark and active/inactive variants.
- Same Android release-signing key; audio quality and buffering unchanged.

## Windows 0.2.28 / Android 0.3.27 — 2026-10-19

- Android debugging disabled; signed with a permanent production key.
- Production Android companion included in the Windows package.
- Windows installer remains unsigned pending a trusted publisher certificate.
- Audio models, processing, presets and buffering unchanged.

## Windows 0.2.27 / Android 0.3.26 — 2026-10-19

- Consistent Sukut identity across the desktop app and Android companion.
- A short closing prayer in the installer and both first-run guides.
- Latest Android companion included in the Windows installer and portable package.
- Same audio models, DSP, presets and buffering.

## Windows0.2.26 — 2026-10-18

- Plain Arabic installer copy and artwork; keep only the product-name damma.
- Larger Arabic text: body/buttons13pt, welcome26pt, driver heading18pt.
- Retain RTL layout, colored developer wordmark, English and both themes.
- Same Android0.3.25 companion; no model/DSP/buffer changes.

## 0.2.25 — 2026-10-18

- New English/Arabic installer with system light/dark support, simpler Arabic art and decorative developer credit.
- Original VB-CABLE installed only if absent; latest Android0.3.25 included.
- Versioned status replies reuse unchanged device lists; 72.5% fewer managed allocations in the 16-device polling fixture.
- Omit 40,359,504 bytes of unused optional Windows ML native binaries. Same CPU TFLite engine and buffering.

## 0.2.24 — 2026-10-18

- Larger Arabic captions/body with explicit IBM font, safe glyph/line bounds across setup, pages and popups.
- Keep Arabic colour emphasis connected; preserve native icons and the developer wordmark.
- Phone guide explains restarting Shizuku after phone reboot without reinstalling.

## 0.2.23 — 2026-10-18

- Source app is a closed-by-default dropdown with scrolling for long lists.
- App icons in Home and Advanced; system/fallback icons where appropriate.
- Small decoded icons and bounded cache; same native audio engine.

## 0.2.22 — 2026-10-18

- Separate Source app section below Playback device in Home's Audio output menu.
- Refresh source apps and output devices before opening; synchronize with Advanced.
- Same native audio engine, presets and buffering; Android unchanged.

## 0.2.21 — 2026-10-18

- Closing a source app no longer locks Stop, Exit or later startup.
- Keep absent app restoration separately; retry when it returns, preserving
  external route choices and protecting the actively filtered app.
- Selected source ending returns to OFF and leaves modes usable.
- Reserve final-glyph space for buffering labels.
- Native routing regression/build passed; final UI check stopped with Escape.
- Same model, DSP, gains and buffer reserves. Android0.3.23 unchanged.

## 0.2.20 — 2026-10-18

- Compact Advanced: full-width Source app, paired Virtual input / Audio capture,
  smaller spacing and one Refresh button. Arabic mirrors the layout.
- Desktop/portable launchers updated; includes Android0.3.23/code29.
- Same audio engine, gains and buffer durations.

## 0.2.19 — 2026-10-18

- Help and Advanced stay visible; section headings have no icons.
- Keep VB-CABLE Administrator, headphone effects / Dolby and setup in Help.
- Remove Audio tools, Volume mixer and separate volume-help entry.
- Built and reviewed in EN/AR; superseded by0.2.20 without a separate ZIP.

## 0.2.18 — 2026-10-18

- Correct Arabic audio-menu font and alignment; complete buffering labels.
- Organized Settings with icons and a reliable selected-theme caption.
- Start again on the last Phone setup page.
- Includes shared welcome, window/menu polish, seven-page Phone setup,
  verified download progress and Sukut installation when missing.
- Bundled Android0.3.22; same audio engine and quality.

## 0.2.17 — 2026-10-18 (intermediate)

- Settings/icons, label spacing and phone-guide restart implemented and built.
- UI review interrupted; superseded by0.2.18 without a portable release.

## 0.2.16 — 2026-10-18

- Audio buffering and output menus match their button widths in both languages.
- Window minimum dimensions respect the monitor work area.
- Includes the welcome, Arabic and seven-step phone setup improvements in 0.2.15.
- Same native engine and audio quality.

## 0.2.15 — 2026-10-18

- New shared welcome line and improved Arabic setup copy.
- Roomier window, compact dropdowns that prefer opening below, refreshed devices.
- Fixed Phone Back/Next/steps; simplified actions and download progress.
- Install bundled Sukut on the phone only if absent.
- Native build paths repaired for CoS - BACKUP; previous AVX runtime retained.
- Existing audio processing, voice levels, buffers and volume behaviour retained.

## 0.2.14 — 2026-10-17

- Home now has one row with two compact buttons that open small menus, like the
  phone: **Audio output** (device list with icons) and **Audio buffering** (the
  three coloured choices plus the tip).
- The Phone tab is now a six-page wizard (one step per page, Back / Step N of 6 /
  Next, icons and coloured headings) instead of one long page.
- Home Audio output was a card like Audio mode, with a standard full-width
  dropdown instead of a fixed-width settings row.
- Home Audio buffering is now three narrow coloured choices side by side
  (Lowest green / Balanced amber / Maximum red) with icons and a short
  explanation, instead of a slider; the same control is used by setup.
- The buffering card has a centered amber title like Audio mode, no corner
  value label and no per-choice description lines.
- Shorter, icon-free in-app changelog; it now says "buffering choices".
- Desktop launcher recompiled to open 0.2.14; Phone tab, short mode tips, the
  precautionary startup step and the checkbox-free VB-CABLE guide are unchanged.
- No audio engine, DSP, gain, buffer-duration or volume change.
- [Implementation and checks](docs/redesign/V0214_BUFFERING_POLISH.md).

##0.2.13 — 2026-10-17

- Put the short mode explanation inside Audio mode.
- Updated the Desktop launcher, which still opened0.2.11, to launch0.2.13.
- Verified Phone setup, Home/setup buffering, optional two-step VB-CABLE guide
  without a checkbox, and concise changelog in the running app.
- No audio engine or saved sound settings changed.
- [Implementation and checks](docs/redesign/V0213_CONTROLS_RELEASE.md).

## 0.2.12 — 2026-10-17

- Phone tab: official Shizuku13.5.4 installation/start through selected USB device, with pictures.
- Three-level buffering slider on Home and setup; short colored mode tips.
- Optional two-step VB-CABLE check; removed checkbox gate and adjust-volume step.
- Short changelog without icons. Native/WinUI build and33managed checks passed.
- Existing0.2.11session preserved; no new live PC audio/UI test.
- Details: [release report](docs/redesign/V0212_PHONE_SETUP_RELEASE.md).

# Sukut release history

## 0.2.11 — 2026-10-16

- Active audio faults and repeated overload attempt three automatic restarts,
  delayed1/2/4seconds, with an amber Reconnecting status. Failed attempts then
  show Attention/headroom with Restart; actionable setup/restoration errors
  still show guidance immediately. OFF/Exit/manual Restart cancel retries.
- Retry budget survives short-lived sessions; a healthy minute replenishes it.
- Optimized background is limited to−20…+10dB/default0. Old lower values clamp
  to−20. Strict background mute, Optimized music mute and all voice0dB remain.
- Background and amplification preview visually during dragging; audio/settings
  commit after release, capture cancellation or keyboard key-up. Background
  commits during another command queue for afterward. No gain-triggered restart.
- Mode controls/slider share a box; descriptions/tips remain separate. Bilingual
  history, guides, metadata and output paths updated. Same native audio/model.
- 31 managed tests passed, build/publish/package completed. Existing running
  0.2.10 session preserved; no new live audio/UI, battery or latency measurement.
  Report: docs/redesign/V0211_AUTOMATIC_RECOVERY_RELEASE.md. Next patch0.2.12.
## 0.2.10 — 2026-10-16

- Audio mode heading, centered buttons and Optimized background slider share
  one box. Colored mode details and all tips remain separate below/above it.
- Includes the unshipped 0.2.9 reliability changes below. Final build, Desktop
  launcher, preview ZIP and bilingual guides/changelogs use 0.2.10.
- Bounded final English/Arabic layout check; audio evidence retained from 0.2.9.
  Report: docs/redesign/V0210_RELIABILITY_RELEASE.md. Next PC patch 0.2.11.

## 0.2.9 — 2026-10-16 (development revision, included in 0.2.10)

- Attention and headroom tips gain Restart, which stops and recreates clients.
- Detect stopped workers, stale health, pending output without progress and
  externally changed Windows/source-app routes. Preserve legitimate idle input.
- Optimized mutes music; saved live background slider spans −100…+10 dB.
  Voice remains 0 dB in every mode. Optimized also uses silent recovery.
- Headroom tip counts actual repeated fallbacks or prolonged recovery, not
  short slow frames already absorbed by buffering. Recovery remains visible.
- Capture and output use two workers: direct inference-to-WASAPI output removes
  the rendered queue, one worker and four output packets (about 15 KiB storage).
  Capture opts out of execution-speed throttling; render inherits Pro Audio QoS.
  Routine health snapshots avoid copying/sorting timing histories.
- Same model, sample format, FFT, TFLite arithmetic, one inference thread and
  playback reserves. No proven acoustic-latency or whole-process RAM percentage.
- Bounded 16-second baseline/direct tests: zero fallback/transport underrun
  estimates. Native audio/realtime/controller and 26 managed contracts passed.
  Actual WinUI playback continued in tray; live slider, route-loss reporting,
  restart, OFF and Exit passed. CPU kept up on AC; no unplugged comparison.
- User's intentional Realtek volume adjustment 47%→25% retained. Focused route/
  restart recheck preserved current volume/defaults/mute. Runtime prefs restored.
- Metadata-only current health/last-fault files added. No microphone recording.
  Physical-master volume, manual driver confirmation, independent guard and
  Windows-rendered menus preserved. Android 0.3.15/code21 untouched.
## 0.2.8 — 2026-10-16

- Fixed mode cards appearing selected while volume setup was unconfirmed.
  For All system audio, the mode selector is disabled until the checkbox is
  ticked. Selection handling and engine start share the same readiness rule.
- Confirmation restores the actual engine selection and immediately refreshes
  enabled state. The first mode click after confirmation can start processing;
  no stale blocked selection or second/repeated selection is required.
- Removing confirmation still stops active all-system processing before locking
  the selector. Specific-app routing keeps its existing confirmation exemption.
  Checking the box does not automatically start an earlier blocked mode.
- Updated English/Arabic tip and in-app/full changelogs, guides, version metadata,
  Desktop/publish/package paths and handoffs. Voice0dB, admin buttons, menu photo,
  native menu theme and physical-master audio behavior are unchanged.
- Build/package only; no runtime checks/tests. Next requested PC patch0.2.9.
  Report: docs/redesign/V028_MODE_CONFIRMATION_RELEASE.md.

## 0.2.7 — 2026-10-16

- The Home volume tip has an Open VB-CABLE as administrator button and a
  persistent manual confirmation checkbox. Confirming the driver option is OFF
  saves and dismisses the tip; cancelling/merely opening UAC does not confirm it.
- Settings and startup setup share the known-local-path elevated control-panel
  launcher. Sukut itself stays unelevated. UAC cancellation is handled quietly.
- Setup retains the control-panel photo and restores the real Options-menu photo,
  with an explicit instruction to REMOVE its pictured Windows Volume Control
  checkmark. Click the menu photo to enlarge.
- Voice is0dB in all four PC modes, including Music Filter and Strict; both
  managed presets and the native legacy fallback changed. Music/background
  settings and the separate optional amplification slider are unchanged.
- Native HMENU tray menus opt into Windows light/dark, independent of Sukut's
  selected theme. Refresh at startup, theme/settings changes and each open;
  high contrast uses Windows defaults. Optional private uxtheme exports use the
  Microsoft PowerToys approach; missing exports retain the ordinary native menu.
- Preserves0.2.6 physical-master volume/restoration, idle capture and Exit fixes.
  No added audio buffer/model/audio-format change or quality/latency benchmark.
- Versions, English/Arabic in-app/full changelogs, guides and handoffs updated.
  Build/package only; runtime/UAC/menu/audio validation remains with the user.
- Report: docs/redesign/V027_ADMIN_TIP_RELEASE.md. Next PC patch0.2.8.

## 0.2.6 — 2026-10-16

- Corrected the user-reported0.2.5 volume regression: removed forced100% physical
  output and virtual-input attenuation as the listening-volume control.
- The real device stays master. All-system Windows volume/mute is mirrored
  between CABLE Input metadata and the physical output. Requires VB-CABLE's
  Enable Windows Volume Control OFF, confirmed manually in Settings or setup.
  Global start is blocked until confirmation; this is not driver-state detection.
- Specific-app transport requests0dB, avoiding maximum scalar/possible boost.
  v4 journaling preserves physical volume; legacyv3 full-scale recovery prefers
  the recorded pre-session physical setting. No inverse sample amplification.
- Standard Windows HMENU replaces custom owner drawing of the tray menu.
- Live WASAPI input may idle while content is paused. Resume starts a fresh
  segment; device/API errors and other adapter watchdogs remain active.
- OFF waits for a completed-stop acknowledgment and clears a stale stream fault
  after successful teardown. Exit disposes directly, with bounded fallback and
  independent journal recovery; errors no longer reopen the Attention loop.
- Updated0.2.6 versions, bilingual guidance/changelogs and preview handoffs.
  No model, stem preset, amplification or audio-buffer change. Build/package
  only; runtime/menu/volume/idle/resume/exit/crash tests remain with the user.
- Report: docs/redesign/V026_VOLUME_EXIT_RELEASE.md. Next PC patch0.2.7.

## 0.2.5 — 2026-10-16

- Fixed the physical-output volume ceiling in active all-system routing. The
  selected output’s percentage/mute transfers to CABLE Input before the physical
  endpoint is temporarily set to full scale. Windows volume can use its full range.
- OFF/Exit transfer the current virtual master back before restoring direct
  playback, then restore the cable’s previous volume/mute. Explicit OFF still
  uses the output selected in Sukut.
- Specific-app routing uses the physical output as master and a unity/unmuted
  cable transport, avoiding a second volume limit on the source stream.
- Durable v3 restoration journal (v1/v2 readable), endpoint-volume notifications,
  error propagation and an idle separate crash-restoration guard. Owner tokens
  keep old guards from restoring a newer session; partial restoration remains
  retryable. Direct hardware-level changes are not mistaken for virtual-master
  percentage requests.
- Updated English/Arabic setup, Audio tools, cumulative in-app changelog, app/
  assembly/native versions, build/start/package paths and handoffs.
- No model, DSP, stem gains, buffering or audio format changes; no added audio
  buffer. Runtime volume/mute/crash/listening validation remains with the user.
- Report: docs/redesign/V025_VOLUME_RELEASE.md.

## 0.2.4 — 2026-10-16

- Moved Audio tools into Settings, directly above Advanced audio routing.
- The section is collapsed by default. Removed its separate navigation tab and
  deferred-page state.
- Kept VB-CABLE, Volume mixer, volume instructions and headphone/Dolby help
  together. Updated English/Arabic setup guidance to the new location.
- The Choose a source app shortcut now opens and focuses Advanced routing from
  the same Settings page.
- Updated application/assembly/native project versions, About, What's new,
  launcher paths, package metadata, guides and current handoffs to 0.2.4.
- Preserved the cumulative release highlights in About and this full history in
  the shareable package. Future requested changes increment the patch version
  and receive a changelog entry, including small changes.
- Built and packaged by request. No runtime tests, listening checks or new
  benchmarks; audio processing and quality settings are unchanged in this patch.

## 0.2.3 — cumulative updates, 2026-10-15

### Interface, setup and tray

- Four centered mode cards: OFF, Music Filter (formerly Halal Mode), Optimized
  and Strict. Selected cards retain full color; others are muted with colored
  hover feedback. Removed the selection side stripe and checkmark.
- Added colored voice/music/background details for the selected mode. IBM Plex
  Sans Arabic for Arabic Home text/titles, Readex Pro for English and Handjet
  for the animated rainbow Arabic developer credit.
- Reduced Home's top whitespace and fully collapsed closed notification bars.
  Constrained About to its viewport and showed the correct release version in
  both About and What's new.
- Refined the supplied ivory calligraphy icon; Desktop and tray use the same
  icon. System-themed tray menu, close/reopen behavior and single-instance
  handling. Share one tray icon handle and exclude unused authoring assets from
  published output while retaining the source files.
- Refreshed playback devices and advanced routing lists before dropdowns open.
  Advanced controls use full-width native ComboBoxes with bounded popup height.
- Bilingual illustrated setup with conditional VB-CABLE download, device
  selection and simple mode descriptions. Removed Later. Installed VB-CABLE
  skips the driver download page.
- Reordered volume-key setup: first open the VB-CABLE panel with its image,
  then Options → Enable Windows Volume Control with the menu image, then Volume
  mixer. Removed unnecessary screenshot descriptions.
- Grouped VB-CABLE, Volume mixer, volume guidance and headphone effects. A
  separate Audio tools page was introduced and is superseded by the collapsed
  Settings section in 0.2.4.

### Audio and performance

- Explicit OFF switches to the playback device selected in Sukut. Emergency
  restoration retains its recorded original-route behavior.
- Three Settings-only buffering levels: Lowest (Default), Balanced and Maximum.
  Lowest retains the previous behavior; higher levels add reserve to absorb
  short stalls with additional delay and a little RAM. Added repeated-overload
  guidance on Home and retained short recovery events in status reporting.
- Live recovery first retries after 2 hops (40 ms of audio) instead of 13 hops
  (260 ms), with failed probes backing off to 4/8/13 hops. Two healthy probes
  remain required. No added default audio buffer or AI quality reduction.
- Shared Media playback allows endpoint effects where Windows/the driver
  supports them. Specific-app routing keeps selected physical headphones as
  Windows' default for Dolby Access while filtering only the selected app.
  All-system routing still uses VB-CABLE. Processed audio remains stereo and
  cannot preserve source Atmos objects/positions. No new effects processor or
  extra effects buffer was added.
- Route restoration journal v2 records prior global defaults; v1 remains
  readable. Audio I/O requests high MMCSS priority.
- Normal playback omits 128 KiB of timing-history arrays and CPU-time sampling;
  diagnostics tools retain their defaults.
- Settings amplification slider: 0 to +12 dB, default 0 dB, applied live in
  processing modes without a restart or extra buffer. Shared stereo gain uses
  current-chunk peak protection and smoothing. OFF remains direct playback.

## 0.2 through 0.2.2 — earlier release highlights

- Rewrote the desktop interface in native WinUI 3, C# and XAML over the existing
- Renamed the app to سُكوت Sukut, developed by الطرفية. Added English/Arabic,
  RTL, System/Light/Dark themes, close to tray and advanced audio routing.
- Added OFF, Music Filter/Halal, Optimized and Strict presets. Music Filter:
  voice +6 dB, music muted, background 0 dB. Optimized: voice 0 dB, music −20 dB,
  background 0 dB. Strict: voice +6 dB, music/background muted.
- Music Filter/Strict use silent overload recovery; Optimized uses aligned
  original fallback. Addressed early jitter and processed/original switching.
- Added setup, real volume-control screenshots, launch shortcuts, branding,
  icon refinements, status pills and cumulative What's new highlights.
- Deferred hidden pages, released the model/pipeline when OFF, stopped hidden
  OFF polling and used bounded pools/lightweight snapshots.
- Earlier measured private RAM: active 97.13 → 76.47 MiB (21.3% lower); fresh
  OFF 61.69 → 58.32 MiB (5.5% lower). Earlier scheduling removed a 2 ms retry
  sleep; AI mean 13.30 ms and p99 14.33 ms. These short historical development
  runs are not benchmarks of 0.2.4 or measured end-to-end listening latency.
- Added complete portable preview bundles with self-contained .NET/WinUI,
  native engine/model, app-local Visual C++ runtime, fonts, licenses and
  bilingual guides. VB-CABLE is installed separately. Personal settings and
  development evidence are excluded from shareable packages.

Earlier detailed evidence remains in docs/redesign/ROW_LATENCY_REPORT.md,
docs/redesign/BRANDING_CHANGELOG_RELEASE.md, docs/redesign/V023_RELEASE.md,
docs/redesign/V023_FOLLOWUP.md and docs/steps/step-08/REPORT.md in the source
project. Historical measurements are retained with their limits; no new runtime
tests or benchmarks were run for 0.2.4. Stage 8 remains IN_PROGRESS.
