package com.osama.cleanwave

import android.os.Debug
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.io.FileInputStream
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.channels.FileChannel
import java.security.MessageDigest
import kotlin.math.sin

/** Deterministic PCM only: no playback, recording, routing or preference changes. */
@RunWith(AndroidJUnit4::class)
class SukutEfficiencyTest {
    @Test fun deterministicPcmAndResidentMemory() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val tag = InstrumentationRegistry.getArguments().getString("run_tag") ?: "sample"
        require(tag.matches(Regex("[a-z0-9-]+")))
        val results = JSONArray()
        for ((preset, background) in listOf(2 to 0, 0 to -7, 3 to -100)) {
            val model = context.assets.openFd("separator.tflite").use { fd ->
                FileInputStream(fd.fileDescriptor).channel.use { channel ->
                    channel.map(FileChannel.MapMode.READ_ONLY, fd.startOffset, fd.length).order(ByteOrder.nativeOrder())
                }
            }
            val cleaner = Class.forName("com.osama.cleanwave.StreamingCleaner")
                .getConstructor(ByteBuffer::class.java, ByteArray::class.java, ByteArray::class.java, Boolean::class.javaPrimitiveType)
                .newInstance(model, context.assets.open("window_576_q15.bin").use { it.readBytes() },
                    context.assets.open("window_960_q15.bin").use { it.readBytes() }, false) as AudioCleaner
            cleaner.use {
                it.setReduction(100); it.setAmplification(0); it.setSourceMix(0, -100, background)
                val hash = MessageDigest.getInstance("SHA-256")
                val first = fixture(it.firstInputFrames, 0)
                val hop = ByteArray(it.inputFrames * 4)
                val output = ByteArray(it.outputFrames * 4)
                var frame = it.firstInputFrames
                it.process(first, true, output, 0); hash.update(output)
                repeat(199) {
                    fixtureInto(hop, frame); frame += hop.size / 4
                    cleaner.process(hop, false, output, 0); hash.update(output)
                }
                assertFalse(it.isBypassed())
                // Sampling only: do not force collections or trim app working sets.
                val memory = Debug.MemoryInfo(); Debug.getMemoryInfo(memory)
                results.put(JSONObject().put("preset", preset).put("backgroundDb", background)
                    .put("pcmSha256", hash.digest().joinToString("") { b -> "%02x".format(b) })
                    .put("pssKiB", memory.totalPss).put("privateDirtyKiB", memory.totalPrivateDirty)
                    .put("javaHeapBytes", Runtime.getRuntime().totalMemory() - Runtime.getRuntime().freeMemory()))
            }
        }
        val version = context.packageManager.getPackageInfo(context.packageName, 0).versionName
        File(context.filesDir, "efficiency-$tag.json").writeText(JSONObject()
            .put("version", version).put("hopsPerMode", 200).put("results", results)
            .put("audioPlayedOrRecorded", false).toString(2))
    }
    private fun fixture(frames: Int, start: Int) = ByteArray(frames * 4).also { fixtureInto(it, start) }
    private fun fixtureInto(data: ByteArray, start: Int) {
        for (frame in 0 until data.size / 4) for (channel in 0..1) {
            val t = (start + frame) / 48000.0
            val sample = ((sin(t * (if (channel == 0) 233 else 311) * 2 * Math.PI) * .05 +
                sin(t * 1301 * 2 * Math.PI) * .015) * 32767).toInt()
            val at = frame * 4 + channel * 2
            data[at] = sample.toByte(); data[at + 1] = (sample shr 8).toByte()
        }
    }
}
