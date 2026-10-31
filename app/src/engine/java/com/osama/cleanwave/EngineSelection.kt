package com.osama.cleanwave

import java.nio.ByteBuffer
import kotlin.math.abs
import kotlin.math.max

/**
 * Chooses the CPU kernel path for the separator on this device.
 *
 * The built-in kernels are the reference. XNNPACK is used only when, on this
 * phone, it reproduces the reference heads and recurrent state within
 * [MAX_RELATIVE_ERROR] across a recurrent run AND is clearly faster. Any
 * delegate failure, mismatch or lack of gain keeps the reference path.
 */
internal object EngineSelection {
    /** -60 dB relative to the head peak: far below audibility, catches real divergence. */
    const val MAX_RELATIVE_ERROR = 1e-3f
    /** XNNPACK must save at least 15% inference time to be worth switching. */
    const val REQUIRED_SPEEDUP = 0.85
    private const val WARMUP_FRAMES = 8
    private const val MEASURED_FRAMES = 40

    class Choice(
        val model: SeparationModel,
        val kernels: String,
        val referenceMs: Double,
        val selectedMs: Double,
        val worstError: Float,
    )

    /** Outcome of this process's first comparison; the model, build and CPU cannot change underneath it. */
    private class Decision(val xnnpack: Boolean, val kernels: String, val referenceMs: Double, val selectedMs: Double, val worstError: Float)
    @Volatile private var decided: Decision? = null

    fun choose(modelBuffer: ByteBuffer, tryFast: Boolean): Choice {
        if (tryFast) decided?.let { return reuse(modelBuffer, it) }
        val reference = SeparationModel(modelBuffer)
        if (!tryFast) return Choice(reference, "builtin", 0.0, 0.0, 0f)
        val fast = try { SeparationModel(modelBuffer, xnnpack = true) }
        catch (_: Throwable) { return remember(Choice(reference, "builtin; xnnpack=unavailable", 0.0, 0.0, 0f)) }
        return try {
            remember(compare(reference, fast))
        } catch (_: Throwable) {
            runCatching { fast.close() }
            reference.reset()
            remember(Choice(reference, "builtin; xnnpack=failed", 0.0, 0.0, 0f))
        }
    }

    // The audio worker lives across Off/Start. Later Starts build only the chosen
    // interpreter instead of two, and skip the 48-frame comparison: a faster Start,
    // a lower memory peak and less CPU. Arithmetic is unchanged.
    private fun remember(choice: Choice): Choice {
        decided = Decision(choice.model.xnnpack, choice.kernels, choice.referenceMs, choice.selectedMs, choice.worstError)
        return choice
    }

    private fun reuse(modelBuffer: ByteBuffer, decision: Decision): Choice {
        if (decision.xnnpack) {
            runCatching { SeparationModel(modelBuffer, xnnpack = true) }.getOrNull()?.let {
                return Choice(it, decision.kernels + "; reused", decision.referenceMs, decision.selectedMs, decision.worstError)
            }
            decided = null // The delegate stopped loading; fall back and decide again next time.
            return Choice(SeparationModel(modelBuffer), "builtin; xnnpack=unavailable", 0.0, 0.0, 0f)
        }
        return Choice(SeparationModel(modelBuffer), decision.kernels + "; reused", decision.referenceMs, decision.selectedMs, decision.worstError)
    }

    private fun compare(reference: SeparationModel, fast: SeparationModel): Choice {
        val left = FloatArray(SeparationModel.SPECTRAL_FLOATS)
        val right = FloatArray(SeparationModel.SPECTRAL_FLOATS)
        val a = Array(4) { FloatArray(SeparationModel.SPECTRAL_FLOATS) }
        val b = Array(4) { FloatArray(SeparationModel.SPECTRAL_FLOATS) }
        var seed = 0x53554b55L
        fun next(): Float {
            seed = seed * 6364136223846793005L + 1442695040888963407L
            return ((seed ushr 40).toInt() / 8388608f) - 1f
        }
        var referenceNs = 0L
        var fastNs = 0L
        var worst = 0f
        repeat(WARMUP_FRAMES + MEASURED_FRAMES) { frame ->
            // Quiet speech up to loud full-scale spectra, with exact-zero frames.
            val scale = when ((frame / 6) % 4) { 0 -> .01f; 1 -> 1f; 2 -> 30f; else -> 200f }
            for (i in left.indices) {
                left[i] = if (frame % 11 == 0) 0f else next() * scale
                right[i] = if (frame % 13 == 0) 0f else next() * scale
            }
            var start = System.nanoTime()
            reference.processInto(left, right, a[0], a[1], a[2], a[3])
            val r = System.nanoTime() - start
            start = System.nanoTime()
            fast.processInto(left, right, b[0], b[1], b[2], b[3])
            val f = System.nanoTime() - start
            if (frame >= WARMUP_FRAMES) { referenceNs += r; fastNs += f }
            for (head in a.indices) worst = max(worst, relativeError(a[head], b[head]))
        }
        worst = max(worst, relativeError(reference.stateSnapshot(), fast.stateSnapshot()))
        val referenceMs = referenceNs / 1e6 / MEASURED_FRAMES
        val fastMs = fastNs / 1e6 / MEASURED_FRAMES
        val accept = worst <= MAX_RELATIVE_ERROR && fastMs < referenceMs * REQUIRED_SPEEDUP
        return if (accept) {
            reference.close(); fast.reset()
            Choice(fast, "xnnpack", referenceMs, fastMs, worst)
        } else {
            fast.close(); reference.reset()
            Choice(reference, "builtin; xnnpack=rejected", referenceMs, referenceMs, worst)
        }
    }

    internal fun relativeError(expected: FloatArray, actual: FloatArray): Float {
        var peak = 0f
        var difference = 0f
        for (i in expected.indices) {
            peak = max(peak, abs(expected[i]))
            difference = max(difference, abs(expected[i] - actual[i]))
        }
        if (!difference.isFinite()) return Float.POSITIVE_INFINITY
        return if (peak < 1e-6f) difference else difference / peak
    }
}
