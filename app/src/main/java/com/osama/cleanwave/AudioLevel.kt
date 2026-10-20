package com.osama.cleanwave

import kotlin.math.abs
import kotlin.math.pow
import kotlin.math.sqrt
import kotlin.math.tanh

/** Voice compensation affects the processed branch only; 0% preserves exact original PCM. */
internal class AudioLevel {
    @Volatile var reduction = 1f
    @Volatile var boostDb = 0
        set(value) {
            field = value
            boostGain = 10.0.pow(value / 20.0).toFloat()
        }
    @Volatile private var boostGain = 1f
    private var amount = 1f
    private var gain = 1f

    fun render(dry: FloatArray, voice: FloatArray, output: ByteArray, paused: Boolean) {
        require(dry.size == voice.size && output.size == dry.size * 2)
        var dryPower = 0.0
        var voicePower = 0.0
        for (i in dry.indices) {
            check(voice[i].isFinite()) { "Model returned a non-finite sample" }
            dryPower += dry[i] * dry[i]
            voicePower += voice[i] * voice[i]
        }
        val ratio = sqrt(voicePower / (dryPower + 1e-20))
        val compensation = if (ratio >= .32 && voicePower / voice.size > 1e-7)
            (1.0 / ratio).coerceIn(1.0, 1.5).toFloat() else 1f
        val nextGain = gain + .15f * (compensation - gain)
        val extra = boostGain
        val frames = dry.size / 2
        val target = if (paused) 0f else reduction
        for (frame in 0 until frames) {
            amount += (target - amount).coerceIn(-.004f, .004f)
            val level = (gain + (nextGain - gain) * frame / frames) * extra
            for (channel in 0..1) {
                val i = frame * 2 + channel
                var sample = dry[i] * (1f - amount) + voice[i] * level * amount
                // Linear for normal levels. Smooth compression only near clipping.
                if (abs(sample) > .9f && amount > 0f) {
                    val sign = if (sample < 0) -1 else 1
                    sample = sign * (.9f + .099f * tanh((abs(sample) - .9f) / .099f))
                }
                val pcm = (sample.coerceIn(-1f, 32767f / 32768f) * 32768f).toInt()
                output[i * 2] = pcm.toByte()
                output[i * 2 + 1] = (pcm shr 8).toByte()
            }
        }
        gain = nextGain
    }

    fun resetAmount() { amount = reduction }
}

internal fun decodeStereo(pcm: ByteArray, destination: FloatArray, offset: Int = 0) {
    for (i in 0 until pcm.size / 2) {
        val value = (pcm[i * 2].toInt() and 255) or (pcm[i * 2 + 1].toInt() shl 8)
        destination[offset + i] = value / 32768f
    }
}
