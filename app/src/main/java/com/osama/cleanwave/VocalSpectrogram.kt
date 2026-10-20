package com.osama.cleanwave

import org.jtransforms.fft.FloatFFT_1D
import kotlin.math.PI
import kotlin.math.cos
import kotlin.math.sqrt

/** Periodic Hann / centred STFT matching the released KUIELab vocal model. */
internal class VocalSpectrogram {
    companion object {
        const val FFT = 6144
        const val HOP = 1024
        const val FRAMES = 32
        const val BINS = 2048
        const val WINDOW = (FRAMES - 1) * HOP
        const val TRIM = FFT / 2
        const val CORE = WINDOW - 2 * TRIM
        const val OVERLAP = 2048
        const val STEP = CORE - OVERLAP
        const val MODEL_ELEMENTS = 4 * BINS * FRAMES
    }

    private val fft = FloatFFT_1D(FFT.toLong())
    private val hann = FloatArray(FFT) { (.5 - .5 * cos(2 * PI * it / FFT)).toFloat() }
    private val spectrum = FloatArray(FFT * 2)
    private val originals = FloatArray(4 * (FFT / 2 + 1) * FRAMES)
    private val denominator = FloatArray(WINDOW)
    private val sums = FloatArray(WINDOW * 2)

    init {
        for (frame in 0 until FRAMES) {
            val start = frame * HOP - TRIM
            for (i in 0 until FFT) {
                val at = start + i
                if (at in denominator.indices) denominator[at] += hann[i] * hann[i]
            }
        }
    }

    fun analyze(stereo: FloatArray, modelInput: FloatArray) {
        require(stereo.size == WINDOW * 2 && modelInput.size == MODEL_ELEMENTS)
        for (channel in 0..1) for (frame in 0 until FRAMES) {
            spectrum.fill(0f)
            val start = frame * HOP - TRIM
            for (i in 0 until FFT) {
                var at = start + i
                if (at < 0) at = -at
                if (at >= WINDOW) at = 2 * WINDOW - 2 - at
                spectrum[i] = stereo[at * 2 + channel] * hann[i]
            }
            fft.realForwardFull(spectrum)
            for (bin in 0..FFT / 2) {
                originals[((channel * 2) * (FFT / 2 + 1) + bin) * FRAMES + frame] = spectrum[bin * 2]
                originals[((channel * 2 + 1) * (FFT / 2 + 1) + bin) * FRAMES + frame] = spectrum[bin * 2 + 1]
                if (bin < BINS) {
                    modelInput[(channel * 2 * BINS + bin) * FRAMES + frame] = if (bin < 3) 0f else spectrum[bin * 2]
                    modelInput[((channel * 2 + 1) * BINS + bin) * FRAMES + frame] = if (bin < 3) 0f else spectrum[bin * 2 + 1]
                }
            }
        }
    }

    fun synthesize(predicted: FloatArray, result: FloatArray) {
        sums.fill(0f)
        for (channel in 0..1) for (frame in 0 until FRAMES) {
            spectrum.fill(0f)
            var dryHigh = 0.0; var wetHigh = 0.0
            for (bin in BINS - 256 until BINS) {
                val re = originals[(channel * 2 * (FFT / 2 + 1) + bin) * FRAMES + frame]
                val im = originals[((channel * 2 + 1) * (FFT / 2 + 1) + bin) * FRAMES + frame]
                val wr = predicted[(channel * 2 * BINS + bin) * FRAMES + frame]
                val wi = predicted[((channel * 2 + 1) * BINS + bin) * FRAMES + frame]
                dryHigh += re * re + im * im; wetHigh += wr * wr + wi * wi
            }
            // The model stops at 14.7 kHz. Extend its upper-band attenuation to
            // the original high frequencies instead of discarding them outright.
            val highGain = sqrt(wetHigh / (dryHigh + 1e-12)).coerceIn(0.0, 1.0).toFloat()
            for (bin in 0..FFT / 2) {
                val re: Float; val im: Float
                if (bin < BINS) {
                    re = predicted[(channel * 2 * BINS + bin) * FRAMES + frame]
                    im = predicted[((channel * 2 + 1) * BINS + bin) * FRAMES + frame]
                } else {
                    re = originals[(channel * 2 * (FFT / 2 + 1) + bin) * FRAMES + frame] * highGain
                    im = originals[((channel * 2 + 1) * (FFT / 2 + 1) + bin) * FRAMES + frame] * highGain
                }
                check(re.isFinite() && im.isFinite()) { "Vocal model returned non-finite spectrum" }
                spectrum[bin * 2] = re; spectrum[bin * 2 + 1] = if (bin == 0 || bin == FFT / 2) 0f else im
                if (bin in 1 until FFT / 2) {
                    spectrum[(FFT - bin) * 2] = re
                    spectrum[(FFT - bin) * 2 + 1] = -im
                }
            }
            fft.complexInverse(spectrum, true)
            val start = frame * HOP - TRIM
            for (i in 0 until FFT) {
                val at = start + i
                if (at in denominator.indices) sums[at * 2 + channel] += spectrum[i * 2] * hann[i]
            }
        }
        for (i in result.indices) result[i] = sums[i] / denominator[i / 2].coerceAtLeast(1e-8f)
    }
}
