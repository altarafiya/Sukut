package com.osama.cleanwave

import kotlin.math.ceil

internal enum class CleanerWork { BYPASS, RUN, FALLBACK_FADE, RECOVERY_PROBE }

internal class CleanerMetrics(
    private val engine: String,
    val budgetMs: Double,
    private val enforceBudget: Boolean = true,
    recoveryCooldownMs: Double = 250.0,
    private val requiredRecoveryProbes: Int = 2,
    private val fallbackOnBacklog: Boolean = true,
    maxRecoveryCooldownMs: Double = recoveryCooldownMs,
    detailedTimings: Boolean = true,
) {
    private val histogram = if (detailedTimings) LongArray(401) else null
    private val cooldownPackets = maxOf(1, ceil(recoveryCooldownMs / budgetMs).toInt())
    private val maxCooldownPackets = maxOf(cooldownPackets, ceil(maxRecoveryCooldownMs / budgetMs).toInt())
    private var failedProbes = 0
    @Volatile var overloadCreditMs = 0.0
    private var debtMs = 0.0
    private var packets = 0L
    private var count = 0L
    private var total = 0L
    private var maximum = 0L
    private var missed = 0
    private var cooldownRemaining = 0
    private var healthyProbes = 0
    private var fallbackEvents = 0L
    private var recoveries = 0L
    private var recoveryProbeFailures = 0L
    @Volatile var paused = false
        private set

    @Synchronized fun nextWork(backlog: Int): CleanerWork {
        if (!enforceBudget) return CleanerWork.RUN
        if (!paused) {
            if (fallbackOnBacklog && backlog >= 2) {
                enterFallback()
                // Run one final aligned AI packet so the mixer can fade wet -> dry.
                return CleanerWork.FALLBACK_FADE
            }
            return CleanerWork.RUN
        }
        if (cooldownRemaining > 0) {
            cooldownRemaining--
            return CleanerWork.BYPASS
        }
        // Cheap bypass should drain the capture queue before probing the model.
        if (fallbackOnBacklog && backlog > 0) {
            return CleanerWork.BYPASS
        }
        return CleanerWork.RECOVERY_PROBE
    }

    /** Returns true when recurrent model state should be reset before the next AI run. */
    @Synchronized fun add(ns: Long, backlog: Int, work: CleanerWork): Boolean {
        packets++
        if (work == CleanerWork.BYPASS) return false
        count++; total += ns; maximum = maxOf(maximum, ns)
        histogram?.let { it[(ns / 5_000_000).toInt().coerceIn(0, 400)]++ }
        if (!enforceBudget) return false

        val overloaded = ns / 1e6 > budgetMs || (fallbackOnBacklog && backlog >= 2)
        if (work == CleanerWork.FALLBACK_FADE) return true
        if (work == CleanerWork.RECOVERY_PROBE) {
            if (overloaded) {
                healthyProbes = 0
                failedProbes = (failedProbes + 1).coerceAtMost(8)
                cooldownRemaining = minOf(maxCooldownPackets, cooldownPackets * (1 shl failedProbes))
                recoveryProbeFailures++
                return true
            } else if (++healthyProbes >= requiredRecoveryProbes) {
                paused = false
                missed = 0
                healthyProbes = 0
                recoveries++
                failedProbes = 0
                debtMs = 0.0
            }
            return false
        }

        missed = if (overloaded) missed + 1 else 0
        debtMs = (debtMs + ns / 1e6 - budgetMs).coerceAtLeast(0.0)
        if ((missed >= 2 && debtMs > overloadCreditMs) || (fallbackOnBacklog && backlog >= 2)) {
            enterFallback()
            return true
        }
        return false
    }

    private fun enterFallback() {
        if (!paused) {
            paused = true
            fallbackEvents++
        }
        missed = 0
        healthyProbes = 0
        failedProbes = 0
        cooldownRemaining = cooldownPackets
    }

    @Synchronized fun report(strength: Float): String {
        var sum = 0L; var p95 = 0.0
        val bins = histogram
        if (count > 0 && bins != null) for (i in bins.indices) {
            sum += bins[i]
            if (sum >= ceil(count * .95).toLong()) { p95 = (i + 1) * 5.0; break }
        }
        return "engine=$engine; packets=$packets; ai_runs=$count; avg_ms=${if (count == 0L) 0.0 else total / count / 1e6}; " +
            "p95_upper_ms=${if (bins == null) "not_sampled" else p95}; max_ms=${maximum / 1e6}; budget_ms=$budgetMs; ai_paused=$paused; " +
            "fallback_events=$fallbackEvents; recoveries=$recoveries; recovery_probe_failures=$recoveryProbeFailures; reduction=${strength * 100}"
    }
}
