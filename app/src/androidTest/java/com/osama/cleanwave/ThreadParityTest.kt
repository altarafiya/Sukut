package com.osama.cleanwave

import android.os.Process
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File
import java.io.FileInputStream
import java.nio.ByteOrder
import java.nio.FloatBuffer
import java.nio.channels.FileChannel
import java.util.Random

/** Exact CPU-kernel/recurrent-state parity, independently of live timing guards. */
@RunWith(AndroidJUnit4::class)
class ThreadParityTest {
    @Test fun oneAndTwoThreadsProduceIdenticalBits() {
        val context=InstrumentationRegistry.getInstrumentation().targetContext
        val report=File(context.filesDir,"thread-parity.txt")
        report.writeText("version=${context.packageManager.getPackageInfo(context.packageName,0).versionName}\n")
        val priority=Process.getThreadPriority(Process.myTid())
        Process.setThreadPriority(Process.THREAD_PRIORITY_AUDIO)
        try {
            val model=context.assets.openFd("separator.tflite").use { fd ->
                FileInputStream(fd.fileDescriptor).channel.use { it.map(FileChannel.MapMode.READ_ONLY,fd.startOffset,fd.length).order(ByteOrder.nativeOrder()) }
            }
            SeparationModel(model,1).use { reference -> SeparationModel(model,2).use { parallel ->
                val state=SeparationModel::class.java.getDeclaredField("currentState").apply { isAccessible=true }
                val left=FloatArray(1024); val right=FloatArray(1024)
                val a=Array(4){FloatArray(1024)};val b=Array(4){FloatArray(1024)}
                repeat(4) { reference.processInto(left,right,a[0],a[1],a[2],a[3]);parallel.processInto(left,right,b[0],b[1],b[2],b[3]) }
                reference.reset();parallel.reset()
                val random=Random(0x53554b55L)
                val t1=DoubleArray(160);val t2=DoubleArray(160)
                repeat(160) { frame ->
                    val scale=when((frame/20)%4){0->.001f;1->.1f;2->1f;else->10f}
                    for(i in left.indices) {
                        left[i]=if(frame%19==0)0f else (random.nextFloat()*2-1)*scale
                        right[i]=if(frame%23==0)0f else (random.nextFloat()*2-1)*scale
                    }
                    var start=System.nanoTime();reference.processInto(left,right,a[0],a[1],a[2],a[3]);t1[frame]=(System.nanoTime()-start)/1e6
                    start=System.nanoTime();parallel.processInto(left,right,b[0],b[1],b[2],b[3]);t2[frame]=(System.nanoTime()-start)/1e6
                    for(head in a.indices) for(i in a[head].indices)
                        assertEquals("frame=$frame head=$head element=$i",a[head][i].toRawBits(),b[head][i].toRawBits())
                    val sa=(state.get(reference) as FloatBuffer).duplicate().apply { rewind() }
                    val sb=(state.get(parallel) as FloatBuffer).duplicate().apply { rewind() }
                    for(i in 0 until sa.capacity()) assertEquals("frame=$frame state=$i",sa.get().toRawBits(),sb.get().toRawBits())
                }
                report.appendText("PASS frames=160 output_floats_per_frame=4096 state_floats_per_frame=14976 bitwise_equal=true one_avg_ms=${t1.average()} two_avg_ms=${t2.average()} one_max_ms=${t1.max()} two_max_ms=${t2.max()}\n")
            } }
        } finally { Process.setThreadPriority(priority) }
    }
}
