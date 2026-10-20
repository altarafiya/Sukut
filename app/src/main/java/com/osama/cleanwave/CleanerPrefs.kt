package com.osama.cleanwave

import android.content.Context
import android.content.SharedPreferences
import android.provider.Settings

object CleanerPrefs {
    // Keep the old Custom ID only to migrate existing installations safely.
    const val SOURCE_DEFAULT = 0
    const val SOURCE_OPTIMIZED = SOURCE_DEFAULT
    const val SOURCE_CUSTOM = 1
    const val SOURCE_MUSIC_FILTER = 2
    const val SOURCE_STRICT = 3
    private const val FILE = "cleanwave_state"
    private const val KEY_ENABLED = "enabled"
    private const val KEY_RESTORE_ON_BOOT = "restore_on_boot"
    private const val KEY_STATUS = "status"
    private const val KEY_ERROR = "error"
    private const val KEY_BOOT_COUNT = "last_boot_count"
    private const val KEY_MUSIC_REDUCTION = "music_reduction"
    private const val KEY_VOICE_DB = "audio_voice_db"
    private const val KEY_MUSIC_DB = "audio_music_db"
    private const val KEY_OTHER_DB = "audio_other_db"
    private const val KEY_SOURCE_PRESET = "audio_source_preset"

    private fun prefs(context: Context) =
        context.getSharedPreferences(FILE, Context.MODE_PRIVATE)

    fun isEnabled(context: Context): Boolean = prefs(context).getBoolean(KEY_ENABLED, false)

    fun musicReduction(context: Context): Int = prefs(context).getInt(KEY_MUSIC_REDUCTION, 100).coerceIn(0, 100)

    fun prepareNewEngine(context: Context) {
        if (prefs(context).getInt("engine_revision", 0) < 4) prefs(context).edit()
            .putInt("engine_revision", 4).putInt(KEY_MUSIC_REDUCTION, 100)
            .putInt("processing_mode", MusicRemovalProcessor.DIALOGUE).putInt("voice_boost", 0)
            .putInt(KEY_VOICE_DB, 0).putInt(KEY_MUSIC_DB, -10).putInt(KEY_OTHER_DB, -20)
            .apply()
        val state = prefs(context)
        if (!state.getBoolean("simple_routing_v1", false)) state.edit()
            .remove("source_app_package").putBoolean("simple_routing_v1", true).apply()
        if (!state.contains(KEY_SOURCE_PRESET)) {
            state.edit().putInt(KEY_SOURCE_PRESET, SOURCE_MUSIC_FILTER).apply()
        }
        if (state.getInt(KEY_SOURCE_PRESET, SOURCE_MUSIC_FILTER) == SOURCE_CUSTOM)
            state.edit().putInt(KEY_SOURCE_PRESET, SOURCE_MUSIC_FILTER).apply()
    }

    fun processingMode(context: Context): Int = if (BuildConfig.ENGINE_RUNTIME) MusicRemovalProcessor.DIALOGUE
        else prefs(context).getInt("processing_mode", MusicRemovalProcessor.DIALOGUE)
    fun setProcessingMode(context: Context, mode: Int) { prefs(context).edit().putInt("processing_mode", mode).apply() }
    fun voiceBoost(context: Context): Int = prefs(context).getInt("voice_boost", 0).coerceIn(0, 6)
    fun setVoiceBoost(context: Context, db: Int) { prefs(context).edit().putInt("voice_boost", db.coerceIn(0, 6)).apply() }
    fun sourcePreset(context: Context): Int = prefs(context).getInt(KEY_SOURCE_PRESET, SOURCE_MUSIC_FILTER)
        .takeIf { it == SOURCE_OPTIMIZED || it == SOURCE_MUSIC_FILTER || it == SOURCE_STRICT } ?: SOURCE_MUSIC_FILTER

    fun setSourcePreset(context: Context, preset: Int) {
        val safe = preset.takeIf { it == SOURCE_OPTIMIZED || it == SOURCE_MUSIC_FILTER || it == SOURCE_STRICT } ?: SOURCE_MUSIC_FILTER
        if (sourcePreset(context) != safe) prefs(context).edit().putInt(KEY_SOURCE_PRESET, safe).apply()
    }

    fun customVoiceDb(context: Context): Int = prefs(context).getInt(KEY_VOICE_DB, 0).coerceIn(-100, 10)
    fun customMusicDb(context: Context): Int = prefs(context).getInt(KEY_MUSIC_DB, -10).coerceIn(-100, 10)
    fun customOtherDb(context: Context): Int = prefs(context).getInt(KEY_OTHER_DB, -20).coerceIn(-100, 10)
    fun sourceVoiceDb(context: Context): Int = 0
    fun sourceMusicDb(context: Context): Int = -100
    fun optimizedBackground(context: Context): Int = EnginePerformanceConfig.background(prefs(context).getInt("optimized_background_db", -10))
    fun setOptimizedBackground(context: Context, db: Int) {
        val value = EnginePerformanceConfig.background(db)
        if (optimizedBackground(context) == value) return
        prefs(context).edit().putInt("optimized_background_db", value).apply()
        if (sourcePreset(context) == SOURCE_OPTIMIZED) reconfigure(context)
    }
    fun sourceOtherDb(context: Context): Int = when (sourcePreset(context)) {
        SOURCE_OPTIMIZED -> optimizedBackground(context)
        SOURCE_STRICT -> -100
        else -> 0
    }
    fun silentRecovery(context: Context): Boolean = sourceMusicDb(context) <= -100

    fun buffering(context: Context): Int = prefs(context).getInt("buffering", 1).coerceIn(0, 2)
    fun setBuffering(context: Context, profile: Int) {
        setEnginePerformance(context, effectsCompatibility(context), profile)
    }
    private fun reconfigure(context: Context) {
        if (CleanerService.isAlive && isEnabled(context)) context.startService(
            android.content.Intent(context, CleanerService::class.java).setAction(CleanerService.ACTION_CONFIGURE))
    }
    fun customPerformance(context: Context) = prefs(context).getBoolean("engine_performance_custom", false)
    internal const val BLUETOOTH_OUTPUT_KEY = "bluetooth_output_key"
    fun bluetoothOutput(context: Context) = prefs(context).contains(BLUETOOTH_OUTPUT_KEY)

    /** Only the privileged playback track reports routes: paired/idle devices do not count. */
    fun observeOutputRoute(context: Context, deviceId: Int, deviceType: Int) {
        if (deviceId < 0 || deviceType <= android.media.AudioDeviceInfo.TYPE_UNKNOWN) return
        val bluetooth = deviceType in setOf(android.media.AudioDeviceInfo.TYPE_BLUETOOTH_A2DP,
            android.media.AudioDeviceInfo.TYPE_BLUETOOTH_SCO, android.media.AudioDeviceInfo.TYPE_HEARING_AID,
            android.media.AudioDeviceInfo.TYPE_BLE_HEADSET, android.media.AudioDeviceInfo.TYPE_BLE_SPEAKER,
            android.media.AudioDeviceInfo.TYPE_BLE_BROADCAST)
        val state = prefs(context)
        val current = BluetoothPerformancePolicy.Options(effectsCompatibility(context), buffering(context), customPerformance(context))
        val previous = if (state.contains("bluetooth_previous_buffering")) BluetoothPerformancePolicy.Options(
            state.getBoolean("bluetooth_previous_effects", false), state.getInt("bluetooth_previous_buffering", 1),
            state.getBoolean("bluetooth_previous_custom", false)) else null
        val before = BluetoothPerformancePolicy.State(state.getString(BLUETOOTH_OUTPUT_KEY, null), previous)
        val change = BluetoothPerformancePolicy.observe(before, "$deviceType:$deviceId", bluetooth, current)
        if (change.state == before && change.options == current) return
        val options = change.options
        // Route marker, snapshot and both options become visible atomically.
        state.edit().apply {
            if (change.state.device == null) {
                remove(BLUETOOTH_OUTPUT_KEY); remove("bluetooth_previous_effects")
                remove("bluetooth_previous_buffering"); remove("bluetooth_previous_custom")
            } else {
                putString(BLUETOOTH_OUTPUT_KEY, change.state.device)
                change.state.previous?.let {
                    putBoolean("bluetooth_previous_effects", it.effects)
                    putInt("bluetooth_previous_buffering", it.buffering)
                    putBoolean("bluetooth_previous_custom", it.custom)
                }
            }
            putBoolean("effects_compatibility", options.effects); putInt("buffering", options.buffering)
            putBoolean("engine_performance_custom", options.custom)
            if (options != current) remove("recovery_until").putBoolean("overload_tip", false)
        }.apply()
        // A custom-label-only change must not rebuild an identical audio route.
        if (options.effects != current.effects || options.buffering != current.buffering) reconfigure(context)
    }

    fun setEnginePerformance(context: Context, effects: Boolean, profile: Int, custom: Boolean = false) {
        val safe = profile.coerceIn(0, 2)
        val changed = buffering(context) != safe || effectsCompatibility(context) != effects
        if (!changed && customPerformance(context) == custom) return
        // Publish both options together, then request exactly one active restart.
        prefs(context).edit().putBoolean("effects_compatibility", effects).putInt("buffering", safe)
            .putBoolean("engine_performance_custom", custom).apply {
                if (changed) remove("recovery_until").putBoolean("overload_tip", false)
            }.apply()
        if (changed) reconfigure(context)
    }
    fun amplification(context: Context): Int = prefs(context).getInt("amplification_db", 0).coerceIn(0, 12)
    fun setAmplification(context: Context, db: Int) {
        prefs(context).edit().putInt("amplification_db", db.coerceIn(0, 12)).apply()
    }
    fun effectsCompatibility(context: Context): Boolean = prefs(context).getBoolean("effects_compatibility", false)
    fun sourceApp(context: Context): String = prefs(context).getString("source_app_package", "") ?: ""
    fun setSourceApp(context: Context, packageName: String) { prefs(context).edit().putString("source_app_package", packageName).apply() }
    fun sourceUid(context: Context): Int {
        val pkg = sourceApp(context)
        if (pkg.isBlank()) return -1
        return try { context.packageManager.getApplicationInfo(pkg, 0).uid } catch (_: android.content.pm.PackageManager.NameNotFoundException) { -2 }
    }
    fun setEffectsCompatibility(context: Context, value: Boolean) {
        prefs(context).edit().putBoolean("effects_compatibility", value).apply()
    }
    fun overloadTip(context: Context): Boolean = prefs(context).getBoolean("overload_tip", false)
    fun suggestMoreBuffering(context: Context) {
        if (!overloadTip(context)) prefs(context).edit().putBoolean("overload_tip", true).apply()
    }
    fun recoveryUntil(context: Context): Long = prefs(context).getLong("recovery_until", 0)
    fun recordRecovery(context: Context, repeated: Boolean) {
        prefs(context).edit().putLong("recovery_until", android.os.SystemClock.elapsedRealtime() + 3000)
            .putBoolean("overload_tip", repeated || overloadTip(context)).apply()
    }
    fun setBufferingAdvice(context: Context, show: Boolean) {
        val state = prefs(context)
        if (state.getBoolean("overload_tip", false) != show) state.edit().putBoolean("overload_tip", show).apply()
    }
    fun resetRecovery(context: Context) {
        prefs(context).edit().remove("recovery_until").putBoolean("overload_tip", false).apply()
    }

    fun setSourceMix(context: Context, voiceDb: Int, musicDb: Int, otherDb: Int) {
        val voice = voiceDb.coerceIn(-100, 10)
        val music = musicDb.coerceIn(-100, 10)
        val other = otherDb.coerceIn(-100, 10)
        val state = prefs(context)
        if (state.getInt(KEY_VOICE_DB, 0) != voice || state.getInt(KEY_MUSIC_DB, -10) != music ||
            state.getInt(KEY_OTHER_DB, -20) != other) {
            state.edit().putInt(KEY_VOICE_DB, voice).putInt(KEY_MUSIC_DB, music).putInt(KEY_OTHER_DB, other).apply()
        }
    }

    fun setMusicReduction(context: Context, percent: Int) {
        prefs(context).edit().putInt(KEY_MUSIC_REDUCTION, percent.coerceIn(0, 100)).apply()
    }

    fun reconcileRuntimeState(context: Context) {
        // Service and UI/tile share a process. A new process has no live
        // foreground service even if force-stop left old preferences behind.
        val oldStatus = status(context)
        if (!CleanerService.isAlive && !startPending(context) &&
            (isEnabled(context) || oldStatus == "Active" || oldStatus == "Starting privileged audio route")) {
            prefs(context).edit()
                .putBoolean(KEY_ENABLED, false)
                .putString(KEY_STATUS, "Off")
                .putString(KEY_ERROR, "")
                .remove("pending_start_pid")
                .apply()
        }
    }

    fun startPending(context: Context): Boolean = prefs(context).getInt("pending_start_pid", -1) == android.os.Process.myPid() &&
        android.os.SystemClock.elapsedRealtime() - prefs(context).getLong("pending_start_time", 0) in 0L..10_000L
    fun cancelledStart(context: Context): Boolean = prefs(context).getBoolean("cancel_start", false)
    fun beginStart(context: Context) {
        prefs(context).edit().putBoolean(KEY_ENABLED, true).putBoolean("cancel_start", false)
            .putInt("pending_start_pid", android.os.Process.myPid()).putLong("pending_start_time", android.os.SystemClock.elapsedRealtime())
            .putString(KEY_STATUS, "Starting privileged audio route").putString(KEY_ERROR, "").apply()
    }
    fun beginStop(context: Context) {
        prefs(context).edit().putBoolean(KEY_ENABLED, false).putBoolean("cancel_start", true)
            .putString(KEY_STATUS, "Stopping").putString(KEY_ERROR, "").apply()
    }
    fun clearStartPending(context: Context) { prefs(context).edit().remove("pending_start_pid").remove("pending_start_time").apply() }

    fun setEnabled(context: Context, enabled: Boolean) {
        val state = prefs(context)
        if (state.getBoolean(KEY_ENABLED, false) != enabled) {
            state.edit().putBoolean(KEY_ENABLED, enabled).apply()
        }
    }

    fun registerListener(context: Context, listener: SharedPreferences.OnSharedPreferenceChangeListener) {
        prefs(context).registerOnSharedPreferenceChangeListener(listener)
    }

    fun unregisterListener(context: Context, listener: SharedPreferences.OnSharedPreferenceChangeListener) {
        prefs(context).unregisterOnSharedPreferenceChangeListener(listener)
    }

    private fun bootCount(context: Context): Int =
        Settings.Global.getInt(context.contentResolver, Settings.Global.BOOT_COUNT, -1)

    fun rememberCurrentBoot(context: Context) {
        val current = bootCount(context)
        if (current >= 0) prefs(context).edit().putInt(KEY_BOOT_COUNT, current).apply()
    }

    fun consumeNewBoot(context: Context): Boolean {
        val current = bootCount(context)
        val previous = prefs(context).getInt(KEY_BOOT_COUNT, -1)
        rememberCurrentBoot(context)
        return current >= 0 && previous >= 0 && current != previous
    }

    fun restoreOnBoot(context: Context): Boolean =
        prefs(context).getBoolean(KEY_RESTORE_ON_BOOT, true)

    fun setRestoreOnBoot(context: Context, enabled: Boolean) {
        prefs(context).edit().putBoolean(KEY_RESTORE_ON_BOOT, enabled).apply()
    }

    fun status(context: Context): String =
        prefs(context).getString(KEY_STATUS, "Driver not checked") ?: "Driver not checked"

    fun error(context: Context): String = prefs(context).getString(KEY_ERROR, "") ?: ""

    fun setStatus(context: Context, status: String, error: String = "") {
        val state = prefs(context)
        if (state.getString(KEY_STATUS, "") != status || state.getString(KEY_ERROR, "") != error) {
            state.edit().putString(KEY_STATUS, status).putString(KEY_ERROR, error).apply()
        }
    }
}
