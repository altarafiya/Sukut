package com.osama.cleanwave

import android.content.ComponentName
import android.content.Intent
import android.content.ServiceConnection
import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioTrack
import android.os.IBinder
import android.os.Process
import android.os.SystemClock
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import rikka.shizuku.Shizuku
import java.io.File
import java.io.FileInputStream
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.channels.FileChannel
import java.security.MessageDigest
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean
import kotlin.math.sin

/** Opt-in connected-phone probe. Plays only its own generated stereo fixture. */
@RunWith(AndroidJUnit4::class)
class PhoneTuningTest {
    private val ins = InstrumentationRegistry.getInstrumentation()
    private val context = ins.targetContext
    private val evidence get() = File(context.filesDir, "phone-tuning.txt")

    @Test fun measureAcousticRoutes() {
        val arguments=InstrumentationRegistry.getArguments()
        val tag=arguments.getString("run_tag") ?: "baseline"
        require(tag.matches(Regex("[A-Za-z0-9_-]{1,32}")))
        val report=File(context.filesDir,"acoustic-routing.txt")
        report.writeText("version=${BuildConfig.VERSION_NAME} run=$tag\n")
        context.startService(Intent(context,CleanerService::class.java).setAction(CleanerService.ACTION_DISABLE))
        val stopUntil=SystemClock.elapsedRealtime()+10000
        while(CleanerService.isAlive && SystemClock.elapsedRealtime()<stopUntil) SystemClock.sleep(100)
        assertFalse("Normal processing must stop before the isolated probe",CleanerService.isAlive)
        val binderUntil=SystemClock.elapsedRealtime()+10000
        while(!runCatching { Shizuku.pingBinder() }.getOrDefault(false) && SystemClock.elapsedRealtime()<binderUntil) SystemClock.sleep(100)
        assertTrue("Shizuku must be available",Shizuku.pingBinder() && Shizuku.checkSelfPermission()==0)
        val args=Shizuku.UserServiceArgs(ComponentName(context,PrivilegedAudioService::class.java))
            .processNameSuffix("sukut_latency").tag("sukut-latency-probe").version(BuildConfig.VERSION_CODE).daemon(false)
        val latch=CountDownLatch(1)
        var api:ICleanWaveService?=null
        val connection=object:ServiceConnection {
            override fun onServiceConnected(name:ComponentName?,binder:IBinder?) { api=ICleanWaveService.Stub.asInterface(binder);latch.countDown() }
            override fun onServiceDisconnected(name:ComponentName?)=Unit
        }
        ins.runOnMainSync { Shizuku.bindUserService(args,connection) }
        assertTrue("Latency worker did not connect",latch.await(15,TimeUnit.SECONDS))
        try {
            val phases=(arguments.getString("phases") ?: "off_a,unity,optimized,off_b").split(',')
            for(phase in phases) {
                val remote=requireNotNull(api)
                remote.stopProcessing()
                if(!phase.startsWith("off")) {
                    remote.setProcessingMode(0); remote.setMusicReduction(100)
                    remote.setSourceMix(0,if(phase=="unity")0 else -20,if(phase=="unity")0 else -10)
                    remote.setPlaybackOptions(arguments.getString("profile")?.toInt() ?: 0,0,true,arguments.getString("effects")!="false",-1)
                    assertTrue(remote.status,remote.startPassthrough())
                }
                val run="${tag}_$phase"
                val path="/sdcard/Android/data/com.osama.sukut.latencyprobe/files/$run/result.json"
                report.appendText("start $run ${if(remote.isRunning)remote.processingMetrics else "OFF"}\n")
                val command="am start -W --activity-clear-task -n com.osama.sukut.latencyprobe/.ProbeActivity --es run $run --ei seconds 18"
                val launch=ins.uiAutomation.executeShellCommand(command).use { android.os.ParcelFileDescriptor.AutoCloseInputStream(it).bufferedReader().readText() }
                report.appendText(launch+"\n")
                var result=""
                val until=SystemClock.elapsedRealtime()+40000
                var poll=0
                while(SystemClock.elapsedRealtime()<until) {
                    SystemClock.sleep(500)
                    result=ins.uiAutomation.executeShellCommand("cat $path").use { android.os.ParcelFileDescriptor.AutoCloseInputStream(it).bufferedReader().readText() }
                    if(result.trimStart().startsWith("{")) break
                    if(++poll%2==0 && remote.isRunning) report.appendText("live $run ${remote.processingMetrics}\n")
                }
                assertTrue("Probe did not finish $run: $result",result.trimStart().startsWith("{"))
                assertFalse("Probe failed $run: $result",org.json.JSONObject(result).has("error"))
                report.appendText("complete $run ${if(remote.isRunning)remote.processingMetrics else "OFF"}\n")
                remote.stopProcessing()
                SystemClock.sleep(400)
            }
        } finally {
            api?.stopProcessing()
            ins.runOnMainSync { Shizuku.unbindUserService(args,connection,true) }
        }
    }

    @Test fun measureQualityAndLiveProfiles() {
        assertTrue(BuildConfig.ENGINE_RUNTIME)
        evidence.writeText("version=${context.packageManager.getPackageInfo(context.packageName,0).versionName}; helper_target=${BuildConfig.VERSION_NAME}\n")
        val before = Process.getThreadPriority(Process.myTid())
        Process.setThreadPriority(Process.THREAD_PRIORITY_AUDIO)
        try {
            for (preset in listOf(2, 0, 3)) {
                val hash = MessageDigest.getInstance("SHA-256")
                // Disable the wall-clock recovery guard for deterministic quality comparison.
                // The live routing tests below retain normal overload protection.
                val model = context.assets.openFd("separator.tflite").use { fd ->
                    FileInputStream(fd.fileDescriptor).channel.use { it.map(FileChannel.MapMode.READ_ONLY, fd.startOffset, fd.length).order(ByteOrder.nativeOrder()) }
                }
                val type = Class.forName("com.osama.cleanwave.StreamingCleaner")
                val cleaner = type.getConstructor(ByteBuffer::class.java, ByteArray::class.java, ByteArray::class.java, Boolean::class.javaPrimitiveType)
                    .newInstance(model, context.assets.open("window_576_q15.bin").use { it.readBytes() },
                        context.assets.open("window_960_q15.bin").use { it.readBytes() }, false) as AudioCleaner
                cleaner.use {
                    it.setReduction(100); it.setAmplification(0)
                    it.setSourceMix(0, if (preset == 0) -20 else -100, when (preset) { 0 -> -10; 3 -> -100; else -> 0 })
                    var frame = 0
                    val times = DoubleArray(150)
                    repeat(times.size) { packet ->
                        val input = fixture(if (packet == 0) it.firstInputFrames else it.inputFrames, frame)
                        frame += input.size / 4
                        val output = ByteArray(it.outputFrames * 4)
                        val start = System.nanoTime(); it.process(input, packet == 0, output, 0)
                        times[packet] = (System.nanoTime() - start) / 1e6
                        hash.update(output)
                    }
                    evidence.appendText("quality preset=$preset sha256=${hash.digest().joinToString("") { b -> "%02x".format(b) }} avg_ms=${times.average()} max_ms=${times.max()}\n")
                }
            }
        } finally { Process.setThreadPriority(before) }

        val until = SystemClock.elapsedRealtime() + 10000
        while (!runCatching { Shizuku.pingBinder() }.getOrDefault(false) && SystemClock.elapsedRealtime() < until) SystemClock.sleep(100)
        assertTrue("Shizuku must be running and authorized", Shizuku.pingBinder() && Shizuku.checkSelfPermission() == 0)
        val args = Shizuku.UserServiceArgs(ComponentName(context, PrivilegedAudioService::class.java))
            .processNameSuffix("sukut_tuning").tag("sukut-phone-tuning").version(BuildConfig.VERSION_CODE).daemon(false)
        val latch = CountDownLatch(1)
        var api: ICleanWaveService? = null
        val connection = object : ServiceConnection {
            override fun onServiceConnected(name: ComponentName?, binder: IBinder?) { api = ICleanWaveService.Stub.asInterface(binder); latch.countDown() }
            override fun onServiceDisconnected(name: ComponentName?) = Unit
        }
        ins.runOnMainSync { Shizuku.bindUserService(args, connection) }
        assertTrue("Test audio worker did not connect", latch.await(15, TimeUnit.SECONDS))
        try {
            val arguments = InstrumentationRegistry.getArguments()
            val profiles = arguments.getString("profile")?.toInt()?.let { listOf(it.coerceIn(0, 2)) } ?: (0..2).toList()
            val seconds = arguments.getString("seconds")?.toInt()?.coerceIn(5, 120) ?: 12
            for (profile in profiles) {
                val remote = requireNotNull(api)
                remote.setProcessingMode(0); remote.setSourceMix(0, -100, 0); remote.setMusicReduction(100)
                remote.setPlaybackOptions(profile, 0, true, arguments.getString("effects")!="false", -1)
                val started = remote.startPassthrough()
                evidence.appendText("start profile=$profile ok=$started status=${remote.status}\n")
                assertTrue(remote.status, started)
                val playing = AtomicBoolean(true)
                val player = Thread {
                    val track = AudioTrack.Builder().setAudioAttributes(AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_MEDIA).setContentType(AudioAttributes.CONTENT_TYPE_MUSIC).build())
                        .setAudioFormat(AudioFormat.Builder().setSampleRate(48000).setEncoding(AudioFormat.ENCODING_PCM_16BIT).setChannelMask(AudioFormat.CHANNEL_OUT_STEREO).build())
                        .setBufferSizeInBytes(19200).build()
                    try {
                        track.play(); var frame = 0
                        while (playing.get()) { val bytes = fixture(960, frame); frame += 960; track.write(bytes, 0, bytes.size, AudioTrack.WRITE_BLOCKING) }
                    } finally { track.pause(); track.flush(); track.release() }
                }.apply { name = "Sukut-Test-Fixture"; start() }
                try {
                    repeat(seconds) { second ->
                        SystemClock.sleep(1000)
                        assertTrue(remote.status, remote.isRunning)
                        val metrics=remote.processingMetrics
                        evidence.appendText("live profile=$profile second=${second + 1} $metrics\n")
                        if(arguments.getString("strict")=="true") {
                            assertTrue("AI paused: $metrics",metrics.contains("ai_paused=false"))
                            for(key in listOf("output_underruns","fallback_events","recoveries","recovery_probe_failures")) {
                                val value=Regex("(?:^|; )$key=([0-9]+)").find(metrics)?.groupValues?.get(1)?.toLong()
                                assertEquals("$key in $metrics",0L,value)
                            }
                        }
                    }
                } finally { playing.set(false); player.join(2000); remote.stopProcessing() }
            }
        } finally { ins.runOnMainSync { Shizuku.unbindUserService(args, connection, true) } }
    }

    @Test fun restoreRequestedState() {
        val arguments=InstrumentationRegistry.getArguments()
        val profile=arguments.getString("profile")?.toInt() ?: 1
        val mode=arguments.getString("mode")?.toInt() ?: 3
        val enabled=arguments.getString("enabled") != "false"
        val until=SystemClock.elapsedRealtime()+10000
        while(!runCatching { Shizuku.pingBinder() }.getOrDefault(false) && SystemClock.elapsedRealtime()<until) SystemClock.sleep(100)
        PlaybackController.disable(context)
        val stopping=SystemClock.elapsedRealtime()+10000
        while(CleanerService.isAlive && SystemClock.elapsedRealtime()<stopping) SystemClock.sleep(100)
        assertFalse(CleanerService.isAlive)
        CleanerPrefs.setBuffering(context,profile)
        CleanerPrefs.setSourcePreset(context,mode)
        CleanerPrefs.setAmplification(context,0)
        CleanerPrefs.setEffectsCompatibility(context,arguments.getString("effects")!="false")
        if(enabled) {
            val home=ins.startActivitySync(Intent(context,MainActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
            ins.runOnMainSync { PlaybackController.enable(home) }
            val ready=SystemClock.elapsedRealtime()+20000
            while(CleanerPrefs.status(context)!="Active" && SystemClock.elapsedRealtime()<ready) SystemClock.sleep(200)
            assertEquals("Active",CleanerPrefs.status(context))
            assertTrue(CleanerPrefs.isEnabled(context))
        }
        File(context.filesDir,"latency-restored-state.txt").writeText(
            "version=${context.packageManager.getPackageInfo(context.packageName,0).versionName}; enabled=${CleanerPrefs.isEnabled(context)}; mode=${CleanerPrefs.sourcePreset(context)}; buffering=${CleanerPrefs.buffering(context)}; effects=${CleanerPrefs.effectsCompatibility(context)}; amplification=${CleanerPrefs.amplification(context)}\n")
    }

    private fun fixture(frames: Int, start: Int): ByteArray = ByteArray(frames * 4).also { data ->
        for (frame in 0 until frames) for (channel in 0..1) {
            val t = (start + frame) / 48000.0
            val sample = ((sin(t * (if (channel == 0) 233 else 311) * 2 * Math.PI) * .05 + sin(t * 1301 * 2 * Math.PI) * .015) * 32767).toInt()
            val at = frame * 4 + channel * 2; data[at] = sample.toByte(); data[at + 1] = (sample shr 8).toByte()
        }
    }
}
