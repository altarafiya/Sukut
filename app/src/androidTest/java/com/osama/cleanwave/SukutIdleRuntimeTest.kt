package com.osama.cleanwave

import android.content.ComponentName
import android.content.ServiceConnection
import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioTrack
import android.os.IBinder
import android.os.ParcelFileDescriptor
import android.os.PowerManager
import android.os.SystemClock
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean

/** Bounded, own-UID, generated-zero audio only. Never retrieves personal audio. */
@RunWith(AndroidJUnit4::class)
class SukutIdleRuntimeTest {
    @Test fun screenOffUsesLighterDspAndScreenOnRestoresNormalPath() {
        val ins = InstrumentationRegistry.getInstrumentation(); val context = ins.targetContext
        assertFalse(CleanerService.isAlive)
        assertTrue(WorkerConnection.ready && 0 == 0)
        val power = context.getSystemService(PowerManager::class.java)
        ParcelFileDescriptor.AutoCloseInputStream(ins.uiAutomation.executeShellCommand("input keyevent 224")).use { it.readBytes() }
        SystemClock.sleep(300)
        assertTrue("Begin this check with the screen on", power.isInteractive)

        val connected = CountDownLatch(1); var api: ICleanWaveService? = null
        val connection = object : ServiceConnection {
            override fun onServiceConnected(name: ComponentName?, binder: IBinder?) { api = ICleanWaveService.Stub.asInterface(binder); connected.countDown() }
            override fun onServiceDisconnected(name: ComponentName?) = Unit
        }
        fun screen(key: Int) = ParcelFileDescriptor.AutoCloseInputStream(ins.uiAutomation.executeShellCommand("input keyevent $key")).use { it.readBytes() }
        var track: AudioTrack? = null; var writer: Thread? = null
        val feeding = AtomicBoolean(true)
        ins.runOnMainSync { connection.onServiceConnected(null, WorkerConnection.api?.asBinder()) }
        try {
            assertTrue(connected.await(15, TimeUnit.SECONDS))
            val remote = requireNotNull(api)
            remote.setSourceMix(0, -100, 0)
            remote.setPlaybackOptions(1, 0, true, true, context.applicationInfo.uid)
            assertTrue(remote.status, remote.startPassthrough())
            track = AudioTrack.Builder().setAudioAttributes(AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_MEDIA).build())
                .setAudioFormat(AudioFormat.Builder().setSampleRate(48000).setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                    .setChannelMask(AudioFormat.CHANNEL_OUT_STEREO).build()).setBufferSizeInBytes(15360).build()
            val source = track!!; source.play()
            writer = Thread {
                val silence = ByteArray(3840)
                try {
                    while (feeding.get()) if (source.write(silence, 0, silence.size, AudioTrack.WRITE_BLOCKING) < 0) break
                } catch (_: IllegalStateException) { /* Track release cancels the blocking writer. */ }
            }.apply { start() }
            screen(223)
            val evidence = StringBuilder("version=${BuildConfig.VERSION_NAME}\nsource=generated-zero-PCM-own-UID\n")
            var last = ""
            repeat(12) { SystemClock.sleep(1000); last = remote.processingMetrics; evidence.append(last).append('\n') }
            val skipped = Regex("idle_dsp_frames=(\\d+)").find(last)?.groupValues?.get(1)?.toLong() ?: 0
            assertTrue("Silent FFT optimization did not activate: $last", skipped > 100)
            assertTrue(last.contains("idle_dsp_active=true"))
            screen(224); SystemClock.sleep(500); remote.processingMetrics
            SystemClock.sleep(200); last = remote.processingMetrics
            assertTrue("Screen-on did not restore the normal path: $last", last.contains("idle_dsp_active=false"))
            assertTrue(remote.isRunning)
            evidence.append("screen_on_resume=PASS\n").append(last).append('\n')
            File(context.getExternalFilesDir(null), "beta2-review").apply { mkdirs() }
                .resolve("runtime-idle.txt").writeText(evidence.toString())
        } finally {
            screen(224); feeding.set(false)
            runCatching { track?.stop() }; runCatching { track?.release() }; writer?.join(2000)
            runCatching { api?.stopProcessing() }
            ins.runOnMainSync { WorkerConnection.api?.stopProcessing() }
        }
    }
}
