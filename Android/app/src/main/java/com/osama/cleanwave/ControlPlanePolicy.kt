package com.osama.cleanwave

/** No effect on inference, audio queues or sample timing. */
internal object ControlPlanePolicy {
    fun healthInterval(visible: Boolean): Long = if (visible) 1000L else 5000L

    // Status/detail updates don't change anything displayed on the Quick Settings tile.
    fun affectsTile(key: String?) = key in tileKeys
    private val tileKeys = setOf(
        "enabled", "status", "audio_source_preset", "pending_start_pid",
        "pending_start_time", "cancel_start", "ui_language", "language",
    )
    fun affectsHome(key: String?) = key in homeKeys
    private val homeKeys = tileKeys + setOf(
        "error", "recovery_until", "overload_tip", "optimized_background_db",
        "advanced_mix_enabled", "advanced_voice_db", "advanced_music_db", "advanced_background_db",
        "buffering", "effects_compatibility", "engine_performance_custom", "bluetooth_output_key",
    )
}
