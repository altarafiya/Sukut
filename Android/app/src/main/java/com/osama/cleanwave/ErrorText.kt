package com.osama.cleanwave

import android.content.Context

/**
 * Turns the engine's technical failure text ("Audio route stopped: …
 * IllegalStateException: AI capture queue saturated") into a short sentence a
 * person can act on. The technical text stays in the logs.
 */
internal object ErrorText {
    fun friendly(context: Context, raw: String): String {
        val text = raw.lowercase()
        val id = when {
            text.isBlank() -> R.string.error_generic
            "saturat" in text || "overload" in text || "queue" in text || "underrun" in text || "too slow" in text -> R.string.error_overload
            "worker" in text || "deadobject" in text || "binder" in text || "transaction" in text || "not running" in text -> R.string.error_worker
            "foreground" in text || "not allowed" in text -> R.string.error_background
            "permission" in text || "security" in text || "denied" in text -> R.string.error_permission
            "routing" in text || "nosuchmethod" in text || "audiorecord" in text || "audiotrack" in text || "capture" in text -> R.string.error_route
            else -> R.string.error_generic
        }
        return context.getString(id)
    }
}
