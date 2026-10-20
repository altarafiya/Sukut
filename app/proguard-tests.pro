# Device probes remain a separate developer-only APK; keep reflective JUnit tests.
-dontoptimize
-dontobfuscate
-keep class com.osama.cleanwave.** { *; }
-dontwarn ai.onnxruntime.**
-dontwarn com.google.errorprone.annotations.CanIgnoreReturnValue
-dontwarn com.google.errorprone.annotations.MustBeClosed
