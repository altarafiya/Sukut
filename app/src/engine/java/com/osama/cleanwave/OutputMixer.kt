package com.osama.cleanwave

import kotlin.math.abs
import kotlin.math.pow

/** Current-packet peak protection: no lookahead, new buffer, RMS makeup or soft clipping. */
internal class OutputMixer {
    @Volatile var reduction = 1f
    @Volatile var silentRecovery = true
    @Volatile private var amplification = 1f
    private var wetAmount = 1f
    private var gain = 1f
    private var wasPaused = false

    fun setAmplification(db: Int) { amplification = 10.0.pow(db.coerceIn(0, 12) / 20.0).toFloat() }

    fun render(dry: FloatArray, wet: FloatArray, output: ByteArray, paused: Boolean, modelAvailable: Boolean) {
        val silent = silentRecovery
        if (paused && silent && (wasPaused || !modelAvailable)) {
            output.fill(0)
            wetAmount = 0f
            wasPaused = true
            return
        }
        // Fade the final valid processed packet to silence on recovery entry.
        // Never fade or copy the original branch into a filtered mode.
        wasPaused = paused
        // Use the already allocated wet packet as scratch after DSP synthesis.
        // Filter/Strict never blend unfiltered audio into recovery.
        val target = if (paused) 0f else reduction
        var peak = 0f
        for (frame in 0 until wet.size / 2) {
            wetAmount += (target - wetAmount).coerceIn(-.004f, .004f)
            for (channel in 0..1) {
                val at = frame * 2 + channel
                val value = if (silent) wet[at] * wetAmount
                    else wet[at] * wetAmount + dry[at] * (1f - wetAmount)
                check(value.isFinite()) { "Model returned non-finite audio" }
                wet[at] = value
                peak = maxOf(peak, abs(value))
            }
        }
        val wanted = minOf(amplification, if (peak > 0f) .999f / peak else amplification)
        // Immediate attenuation protects this packet; release is gradual.
        val startGain = minOf(gain, wanted)
        val endGain = if (wanted < gain) wanted else gain + .08f * (wanted - gain)
        for (i in wet.indices) {
            val scale = startGain + (endGain - startGain) * i / wet.size
            val pcm = (wet[i] * scale).coerceIn(-1f, 32767f / 32768f).times(32768f).toInt()
            output[i * 2] = pcm.toByte()
            output[i * 2 + 1] = (pcm shr 8).toByte()
        }
        gain = endGain
    }
}
