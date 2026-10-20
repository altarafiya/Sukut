package com.osama.cleanwave

import org.jtransforms.fft.FloatFFT_1D
import java.nio.ByteBuffer

/**
 * Research-only 48 kHz streaming wrapper reconstructed from Audio's streaming separation engine.
 *
 * Frame geometry is exact: a 1536-sample analysis frame advances by 960 samples. The first
 * 576 samples use Audio's rising Q15 window and the following 960 use its flat/falling Q15
 * window. Synthesis overlap/add mirrors the native engine: 576 overlapped samples + 384 direct
 * samples are emitted, while the final 576 inverse-FFT samples become the next synthesis tail.
 *
 * We intentionally use JTransforms for the FFT. Audio's fixed-point implementation is an
 * unnormalised forward DFT with a normalised inverse. Operating directly on normalised float
 * PCM preserves the neural feature scale produced by Audio's Q15 PCM -> FFT -> /32767 path,
 * up to fixed-point rounding.
 */
internal class StreamingDsp(
    modelBuffer: ByteBuffer,
    riseWindowBytes: ByteArray,
    hopWindowBytes: ByteArray,
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
            require(bytes.size == expected * 2) { "Unexpected Audio window size" }
            return FloatArray(expected) { i ->
                val lo = bytes[i * 2].toInt() and 0xff
                val hi = bytes[i * 2 + 1].toInt()
                ((lo or (hi shl 8)).toShort().toInt()) / Q15
            }
        }
    }

    private val rise = decodeWindow(riseWindowBytes, OVERLAP_FRAMES)
    private val hopWindow = decodeWindow(hopWindowBytes, HOP_FRAMES)
    private val model = SeparationModel(modelBuffer)
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

    init {
        // The rising and falling windows are a sqrt-complementary pair. Audio applies them
        // in both analysis and synthesis, so their squared sum should remain full scale.
        val target = (32767L * 32767L).toDouble()
        var worst = 0.0
        for (i in 0 until OVERLAP_FRAMES) {
            val a = (rise[i] * Q15).toLong()
            val b = (hopWindow[DIRECT_FRAMES + i] * Q15).toLong()
            worst = maxOf(worst, kotlin.math.abs(a * a + b * b - target))
        }
        require(worst / target < 0.0002) { "Audio windows are not complementary" }
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

        if (!runModel) {
            dryOutput.copyInto(wetOutput)
            return
        }

        analyzeChannel(0, leftSpectrum, modelLeft)
        analyzeChannel(1, rightSpectrum, modelRight)
        model.processInto(modelLeft, modelRight, voiceLeft, voiceRight, musicLeft, musicRight)

        buildWetSpectrum(leftSpectrum, voiceLeft, musicLeft, voiceQ12, musicQ12, otherQ12, wetLeftSpectrum)
        buildWetSpectrum(rightSpectrum, voiceRight, musicRight, voiceQ12, musicQ12, otherQ12, wetRightSpectrum)

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
        destination: FloatArray,
    ) {
        val gv = voiceQ12 / 4096f
        val gm = musicQ12 / 4096f
        val go = otherQ12 / 4096f
        for (bin in 0..FFT_SIZE / 2) {
            val at = bin * 2
            val originalRe = original[at]
            val originalIm = original[at + 1]
            val voiceRe = if (bin < SeparatorCore.MODEL_BINS) voice[at] * (32767f / 32768f) else 0f
            val voiceIm = if (bin < SeparatorCore.MODEL_BINS) voice[at + 1] * (32767f / 32768f) else 0f
            val musicRe = if (bin < SeparatorCore.MODEL_BINS) music[at] * (32767f / 32768f) else 0f
            val musicIm = if (bin < SeparatorCore.MODEL_BINS) music[at + 1] * (32767f / 32768f) else 0f
            val re = voiceRe * gv + musicRe * gm + (originalRe - voiceRe - musicRe) * go
            val im = voiceIm * gv + musicIm * gm + (originalIm - voiceIm - musicIm) * go
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
        model.reset()
        synthesisTail.fill(0f)
    }

    fun resetAll() {
        history.fill(0f)
        resetNeuralState()
    }

    override fun close() = model.close()
}
