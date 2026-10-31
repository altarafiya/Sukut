package com.osama.cleanwave

import android.os.Debug
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder
import kotlin.math.sin

/** Generated PCM only. No playback, capture, microphone or device audio routing. */
@RunWith(AndroidJUnit4::class)
class SukutIdleParityTest {
    @Test fun screenOffDspSavingsMatchContinuousProcessingAndKeepFirstQuietSamples() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val rise = AssetSeal.bytes(context, "window_576_q15.bin")
        val hop = AssetSeal.bytes(context, "window_960_q15.bin")
        fun dsp() = StreamingDsp(AssetSeal.model(context), rise, hop)
        val reference = dsp(); val candidate = dsp()
        val dryA = FloatArray(1920); val dryB = FloatArray(1920)
        val wetA = FloatArray(1920); val wetB = FloatArray(1920)
        var frames = 0
        var candidateCpu = 0L; var referenceCpu = 0L
        fun frame(signal: Boolean, screenOff: Boolean, quiet: Boolean = false) {
            val input = FloatArray(if (frames == 0) 3072 else 1920) { index ->
                if (!signal) 0f else ((if (quiet) 1f / 32768f else .1f) * sin((index + frames * 960) * .031)).toFloat()
            }
            var start = Debug.threadCpuTimeNanos()
            reference.process(input, frames == 0, true, 4096, 0, 1295, dryA, wetA)
            referenceCpu += Debug.threadCpuTimeNanos() - start
            start = Debug.threadCpuTimeNanos()
            candidate.process(input, frames == 0, true, 4096, 0, 1295, dryB, wetB, allowIdle = screenOff)
            candidateCpu += Debug.threadCpuTimeNanos() - start
            for (i in wetA.indices) {
                if (wetA[i] != wetB[i]) fail("Wet sample changed at frame=$frames sample=$i: ${wetA[i]} / ${wetB[i]}")
                if (dryA[i] != dryB[i]) fail("Dry sample changed at frame=$frames sample=$i")
            }
            if (signal || !screenOff) assertFalse(candidate.idleDspActive)
            frames++
        }
        try {
            repeat(10) { frame(true, false) }
            repeat(600) { frame(false, true) }
            assertTrue("Silent FFT optimization did not activate", candidate.idleDspFrames > 100)
            repeat(8) { frame(true, true, quiet = true) }
            repeat(8) { frame(true, true) }
            repeat(180) { frame(false, true) }
            frame(false, false)
            repeat(8) { frame(true, false) }
            val evidence = File(context.getExternalFilesDir(null), "beta2-review").apply { mkdirs() }
            File(evidence, "idle-parity.txt").writeText("version=${BuildConfig.VERSION_NAME}\nframes=$frames\n" +
                "exact_output_parity=PASS\nquiet_resume=PASS\nscreen_on_resume=PASS\n" +
                "idle_dsp_frames=${candidate.idleDspFrames}\nreference_cpu_ns=$referenceCpu\ncandidate_cpu_ns=$candidateCpu\n" +
                "Synthetic DSP test; CPU totals are not an overall battery measurement.\n")
        } finally { reference.close(); candidate.close() }
    }
}
