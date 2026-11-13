package com.osama.cleanwave

import java.security.MessageDigest

/** Evaluated only when the user submits the hidden dialog; no running timer. */
internal object AdvancedMixPolicy {
    const val HOLD_MS = 5_000L
    const val DISABLE_HOLD_MS = 3_000L
    fun holdMillis(enabled: Boolean) = if(enabled) DISABLE_HOLD_MS else HOLD_MS
    const val LOCK_MS = 600_000L
    data class State(val failures: Int = 0, val lockedUntil: Long = 0)
    enum class Result { ENABLED, INCORRECT, LOCKED }
    data class Attempt(val state: State, val result: Result)
    // Only a salted SHA-256 of the access code is in the source.
    private const val CODE_SHA256 = "1eab90bc571f33625f026854d109a8a8331d688bdddd31c1917f7631e49c6d60"
    fun isAccessCode(code: String): Boolean =
        MessageDigest.getInstance("SHA-256").digest("sukut-advanced-mix:$code".toByteArray(Charsets.UTF_8))
            .joinToString("") { "%02x".format(it) } == CODE_SHA256
    fun attempt(state: State, code: String, now: Long, matches: (String) -> Boolean = ::isAccessCode): Attempt {
        if (state.lockedUntil > now) return Attempt(state, Result.LOCKED)
        val failures = if (state.lockedUntil != 0L) 0 else state.failures.coerceIn(0, 2)
        if (matches(code)) return Attempt(State(), Result.ENABLED)
        return if (failures == 2) Attempt(State(0, now + LOCK_MS), Result.LOCKED)
            else Attempt(State(failures + 1), Result.INCORRECT)
    }
}
