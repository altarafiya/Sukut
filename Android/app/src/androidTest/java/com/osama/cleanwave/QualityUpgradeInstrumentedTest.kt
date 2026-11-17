package com.osama.cleanwave

import android.os.Process
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import java.io.ByteArrayOutputStream
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder
import kotlin.math.sqrt
import kotlin.math.sin
import org.lsposed.hiddenapibypass.HiddenApiBypass

@RunWith(AndroidJUnit4::class)
class QualityUpgradeInstrumentedTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()

    @Test fun nativeVocalModelMatchesDesktopAndLeavesProcessingHeadroom() {
        val old=Process.getThreadPriority(Process.myTid());Process.setThreadPriority(Process.THREAD_PRIORITY_AUDIO)
        try {
            val features=floats("vocal-features.bin");val reference=floats("vocal-reference.bin")
            val actual=FloatArray(reference.size);val times=mutableListOf<Double>()
            OnnxVocalModel(instrumentation.targetContext.assets.open("singing.onnx").use { it.readBytes() }).use { model ->
                repeat(3){model.process(features,actual)}
                repeat(12){val tick=System.nanoTime();model.process(features,actual);times.add((System.nanoTime()-tick)/1e6)}
            }
            var error=0.0;var power=0.0
            for(i in actual.indices){assertTrue(actual[i].isFinite());val diff=actual[i]-reference[i];error+=diff*diff;power+=reference[i]*reference[i]}
            val relative=sqrt(error/(power+1e-20));assertTrue("Phone model output changed: $relative",relative<.002)
            val sorted=times.sorted();val p95=sorted[11]
            assertTrue("Vocal inference lacks live headroom: $p95 ms",p95 < VocalSpectrogram.STEP/44.1*.9)
            File(instrumentation.targetContext.filesDir,"stage4-vocal-numerics.txt").writeText(
                "relative_rms_error=$relative; avg_ms=${times.average()}; p95_ms=$p95; samples=${times.size}\n")
        } finally {Process.setThreadPriority(old)}
    }

    @Test fun bothActiveRoutesPreserveStereoPcmAtZeroStrength() {
        HiddenApiBypass.setHiddenApiExemptions("")
        val old=Process.getThreadPriority(Process.myTid());Process.setThreadPriority(Process.THREAD_PRIORITY_AUDIO)
        try {
            for(mode in listOf(0,1)) {
                MusicRemovalProcessor(instrumentation.targetContext,mode).use { processor ->
                    processor.setReduction(0);processor.setVoiceBoost(6)
                    val source=ByteArray((processor.firstInputFrames+processor.inputFrames*7)*4){i->((i*97+i/17)%256).toByte()}
                    val sink=ByteArrayOutputStream();var read=0
                    repeat(8){packet->
                        val count=(if(packet==0)processor.firstInputFrames else processor.inputFrames)*4
                        val input=source.copyOfRange(read,read+count);read+=count
                        val output=ByteArray(processor.outputFrames*4)
                        processor.process(input,packet==0,output,0);sink.write(output)
                    }
                    val delay=if(mode==0)1536*4 else 0
                    val actual=sink.toByteArray().copyOfRange(delay,sink.size())
                    assertArrayEquals("Stereo dry signal changed in mode $mode",source.copyOfRange(0,actual.size),actual)
                    assertFalse("Engine unexpectedly paused",processor.isBypassed())
                }
            }
        } finally {Process.setThreadPriority(old)}
    }

    @Test fun singingFftModelAndOutputFitTheLivePacketBudget() {
        HiddenApiBypass.setHiddenApiExemptions("")
        val old=Process.getThreadPriority(Process.myTid());Process.setThreadPriority(Process.THREAD_PRIORITY_AUDIO)
        try {
            MusicRemovalProcessor(instrumentation.targetContext,MusicRemovalProcessor.SINGING).use { processor ->
                processor.setReduction(100)
                var sample=0
                repeat(8){packet->
                    val frames=if(packet==0)processor.firstInputFrames else processor.inputFrames
                    val input=ByteArray(frames*4)
                    for(frame in 0 until frames) {
                        for(channel in 0..1) {
                            val value=(sin((sample+frame)*(if(channel==0).173 else .231))*.2*32768).toInt()
                            val at=frame*4+channel*2;input[at]=value.toByte();input[at+1]=(value shr 8).toByte()
                        }
                    }
                    sample+=frames
                    processor.process(input,packet==0,ByteArray(processor.outputFrames*4),0)
                }
                assertFalse("Full stereo FFT/model/output missed sustained live deadlines",processor.isBypassed())
                File(instrumentation.targetContext.filesDir,"stage4-singing-pipeline.txt").writeText(processor.metrics()+"\n")
            }
        } finally {Process.setThreadPriority(old)}
    }

    private fun floats(name:String):FloatArray {
        val bytes=instrumentation.context.assets.open(name).use{it.readBytes()}
        val buffer=ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN).asFloatBuffer()
        return FloatArray(buffer.remaining()).also {buffer.get(it)}
    }
}
