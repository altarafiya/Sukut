package com.osama.cleanwave

/** Shared preset mapping; renderer choice does not change AI separation quality. */
internal object EnginePerformanceConfig {
    const val LOWEST_LATENCY = 0
    const val BALANCED = 1
    const val HIGHER_QUALITY = 2
    const val CUSTOM = 3
    data class Options(val effects: Boolean, val buffering: Int)
    val presets = listOf(Options(false, 0), Options(false, 1), Options(true, 1))
    fun mode(effects: Boolean, buffering: Int, custom: Boolean = false): Int =
        if (custom) CUSTOM else presets.indexOf(Options(effects, buffering)).takeIf { it >= 0 } ?: CUSTOM
    fun background(db: Int) = db.coerceIn(-20, 10)
}
