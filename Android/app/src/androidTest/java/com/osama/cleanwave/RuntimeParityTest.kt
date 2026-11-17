package com.osama.cleanwave

import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.FloatBuffer
import java.security.MessageDigest
import java.util.Random
import org.junit.Assert.*
import org.junit.Assume.assumeTrue
import org.junit.Test
import org.junit.runner.RunWith

/** Synthetic CPU outputs and full recurrent-state digests; no audio or plain model files. */
@RunWith(AndroidJUnit4::class)
class RuntimeParityTest {
    @Test fun compareRuntimeWithoutChangingTheModelOrDsp() {
        val mode=InstrumentationRegistry.getArguments().getString("runtimeParity")
        assumeTrue(mode in listOf("record","compare"))
        val context=InstrumentationRegistry.getInstrumentation().targetContext
        val folder=context.getExternalFilesDir("release-04-qa")!!
        val values=if(InstrumentationRegistry.getArguments().getString("runtimeValues")=="true")
            File(folder,"runtime-values-$mode.bin").outputStream().buffered() else null
        val actual=mutableListOf<String>()
        val timing=mutableListOf<String>()
        try { for(threads in 1..2)SeparationModel(AssetSeal.model(context),threads,false).use { model ->
            val state=SeparationModel::class.java.getDeclaredField("currentState").apply { isAccessible=true }
            val left=FloatArray(1024); val right=FloatArray(1024); val outputs=Array(4) { FloatArray(1024) }
            repeat(4) { model.processInto(left,right,outputs[0],outputs[1],outputs[2],outputs[3]) }; model.reset()
            val random=Random(0x53554b55L)
            val packed=ByteBuffer.allocate((4096+14976)*4).order(ByteOrder.LITTLE_ENDIAN)
            val digest=MessageDigest.getInstance("SHA-256")
            val elapsed=DoubleArray(160)
            repeat(160) { frame ->
                val scale=when((frame/20)%4) { 0->.001f; 1->.1f; 2->1f; else->10f }
                for(i in left.indices) {
                    left[i]=if(frame%19==0)0f else (random.nextFloat()*2-1)*scale
                    right[i]=if(frame%23==0)0f else (random.nextFloat()*2-1)*scale
                }
                val start=System.nanoTime()
                model.processInto(left,right,outputs[0],outputs[1],outputs[2],outputs[3])
                elapsed[frame]=(System.nanoTime()-start)/1e6
                packed.clear(); outputs.forEach { head -> head.forEach { packed.putInt(it.toRawBits()) } }
                val recurrent=(state.get(model) as FloatBuffer).duplicate().apply { rewind() }
                while(recurrent.hasRemaining())packed.putInt(recurrent.get().toRawBits())
                values?.write(packed.array())
                actual.add("$threads:$frame:"+digest.digest(packed.array()).joinToString("") { "%02x".format(it) })
            }
            timing.add("threads=$threads avg_ms=${elapsed.average()} median_ms=${elapsed.sorted()[80]}")
        } } finally { values?.close() }
        val baseline=File(folder,"runtime-baseline.txt")
        File(folder,"runtime-$mode.txt").writeText(actual.joinToString("\n"))
        File(folder,"runtime-timing-$mode.txt").writeText(timing.joinToString("\n"))
        if(mode=="record")baseline.writeText(actual.joinToString("\n")) else {
            val expected=baseline.readLines(); assertEquals(expected.size,actual.size)
            for(i in expected.indices)assertEquals("CPU spectral output and recurrent state row $i",expected[i],actual[i])
        }
        println("RUNTIME_PARITY mode=$mode rows=${actual.size} output_and_state_floats_per_row=19072")
    }
}
