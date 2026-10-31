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

/** Generated silence, target-app UID only. Does not change saved settings or volume. */
@RunWith(AndroidJUnit4::class)
class WorkerEngineSmokeTest {
    @Test fun ownWorkerLoadsTheModelRoutesOwnSilenceAndReturnsToOff() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val deadline = SystemClock.elapsedRealtime() + 8000
        while (!WorkerConnection.ready && SystemClock.elapsedRealtime() < deadline) SystemClock.sleep(50)
        val api = requireNotNull(WorkerConnection.api) { "Activate Sukut first" }
        assertFalse("Begin with processing Off", api.isRunning)
        val pid = WorkerConnection.workerPid
        repeat(2) {
            var source: AudioTrack? = null
            try {
                api.setProcessingMode(CleanerPrefs.processingMode(context))
                api.setSourceMix(0, -100, -100)
                api.setPlaybackOptions(1, 0, true, true, context.applicationInfo.uid)
                val started = api.startPassthrough()
                val startStatus = api.status
                println("SUKUT_START_STATUS=$startStatus")
                assertTrue(startStatus, started)
                source = AudioTrack.Builder()
                    .setAudioAttributes(AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_MEDIA).build())
                    .setAudioFormat(AudioFormat.Builder().setSampleRate(48000).setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                        .setChannelMask(AudioFormat.CHANNEL_OUT_STEREO).build())
                    .setBufferSizeInBytes(15360).build()
                source.play()
                val silence = ByteArray(3840)
                repeat(20) { assertTrue(source.write(silence, 0, silence.size, AudioTrack.WRITE_BLOCKING) > 0) }
                SystemClock.sleep(500)
                assertTrue(api.status, api.isRunning)
                val health = EngineHealth.parse(api.healthSnapshot)
                assertTrue(health.detail, health.running)
                println("SUKUT_WORKER_ENGINE_PASS pid=$pid generated_silence_only=true")
            } finally {
                runCatching { source?.stop() }; runCatching { source?.release() }
                api.stopProcessing()
            }
            assertFalse(api.isRunning)
            assertTrue(WorkerConnection.ready)
            assertEquals(pid, WorkerConnection.workerPid)
            println("SUKUT_WORKER_OFF_PASS same_pid=true")
        }
    }
}
