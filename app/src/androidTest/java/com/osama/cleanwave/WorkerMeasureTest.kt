package com.osama.cleanwave

import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioTrack
import android.os.SystemClock
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith

/**
 * Generated silence, target-app UID only; no saved settings or volume change.
 * Times three Start/Off cycles so the first (kernel comparison) and later (reused
 * choice) Starts can be compared while the host samples the worker's memory.
 */
@RunWith(AndroidJUnit4::class)
class WorkerMeasureTest {
    @Test fun startOffCyclesReportStartTimeAndKernels() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val deadline = SystemClock.elapsedRealtime() + 8000
        while (!WorkerConnection.ready && SystemClock.elapsedRealtime() < deadline) SystemClock.sleep(50)
        val api = requireNotNull(WorkerConnection.api) { "Activate Sukut first" }
        assertFalse("Begin with processing Off", api.isRunning)
        repeat(3) { cycle ->
            var source: AudioTrack? = null
            try {
                api.setProcessingMode(CleanerPrefs.processingMode(context))
                api.setSourceMix(0, -100, -100)
                api.setPlaybackOptions(1, 0, true, true, context.applicationInfo.uid)
                val begin = SystemClock.elapsedRealtimeNanos()
                assertTrue(api.status, api.startPassthrough())
                val startMs = (SystemClock.elapsedRealtimeNanos() - begin) / 1e6
                source = AudioTrack.Builder()
                    .setAudioAttributes(AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_MEDIA).build())
                    .setAudioFormat(AudioFormat.Builder().setSampleRate(48000).setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                        .setChannelMask(AudioFormat.CHANNEL_OUT_STEREO).build())
                    .setBufferSizeInBytes(15360).build()
                source.play()
                // Three seconds of 20 ms packets: shorter than the 5 s standby, so the model runs throughout.
                val silence = ByteArray(3840)
                repeat(150) { assertTrue(source.write(silence, 0, silence.size, AudioTrack.WRITE_BLOCKING) > 0) }
                assertTrue(api.status, api.isRunning)
                // The kernels value itself contains "; " (for example "builtin; xnnpack=rejected").
                val metrics = api.processingMetrics
                val kernels = Regex("kernels=(.*?); kernel_reference_ms=([^;]+); kernel_selected_ms=([^;]+)")
                    .find(metrics)?.groupValues?.drop(1)
                fun field(name: String) = Regex("\\b$name=([^;]+)").find(metrics)?.groupValues?.get(1)
                println("SUKUT_MEASURE cycle=${cycle + 1} start_ms=${"%.0f".format(startMs)} kernels=$kernels" +
                    " hop_avg_ms=${field("avg_ms")} hop_max_ms=${field("max_ms")} ai_runs=${field("ai_runs")}")
            } finally {
                runCatching { source?.stop() }; runCatching { source?.release() }
                api.stopProcessing()
            }
            assertFalse(api.isRunning)
            SystemClock.sleep(2000) // Off: lets the host sample idle memory between cycles.
        }
    }
}
