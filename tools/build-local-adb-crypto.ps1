param([string]$Sdk = "$env:LOCALAPPDATA\Android\Sdk")
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$cmake = Join-Path $Sdk 'cmake\3.22.1\bin\cmake.exe'
$ninja = Join-Path $Sdk 'cmake\3.22.1\bin\ninja.exe'
$toolchain = Join-Path $Sdk 'ndk\28.2.13676358\build\cmake\android.toolchain.cmake'
$build = Join-Path $root 'work\android-beta13\pairing-native'
& $cmake -S "$root\app\src\main\cpp\local-adb" -B $build -G Ninja "-DCMAKE_MAKE_PROGRAM=$ninja" "-DCMAKE_TOOLCHAIN_FILE=$toolchain" -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 -DCMAKE_BUILD_TYPE=MinSizeRel -DANDROID_STL=c++_static
if ($LASTEXITCODE -ne 0) { throw 'Pairing native configuration failed' }
& $cmake --build $build --target sukut_pairing --parallel 2
if ($LASTEXITCODE -ne 0) { throw 'Pairing native build failed' }
$destination = Join-Path $root 'app\src\main\jniLibs\arm64-v8a'
New-Item -ItemType Directory -Force $destination | Out-Null
Copy-Item -LiteralPath "$build\libsukut_pairing.so" -Destination $destination
