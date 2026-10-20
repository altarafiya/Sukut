package com.osama.cleanwave

import org.jtransforms.fft.FloatFFT_1D
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File
import kotlin.math.PI
import kotlin.math.abs
import kotlin.math.sin

class WindowFftTest {
    @Test
    fun AudioWindowsAndFloatFftGiveContinuousIdentityOverlapAdd() {
        val rise = loadQ15("window_576_q15.bin", StreamingDsp.OVERLAP_FRAMES)
        val hop = loadQ15("window_960_q15.bin", StreamingDsp.HOP_FRAMES)
        val fft = FloatFFT_1D(StreamingDsp.FFT_SIZE.toLong())

        val total = StreamingDsp.FFT_SIZE + StreamingDsp.HOP_FRAMES
        val signal = FloatArray(total) { i ->
            (0.31 * sin(2 * PI * 997 * i / 48000.0) +
                0.17 * sin(2 * PI * 4213 * i / 48000.0)).toFloat()
        }

        fun roundTrip(start: Int): FloatArray {
            val spectrum = FloatArray(StreamingDsp.FFT_SIZE * 2)
            for (i in 0 until StreamingDsp.FFT_SIZE) {
                val w = if (i < StreamingDsp.OVERLAP_FRAMES) rise[i]
                else hop[i - StreamingDsp.OVERLAP_FRAMES]
                spectrum[i] = signal[start + i] * w
            }
            fft.realForwardFull(spectrum)
            fft.complexInverse(spectrum, true)
            return FloatArray(StreamingDsp.FFT_SIZE) { spectrum[it * 2] }
        }

        val first = roundTrip(0)
        val second = roundTrip(StreamingDsp.HOP_FRAMES)
        val previousTail = first.copyOfRange(
            StreamingDsp.HOP_FRAMES,
            StreamingDsp.FFT_SIZE,
        )
        val output = FloatArray(StreamingDsp.HOP_FRAMES)
        for (i in 0 until StreamingDsp.OVERLAP_FRAMES) {
            output[i] = previousTail[i] * hop[StreamingDsp.DIRECT_FRAMES + i] + second[i] * rise[i]
        }
        for (i in StreamingDsp.OVERLAP_FRAMES until StreamingDsp.HOP_FRAMES) {
            output[i] = second[i] * hop[i - StreamingDsp.OVERLAP_FRAMES]
        }

        var maxError = 0f
        for (i in output.indices) {
            maxError = maxOf(maxError, abs(output[i] - signal[StreamingDsp.HOP_FRAMES + i]))
        }
        assertTrue("identity WOLA max error=$maxError", maxError < 5e-5f)
    }

    private fun loadQ15(name: String, count: Int): FloatArray {
        val file = File("src/engine/assets/$name")
        val bytes = file.readBytes()
        require(bytes.size == count * 2)
        return FloatArray(count) { i ->
            val lo = bytes[i * 2].toInt() and 0xff
            val hi = bytes[i * 2 + 1].toInt()
            ((lo or (hi shl 8)).toShort().toInt()) / 32768f
        }
    }
}
