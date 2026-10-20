# CleanWave Windows dependency lock

This file records the exact Step 2 native dependency inputs. All archives, sources and build outputs remain inside the Windows workspace.

## TensorFlow Lite

- Project: TensorFlow / TensorFlow Lite
- Required baseline: **2.16.1**
- Upstream tag: `v2.16.1`
- Resolved commit: `5bc9d26649cca274750ad3625bd93422617eed4b`
- Downloaded tag archive: `https://github.com/tensorflow/tensorflow/archive/refs/tags/v2.16.1.zip`
- Local archive: `deps/cache/tensorflow-v2.16.1.zip`
- Archive bytes: `95,820,375`
- Archive SHA-256: `54C976F828182F85E10E03840DD3B0504109F57760A498075574E35E9AA983FE`
- Extracted source: `deps/src/tensorflow-2.16.1`
- License: Apache-2.0; verbatim upstream license retained under `deps/licenses/`.

The build sets `OVERRIDABLE_FETCH_CONTENT_GIT_REPOSITORY_AND_TAG_TO_URL=ON` because Git is not installed on this host. TensorFlow 2.16.1 then resolves its own pinned CMake dependencies through archives. The Step 2 baseline disables XNNPACK, external delegates, GPU support, RUY and TensorFlow Lite kernel tests; later performance experiments require their own stage-specific evidence.

## KissFFT

- Project: KissFFT
- Version/tag: **131.1.0**
- Resolved commit: `8f47a67f595a6641c566087bf5277034be64f24d`
- Downloaded tag archive: `https://github.com/mborgerding/kissfft/archive/refs/tags/131.1.0.zip`
- Local archive: `deps/cache/kissfft-131.1.0.zip`
- Archive bytes: `70,880`
- Archive SHA-256: `9C2E19CC34ED910DCB509FD8AB561A523B923B6578703ACE8C8F37F5A286BB25`
- Extracted source: `deps/src/kissfft-131.1.0`
- License: BSD-3-Clause; verbatim upstream license retained under `deps/licenses/`.

KissFFT is configured for float, static linking, no OpenMP, no tools/tests/pkg-config, and no `alloca`. Its floating-point implementation is unscaled in both directions; Step 4 must explicitly apply the required normalized inverse factor and independently validate the exact 1536-point waveform contract.

## FXdiv transitive source pin

TensorFlow Lite 2.16.1 pulls `pthreadpool` commit `4fe0e1e183925bf8cfa6aae24237e724a96479b8`. That upstream CMake file attempts to clone FXdiv `master` when `FXDIV_SOURCE_DIR` is absent. Because this machine has no Git and an unpinned `master` would weaken reproducibility, CleanWave supplies the exact FXdiv revision already pinned by TensorFlow 2.16.1's own Bazel workspace:

- Project: FXdiv
- Commit: `63058eff77e11aa15bf531df5dd34395ec3017c8`
- Archive: `https://codeload.github.com/Maratyszcza/FXdiv/zip/63058eff77e11aa15bf531df5dd34395ec3017c8`
- Local archive: `deps/cache/fxdiv-63058eff77e11aa15bf531df5dd34395ec3017c8.zip`
- Archive bytes: `16,646`
- Archive SHA-256: `3D7B0E9C4C658A84376A1086126BE02F9B7F753CAA95E009D9AC38D11DA444DB`
- Extracted source: `deps/src/FXdiv-63058eff77e11aa15bf531df5dd34395ec3017c8`
- License: MIT; verbatim upstream license retained as `deps/licenses/fxdiv-MIT.txt`.

`CMakeLists.txt` sets `FXDIV_SOURCE_DIR` to this local source before TensorFlow Lite is added, preventing the transitive Git clone without installing Git or changing the host.

The top-level cache requests `TFLITE_ENABLE_RUY=OFF`, but TensorFlow Lite 2.16.1's CMake dependency graph still configures/builds several RUY support libraries while producing the static runtime. The cache value and observed build behavior are both retained in Step 2 evidence; no inference delegate is enabled by the bootstrap executable.

## Toolchain baseline

- Visual Studio Build Tools 2022 17.14.40
- MSVC 19.44.35228, x64
- Windows SDK 10.0.26100.0
- CMake 3.31.6-msvc6 (Visual Studio bundled)
- Visual Studio generator: `Visual Studio 17 2022`, architecture `x64`

No system package manager, PATH modification or toolchain installation is required by this lock.

## Step 3 runtime configuration update

The current build enables RUY (TFLITE_ENABLE_RUY=ON) to match the pinned TensorFlow Lite 2.16.1 Android CPU kernel configuration as closely as practical. The OFF references above describe the historical Step 2 bootstrap only. XNNPACK/GPU/external delegates remain OFF; inference uses one thread. See docs/steps/step-03/REPORT.md for the numerical validation of this change.

