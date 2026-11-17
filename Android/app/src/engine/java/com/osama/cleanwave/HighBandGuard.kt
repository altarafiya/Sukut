package com.osama.cleanwave

import kotlin.math.max

/**
 * The separator only predicts bins below 16 kHz (512 of 769). Above that, the
 * original spectrum passes through the "other" path unchanged, so cymbal and
 * hi-hat air leaked straight through when music was muted.
 *
 * While music is muted, scale the >16 kHz band by the voice share of the
 * neighbouring 12-16 kHz band. Speech sibilance keeps its air; percussion air
 * follows the music suppression. Fast attack, slower release and a -30 dB
 * floor avoid audible pumping. Below 16 kHz nothing changes.
 */
internal class HighBandGuard {
    companion object {
        const val REFERENCE_START = 384 // 12 kHz at 31.25 Hz per bin
        const val REFERENCE_END = SeparatorCore.MODEL_BINS // 16 kHz
        const val FLOOR = 0.0316f // -30 dB
        const val ATTACK = 0.6f
        const val RELEASE = 0.15f
        /** Heads explaining less than 1% of the band carry no reliable evidence. */
        private const val EVIDENCE = 0.01
    }

    var gain = 1f
        private set

    fun reset() { gain = 1f }

    fun update(
        originalLeft: FloatArray,
        originalRight: FloatArray,
        voiceLeft: FloatArray,
        voiceRight: FloatArray,
        musicLeft: FloatArray,
        musicRight: FloatArray,
        enabled: Boolean,
    ): Float {
        if (!enabled) { gain = 1f; return gain }
        var voice = 0.0
        var music = 0.0
        var total = 0.0
        for (bin in REFERENCE_START until REFERENCE_END) {
            val re = bin * 2
            val im = re + 1
            voice += power(voiceLeft, re, im) + power(voiceRight, re, im)
            music += power(musicLeft, re, im) + power(musicRight, re, im)
            total += power(originalLeft, re, im) + power(originalRight, re, im)
        }
        val heads = voice + music
        if (heads <= 0.0 || heads < EVIDENCE * total) return gain
        val target = max(FLOOR, (voice / heads).toFloat())
        gain += (target - gain) * if (target < gain) ATTACK else RELEASE
        return gain
    }

    private fun power(values: FloatArray, re: Int, im: Int): Double {
        val a = values[re].toDouble()
        val b = values[im].toDouble()
        return a * a + b * b
    }
}
