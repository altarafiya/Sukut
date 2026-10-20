package com.osama.cleanwave

import org.junit.Assert.*
import org.junit.Test
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder
import kotlin.math.sin
import kotlin.math.sqrt

class VocalSpectrogramTest {
    @Test fun featuresMatchIndependentNumpyReference() {
        val root = File(requireNotNull(System.getProperty("cleanwave.models"))).parentFile!!.parentFile!!.resolve("test/resources")
        val input = floats(File(root, "vocal-input.bin"))
        val reference = floats(File(root, "vocal-features.bin"))
        val actual = FloatArray(VocalSpectrogram.MODEL_ELEMENTS)
        VocalSpectrogram().analyze(input, actual)
        var error = 0.0; var power = 0.0
        for (i in actual.indices) { error += (actual[i] - reference[i]) * (actual[i] - reference[i]); power += reference[i] * reference[i] }
        assertTrue("STFT layout/window differs from NumPy: ${sqrt(error/power)}", sqrt(error/power) < 2e-6)
    }

    @Test fun originalStereoPassesAtZeroStrengthIncludingHighFrequencies() {
        val level = AudioLevel().apply { reduction = 0f; boostDb = 6; resetAmount() }
        val input = FloatArray(2048) { i ->
            (sin((i/2) * (if (i%2==0) 2.57 else 1.97))*.3).toFloat()
        }
        // Quantize once to actual PCM16: zero strength must return every bit.
        val original = ByteArray(input.size*2)
        for (i in input.indices) {
            val raw=(input[i]*32768).toInt();original[i*2]=raw.toByte();original[i*2+1]=(raw shr 8).toByte()
        }
        decodeStereo(original,input)
        val output=ByteArray(original.size)
        level.render(input,FloatArray(input.size),output,false)
        assertArrayEquals(original,output)
    }

    @Test fun nonFiniteModelAudioIsRejected() {
        val level=AudioLevel()
        try { level.render(FloatArray(4),floatArrayOf(0f,Float.NaN,0f,0f),ByteArray(8),false); fail("NaN reached playback") }
        catch (_: IllegalStateException) { }
    }

    @Test fun bypassAndRecoveryUseSmoothWetDryRamps() {
        val level=AudioLevel().apply { reduction=1f; resetAmount() }
        val dry=FloatArray(1024){.2f};val wet=FloatArray(1024){.6f}
        val bypass=ByteArray(dry.size*2);level.render(dry,wet,bypass,true)
        val bypassFloat=FloatArray(dry.size);decodeStereo(bypass,bypassFloat)
        assertTrue(bypassFloat[0]>.55f)
        assertEquals(.2f,bypassFloat[bypassFloat.size-2],.002f)

        val recovered=ByteArray(dry.size*2);level.render(dry,wet,recovered,false)
        val recoveredFloat=FloatArray(dry.size);decodeStereo(recovered,recoveredFloat)
        assertTrue(recoveredFloat[0]<.25f)
        assertEquals(.6f,recoveredFloat[recoveredFloat.size-2],.002f)
    }

    private fun floats(file: File): FloatArray {
        val buffer=ByteBuffer.wrap(file.readBytes()).order(ByteOrder.LITTLE_ENDIAN).asFloatBuffer()
        return FloatArray(buffer.remaining()).also { buffer.get(it) }
    }
}
