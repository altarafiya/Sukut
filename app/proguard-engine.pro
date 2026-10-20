# Shrink only. Preserve numerical implementation, reflection and developer probes.
-dontoptimize
-dontobfuscate
-keepattributes Signature,InnerClasses,EnclosingMethod,RuntimeVisibleAnnotations,AnnotationDefault
-keep class com.osama.cleanwave.StreamingCleaner { *; }
-keep class com.osama.cleanwave.SeparationModel { *; }
-keep class com.osama.cleanwave.StreamingDsp { *; }
-keep class com.osama.cleanwave.PrivilegedAudioService { *; }
# Instrumentation and developer helpers share the app's Kotlin class loader.
# Retain its public runtime surface, including APIs used only by those helpers.
-keep class kotlin.** { *; }
# Preserve UI/probe entry points and Kotlin default-argument bridges. Only the
# unused legacy cleaners/model wrappers may disappear from this variant.
-keep class !com.osama.cleanwave.DialogueCleaner,!com.osama.cleanwave.SingingCleaner,!com.osama.cleanwave.OnnxVocalModel,!com.osama.cleanwave.OnnxDialogueModel,com.osama.cleanwave.** { *; }
# The legacy ONNX implementation is not used by the Audio variant and its
# runtime is deliberately compileOnly here. Other variants retain it unchanged.
-dontwarn ai.onnxruntime.**
# JLargeArrays' optional off-heap (>2^31-element) JVM path references this
# desktop-only type. Sukut uses the same 1536-element in-heap FFT as before.
-dontwarn sun.misc.Cleaner
