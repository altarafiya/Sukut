# Sukut acoustic latency measurement tool

Separate, disposable Android app. It is NOT linked into the Sukut APK. Only
this tool declares RECORD_AUDIO; regular Sukut keeps its existing permissions.
Use microphone recording only with the phone owner's explicit approval and
other audio paused, in a quiet place. It emits only its generated quiet chirps.

Build from the CoS root with JDK17:

```powershell
.\gradlew.bat :latencyProbe:assembleDebug :app:assembleEngineAndroidTest --offline --no-daemon --max-workers=2
adb install -r tools/latency-probe/build/outputs/apk/debug/latencyProbe-debug.apk
adb install -r app/build/outputs/apk/androidTest/engine/app-engine-androidTest.apk
adb shell pm grant com.osama.sukut.latencyprobe android.permission.RECORD_AUDIO
```

Xiaomi may require the owner to approve installation manually. Do not bypass
that restriction. Both APKs can be copied into Downloads for manual install.

The test runner binds a separate versioned Shizuku worker, stops the regular
route through its own app UID and removes the test worker afterward. Use fresh
run tags; the recorder intentionally refuses to overwrite an existing run.
For reliable phase isolation, stop only this helper between single-phase tests:

```powershell
adb shell am force-stop com.osama.sukut.latencyprobe
adb shell am instrument -w -r -e class com.osama.cleanwave.PhoneTuningTest#measureAcousticRoutes -e run_tag example1 -e phases off_a -e profile 0 com.osama.sukut.test/androidx.test.runner.AndroidJUnitRunner
```

Repeat with phases unity, optimized, off_b using the same tag. The unity remix
is TEST ONLY, with the actual AI running and all stems0dB to retain a known
chirp. Optimized uses the actual Android0/−20/−10dB preset. Never call a unity
probe the Music Filter preset. The runner also requests a fresh task because
Android otherwise reused a completed recorder screen in early attempts.

Recordings/results are at
/sdcard/Android/data/com.osama.sukut.latencyprobe/files/<tag>_<phase>.
Pull them to local evidence, then:

```powershell
python tools/latency-probe/analyze_acoustic.py <off_a_dir> <unity_dir> <optimized_dir> <off_b_dir>
```

The tool uses48kHz mono microphone PCM, fixed9600frame source reserve and quiet
40ms700→3400Hz chirps every2s. The source reserve is deliberately large enough
to avoid OEM deep-buffer starvation on both physical and intercepted routes.
An earlier40ms source ring underrun450times and those comparisons were rejected.
Source prefill avoids startup starvation. A finite source can underrun once
during the final1.2s microphone tail, after all test data were supplied; compare
source_underruns_while_feeding separately from source_underruns.

Analysis correlates the known waveform and maps its microphone sample position
to monotonic time using AudioRecord hardware timestamps. Every marker has a
nonblocking source-enqueue time bracket. Require strong, consistent correlation,
valid microphone clocks, correct routes and no feeding underruns. Mic input is
UNPROCESSED where supported, otherwise VOICE_RECOGNITION, with preferred built-in
mic. Keep the same input source/device/volume/settings between compared runs.

Absolute source-enqueue→microphone time includes the generator's own200ms
reserve, Android/OEM output and microphone path. ON minus the bracketing OFF
baseline estimates added audible path delay; fixed microphone/group delay
cancels. It is not a universal device number or a Bluetooth result. Record
per-chirp variation and repeated route startups; inference duration and the
capture/render software timestamp gap are separate measurements.

Restore the user's prior app settings/running state after tests, remove both
temporary helper packages, and retain local evidence separately from releases.
The0.3.11 work-in-progress and final report document actual runs/limitations.
