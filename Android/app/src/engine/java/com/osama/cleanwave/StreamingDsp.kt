package com.osama.cleanwave

import org.jtransforms.fft.FloatFFT_1D
import java.nio.ByteBuffer

/**
 * Sukut's 48 kHz streaming separation DSP around the separation model.
 *
 * Frame geometry: a 1536-sample analysis frame advances by 960 samples. The first 576
 * samples use the rising Q15 window and the following 960 use the flat/falling Q15 window.
 * Synthesis overlap/add emits 576 overlapped samples + 384 direct samples, while the final
 * 576 inverse-FFT samples become the next synthesis tail.
 *
 * JTransforms provides the FFT on normalised float PCM, which gives the model the feature
 * scale it expects (Q15 PCM -> FFT -> /32767), up to fixed-point rounding.
 */
internal class StreamingDsp(
    modelBuffer: ByteBuffer,
    riseWindowBytes: ByteArray,
    hopWindowBytes: ByteArray,
    tryFastKernels: Boolean = false,
) : AutoCloseable {
    companion object {
        const val SAMPLE_RATE = 48000
        const val FFT_SIZE = 1536
        const val HOP_FRAMES = 960
        const val OVERLAP_FRAMES = 576
        const val DIRECT_FRAMES = HOP_FRAMES - OVERLAP_FRAMES // 384
        const val FIRST_INPUT_FRAMES = FFT_SIZE
        private const val Q15 = 32768f

        private fun decodeWindow(bytes: ByteArray, expected: Int): FloatArray {
            require(bytes.size == expected * 2) { "Unexpected Sukut window size" }
            return FloatArray(expected) { i ->
                val lo = bytes[i * 2].toInt() and 0xff
                val hi = bytes[i * 2 + 1].toInt()
                ((lo or (hi shl 8)).toShort().toInt()) / Q15
            }
        }
    }

    private val rise = decodeWindow(riseWindowBytes, OVERLAP_FRAMES)
    private val hopWindow = decodeWindow(hopWindowBytes, HOP_FRAMES)
    val kernels = EngineSelection.choose(modelBuffer, tryFastKernels)
    private val model = kernels.model
    private val highBand = HighBandGuard()
    private val fft = FloatFFT_1D(FFT_SIZE.toLong())

    private val history = FloatArray(OVERLAP_FRAMES * 2)
    private val frame = FloatArray(FFT_SIZE * 2)
    private val leftSpectrum = FloatArray(FFT_SIZE * 2)
    private val rightSpectrum = FloatArray(FFT_SIZE * 2)
    private val wetLeftSpectrum = FloatArray(FFT_SIZE * 2)
    private val wetRightSpectrum = FloatArray(FFT_SIZE * 2)
    private val modelLeft = FloatArray(SeparationModel.SPECTRAL_FLOATS)
    private val modelRight = FloatArray(SeparationModel.SPECTRAL_FLOATS)
    private val voiceLeft = FloatArray(SeparationModel.SPECTRAL_FLOATS)
    private val voiceRight = FloatArray(SeparationModel.SPECTRAL_FLOATS)
    private val musicLeft = FloatArray(SeparationModel.SPECTRAL_FLOATS)
    private val musicRight = FloatArray(SeparationModel.SPECTRAL_FLOATS)
    private val synthesisTail = FloatArray(OVERLAP_FRAMES * 2)
    private var previousZeroAnalysis = false
    /** Off by default so reference parity tests keep the original remix. */
    @Volatile var guardHighBand = false
    val highBandGain get() = highBand.gain
    @Volatile var idleDspFrames = 0L
        private set
    @Volatile var idleDspActive = false
        private set

    init {
        // The rising and falling windows are a sqrt-complementary pair. They are applied
        // in both analysis and synthesis, so their squared sum should remain full scale.
        val target = (32767L * 32767L).toDouble()
        var worst = 0.0
        for (i in 0 until OVERLAP_FRAMES) {
            val a = (rise[i] * Q15).toLong()
            val b = (hopWindow[DIRECT_FRAMES + i] * Q15).toLong()
            worst = maxOf(worst, kotlin.math.abs(a * a + b * b - target))
        }
        require(worst / target < 0.0002) { "Sukut windows are not complementary" }
    }

    /**
     * Processes one hop. The first call consumes 1536 stereo frames; later calls consume 960.
     * [dryOutput] and [wetOutput] always receive 960 interleaved stereo frames on the same
     * timeline. When [runModel] is false, only analysis history advances and wet equals dry.
     */
    fun process(
        input: FloatArray,
        first: Boolean,
        runModel: Boolean,
        voiceQ12: Int,
        musicQ12: Int,
        otherQ12: Int,
        dryOutput: FloatArray,
        wetOutput: FloatArray,
        allowIdle: Boolean = false,
    ) {
        val expectedFrames = if (first) FIRST_INPUT_FRAMES else HOP_FRAMES
        require(input.size == expectedFrames * 2)
        require(dryOutput.size == HOP_FRAMES * 2 && wetOutput.size == HOP_FRAMES * 2)

        if (first) {
            input.copyInto(frame)
        } else {
            history.copyInto(frame, 0)
            input.copyInto(frame, history.size)
        }

        // This is the exact timeline emitted by the native overlap/add stage.
        frame.copyInto(dryOutput, 0, 0, HOP_FRAMES * 2)
        frame.copyInto(history, 0, HOP_FRAMES * 2, FFT_SIZE * 2)

        idleDspActive = false
        if (!runModel) {
            previousZeroAnalysis = false
            dryOutput.copyInto(wetOutput)
            return
        }

        // Never gate by loudness: even the quietest nonzero sample must run the
        // original processing path. Keep capture, playback and overlap timing.
        val exactSilence = allowIdle && frame.all { it.toRawBits() == 0 }
        if (exactSilence && previousZeroAnalysis) {
            // These input spectra were produced by the same FFT from an identical
            // zero frame. They are read-only downstream, so reuse their exact bits.
            idleDspActive = true
        } else {
            analyzeChannel(0, leftSpectrum, modelLeft)
            analyzeChannel(1, rightSpectrum, modelRight)
        }
        previousZeroAnalysis = exactSilence
        // Always advance recurrent state, even during silence. Freezing it did
        // not pass the real-model fixed-point check and could change resumption.
        model.processInto(modelLeft, modelRight, voiceLeft, voiceRight, musicLeft, musicRight)

        if (exactSilence && synthesisTail.all { it == 0f } &&
            voiceLeft.all { it == 0f } && voiceRight.all { it == 0f } &&
            musicLeft.all { it == 0f } && musicRight.all { it == 0f }) {
            // No old overlap tail or spectral source can contribute a sample.
            // Preserve the original all-zero output without two inverse FFTs.
            wetOutput.fill(0f); idleDspActive = true; idleDspFrames++
            return
        }
        if (idleDspActive) idleDspFrames++

        // One shared stereo gain keeps the image stable above 16 kHz.
        val highGain = highBand.update(
            leftSpectrum, rightSpectrum, voiceLeft, voiceRight, musicLeft, musicRight,
            enabled = guardHighBand && musicQ12 == 0 && otherQ12 > 0,
        )
        buildWetSpectrum(leftSpectrum, voiceLeft, musicLeft, voiceQ12, musicQ12, otherQ12, highGain, wetLeftSpectrum)
        buildWetSpectrum(rightSpectrum, voiceRight, musicRight, voiceQ12, musicQ12, otherQ12, highGain, wetRightSpectrum)

        fft.complexInverse(wetLeftSpectrum, true)
        fft.complexInverse(wetRightSpectrum, true)
        synthesize(wetLeftSpectrum, wetRightSpectrum, wetOutput)
    }

    private fun analyzeChannel(channel: Int, spectrum: FloatArray, modelInput: FloatArray) {
        for (i in 0 until FFT_SIZE) {
            val window = if (i < OVERLAP_FRAMES) rise[i] else hopWindow[i - OVERLAP_FRAMES]
            spectrum[i] = frame[i * 2 + channel] * window
        }
        fft.realForwardFull(spectrum)
        for (bin in 0 until SeparatorCore.MODEL_BINS) {
            modelInput[bin * 2] = spectrum[bin * 2] * (32768f / 32767f)
            modelInput[bin * 2 + 1] = spectrum[bin * 2 + 1] * (32768f / 32767f)
        }
    }

    private fun buildWetSpectrum(
        original: FloatArray,
        voice: FloatArray,
        music: FloatArray,
        voiceQ12: Int,
        musicQ12: Int,
        otherQ12: Int,
        highGain: Float,
        destination: FloatArray,
    ) {
        val gv = voiceQ12 / 4096f
        val gm = musicQ12 / 4096f
        val go = otherQ12 / 4096f
        val goHigh = go * highGain
        for (bin in 0..FFT_SIZE / 2) {
            val at = bin * 2
            val originalRe = original[at]
            val originalIm = original[at + 1]
            val modelled = bin < SeparatorCore.MODEL_BINS
            val voiceRe = if (modelled) voice[at] * (32767f / 32768f) else 0f
            val voiceIm = if (modelled) voice[at + 1] * (32767f / 32768f) else 0f
            val musicRe = if (modelled) music[at] * (32767f / 32768f) else 0f
            val musicIm = if (modelled) music[at + 1] * (32767f / 32768f) else 0f
            val residual = if (modelled) go else goHigh
            val re = voiceRe * gv + musicRe * gm + (originalRe - voiceRe - musicRe) * residual
            val im = voiceIm * gv + musicIm * gm + (originalIm - voiceIm - musicIm) * residual
            destination[at] = re
            destination[at + 1] = if (bin == 0 || bin == FFT_SIZE / 2) 0f else im
            if (bin in 1 until FFT_SIZE / 2) {
                val mirror = (FFT_SIZE - bin) * 2
                destination[mirror] = re
                destination[mirror + 1] = -im
            }
        }
    }

    private fun synthesize(left: FloatArray, right: FloatArray, output: FloatArray) {
        for (i in 0 until OVERLAP_FRAMES) {
            val falling = hopWindow[DIRECT_FRAMES + i]
            val rising = rise[i]
            output[i * 2] = synthesisTail[i * 2] * falling + left[i * 2] * rising
            output[i * 2 + 1] = synthesisTail[i * 2 + 1] * falling + right[i * 2] * rising
        }
        for (i in OVERLAP_FRAMES until HOP_FRAMES) {
            val w = hopWindow[i - OVERLAP_FRAMES]
            output[i * 2] = left[i * 2] * w
            output[i * 2 + 1] = right[i * 2] * w
        }
        for (i in 0 until OVERLAP_FRAMES) {
            val sourceFrame = HOP_FRAMES + i
            synthesisTail[i * 2] = left[sourceFrame * 2]
            synthesisTail[i * 2 + 1] = right[sourceFrame * 2]
        }
    }

    /** Reset only state that becomes invalid when AI frames are skipped. */
    fun resetNeuralState() {
        previousZeroAnalysis = false; idleDspActive = false
        model.reset()
        highBand.reset()
        synthesisTail.fill(0f)
    }

    fun resetAll() {
        history.fill(0f)
        resetNeuralState()
    }

    override fun close() = model.close()
}
