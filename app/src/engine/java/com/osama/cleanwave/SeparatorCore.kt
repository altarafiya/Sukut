package com.osama.cleanwave

/**
 * Research-only frequency-domain wrapper around [SeparationModel].
 *
 * The surrounding native Audio engine uses 769-bin complex spectra (1536-point real FFT)
 * and feeds bins 0..511 to the neural graph as interleaved real/imaginary Q15-normalized
 * floats. The graph's first stereo head is voice and the second stereo head is music.
 * Audio derives the third "other" source as the original spectrum minus those two heads.
 *
 * Windowing, FFT scaling, overlap/add and external-rate resampling live outside this class.
 */
internal class SeparatorCore(private val model: SeparationModel) {
    companion object {
        const val FFT_SIZE = 1536
        const val SPECTRUM_BINS = FFT_SIZE / 2 + 1 // 769
        const val MODEL_BINS = 512
        const val COMPLEX_VALUES = SPECTRUM_BINS * 2
        const val MODEL_COMPLEX_VALUES = MODEL_BINS * 2
        const val Q12_ONE = 4096

        /** Matches DNN_Exe's int -> float conversion for bins 0..511. */
        fun packModelInput(spectrum: IntArray): FloatArray {
            require(spectrum.size == COMPLEX_VALUES)
            return FloatArray(MODEL_COMPLEX_VALUES) { i -> spectrum[i] / 32767.0f }
        }

        /** Matches DNN_Exe's float -> int conversion: (x * 32767 + 0.5) then C int cast. */
        fun modelFloatToQ15(value: Float): Int = (value * 32767.0f + 0.5f).toInt()

        /**
         * Reconstruct one full 769-bin source remix from original + voice + music spectra.
         * voice/music contain the neural bins only; bins 512..768 are therefore zero for those
         * sources and pass through the residual "other" path.
         */
        fun remix(
            original: IntArray,
            voiceModel: FloatArray,
            musicModel: FloatArray,
            reqVoice: Int,
            reqMusic: Int,
            reqOther: Int,
            destination: IntArray,
        ) {
            require(original.size == COMPLEX_VALUES)
            require(voiceModel.size == MODEL_COMPLEX_VALUES)
            require(musicModel.size == MODEL_COMPLEX_VALUES)
            require(destination.size == COMPLEX_VALUES)

            for (i in destination.indices) {
                val voice = if (i < MODEL_COMPLEX_VALUES) modelFloatToQ15(voiceModel[i]) else 0
                val music = if (i < MODEL_COMPLEX_VALUES) modelFloatToQ15(musicModel[i]) else 0
                val other = original[i].toLong() - voice - music
                val mixed = voice.toLong() * reqVoice +
                    music.toLong() * reqMusic +
                    other * reqOther
                destination[i] = ((mixed + 0x800L) shr 12).toInt()
            }
        }
    }

    private val leftInput = FloatArray(MODEL_COMPLEX_VALUES)
    private val rightInput = FloatArray(MODEL_COMPLEX_VALUES)
    private val voiceLeft = FloatArray(MODEL_COMPLEX_VALUES)
    private val voiceRight = FloatArray(MODEL_COMPLEX_VALUES)
    private val musicLeft = FloatArray(MODEL_COMPLEX_VALUES)
    private val musicRight = FloatArray(MODEL_COMPLEX_VALUES)

    fun reset() = model.reset()

    fun process(
        leftOriginal: IntArray,
        rightOriginal: IntArray,
        reqVoice: Int,
        reqMusic: Int,
        reqOther: Int,
        leftOutput: IntArray,
        rightOutput: IntArray,
    ) {
        require(leftOriginal.size == COMPLEX_VALUES && rightOriginal.size == COMPLEX_VALUES)
        require(leftOutput.size == COMPLEX_VALUES && rightOutput.size == COMPLEX_VALUES)

        for (i in 0 until MODEL_COMPLEX_VALUES) {
            leftInput[i] = leftOriginal[i] / 32767.0f
            rightInput[i] = rightOriginal[i] / 32767.0f
        }

        model.processInto(leftInput, rightInput, voiceLeft, voiceRight, musicLeft, musicRight)
        remix(leftOriginal, voiceLeft, musicLeft, reqVoice, reqMusic, reqOther, leftOutput)
        remix(rightOriginal, voiceRight, musicRight, reqVoice, reqMusic, reqOther, rightOutput)
    }
}
