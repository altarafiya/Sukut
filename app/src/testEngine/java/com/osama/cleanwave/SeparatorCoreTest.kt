package com.osama.cleanwave

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class SeparatorCoreTest {
    @Test
    fun packsOnlyNeuralBandInInterleavedComplexOrder() {
        val spectrum = IntArray(SeparatorCore.COMPLEX_VALUES) { it - 500 }
        val packed = SeparatorCore.packModelInput(spectrum)

        assertEquals(SeparatorCore.MODEL_COMPLEX_VALUES, packed.size)
        assertEquals(spectrum[0] / 32767f, packed[0], 0f)
        assertEquals(spectrum[1] / 32767f, packed[1], 0f)
        assertEquals(spectrum[1023] / 32767f, packed[1023], 0f)
    }

    @Test
    fun upperBinsTravelThroughOtherResidual() {
        val original = IntArray(SeparatorCore.COMPLEX_VALUES)
        val voice = FloatArray(SeparatorCore.MODEL_COMPLEX_VALUES)
        val music = FloatArray(SeparatorCore.MODEL_COMPLEX_VALUES)
        val output = IntArray(original.size)
        val upper = SeparatorCore.MODEL_COMPLEX_VALUES + 20
        original[upper] = 12345

        SeparatorCore.remix(
            original, voice, music,
            reqVoice = 0,
            reqMusic = 0,
            reqOther = SeparatorCore.Q12_ONE,
            destination = output,
        )

        assertEquals(12345, output[upper])
    }

    @Test
    fun sourceGainsUseQ12ResidualMixing() {
        val original = IntArray(SeparatorCore.COMPLEX_VALUES)
        val voice = FloatArray(SeparatorCore.MODEL_COMPLEX_VALUES)
        val music = FloatArray(SeparatorCore.MODEL_COMPLEX_VALUES)
        val output = IntArray(original.size)
        original[0] = 10000
        voice[0] = 2000f / 32767f
        music[0] = 3000f / 32767f

        SeparatorCore.remix(
            original, voice, music,
            reqVoice = SeparatorCore.Q12_ONE,
            reqMusic = SeparatorCore.Q12_ONE,
            reqOther = 0,
            destination = output,
        )

        // Float->Q15 uses Audio's +0.5/truncate conversion, so tolerate one LSB.
        assertTrue(output[0] in 4999..5001)
    }
}
