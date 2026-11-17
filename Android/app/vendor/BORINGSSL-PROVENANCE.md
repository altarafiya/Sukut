# Local ADB pairing primitive

- Upstream: https://github.com/google/boringssl
- Pinned source commit: `e651545da6e26e1f4420723a3b69a78ed1b9c51d`
- Download: https://codeload.github.com/google/boringssl/tar.gz/e651545da6e26e1f4420723a3b69a78ed1b9c51d
- Licence: see `boringssl/LICENSE`, including its embedded component notices.
- Build: `tools/build-local-adb-crypto.ps1`; NDK 28.2.13676358, CMake 3.22.1, ARM64 API29, static C++/crypto, stripped, 16 KiB ELF pages.
- Only SPAKE2 + HKDF/SHA256 are reachable through four JNI methods. TLS 1.3, RSA identity and AES-GCM use Android's platform/Keystore implementations. The audio worker never loads this library.
- ADB interoperability references: AOSP `packages/modules/adb/pairing_auth` and `pairing_connection`; Android Wireless debugging documentation. Protocol interpretation was cross-checked against libadb-android (Apache-2.0 alternative licence): https://github.com/MuntashirAkon/libadb-android . No libadb, BouncyCastle, Conscrypt or Shizuku dependency is bundled.
