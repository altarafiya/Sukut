package com.osama.cleanwave

import org.junit.Assert.*
import org.junit.Assume.assumeTrue
import org.junit.Test
import java.io.ByteArrayOutputStream
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder
import kotlin.math.ln

class ActualCleanersTest {
    private val models = File(requireNotNull(System.getProperty("cleanwave.models")))
    private val research = File(requireNotNull(System.getProperty("cleanwave.research")))

    @Test fun bundledCleanSpeechRetainsFidelityAndVolume() {
        val fixture=models.parentFile!!.parentFile!!.resolve("test/resources/clean-speech-reference.wav")
        val source=pcm(fixture);val actual=render(0,source,100);val result=score(source,actual)
        assertTrue("Clean speech fidelity reduced: $result",result.first>25)
        assertTrue("Clean speech volume reduced: $result",result.second>-.75)
    }

    @Test fun bothRealEnginesPreserveEveryStereoPcmBitAtZeroStrength() {
        for (mode in listOf(MusicRemovalProcessor.DIALOGUE,MusicRemovalProcessor.SINGING)) {
            val frames=if(mode==0)48000 else 44100
            val source=ByteArray(frames*4) { i -> ((i*97+i/17)%256).toByte() }
            val output=render(mode,source,0)
            assertArrayEquals("Zero strength altered stereo PCM in mode $mode",source,output)
        }
    }

    @Test fun productionProcessorsImproveReferencesWithoutMufflingCleanSpeech() {
        // Real song test excerpts stay in private research work, outside APKs/source deliverables.
        val fixture=research.resolve("fixtures")
        assumeTrue("Private quality fixtures unavailable",fixture.resolve("clean-speech-48k.wav").isFile)
        val report=StringBuilder("fixture,mode,baseline_si_sdr_db,processed_si_sdr_db,voice_projection_gain_db\n")
        val clean=pcm(fixture.resolve("clean-speech-48k.wav"))
        val cleaned=render(0,clean,100)
        val cleanScore=score(clean,cleaned)
        assertTrue("Clean speech fidelity too low: ${cleanScore.first}",cleanScore.first>25)
        assertTrue("Clean speech volume reduced: ${cleanScore.second}",cleanScore.second>-.75)
        report.append("clean speech,Dialogue,Infinity,${cleanScore.first},${cleanScore.second}\n")
        val mixed=pcm(fixture.resolve("dialogue-music-48k.wav"))
        val enhanced=render(0,mixed,100)
        val baseline=score(clean,mixed).first;val actual=score(clean,enhanced)
        assertTrue("Dialogue separation improvement too low",actual.first-baseline>6)
        report.append("dialogue and real instrumental music,Dialogue,$baseline,${actual.first},${actual.second}\n")
        for (name in listOf("Al James - Schoolboy Facination","AM Contra - Heart Peripheral","Angels In Amplifiers - I'm Alright")) {
            val source=pcm(fixture.resolve("$name-mixture.wav"));val voice=pcm(fixture.resolve("$name-vocals.wav"))
            val output=render(1,source,100)
            val before=score(voice,source).first;val after=score(voice,output)
            assertTrue("Song separation improvement too low for $name: $after",after.first-before>6)
            assertTrue("Song vocal level reduced for $name",after.second > -2)
            report.append("\"$name\",Singing,$before,${after.first},${after.second}\n")
            wav(research.resolve("fixtures/$name-production.wav"),output,44100)
        }
        wav(research.resolve("fixtures/clean-speech-production.wav"),cleaned,48000)
        wav(research.resolve("fixtures/dialogue-music-production.wav"),enhanced,48000)
        research.resolve("production-quality-results.csv").writeText(report.toString())
    }

    private fun render(mode: Int, source: ByteArray, strength: Int): ByteArray {
        MusicRemovalProcessor.forFiles(models,mode).use { processor ->
            processor.setReduction(strength);processor.setVoiceBoost(0)
            val sink=ByteArrayOutputStream();var read=0;var first=true
            val delay=if(mode==0)1536*4 else 0
            while(sink.size()<source.size+delay) {
                val input=ByteArray((if(first)processor.firstInputFrames else processor.inputFrames)*4)
                val count=minOf(input.size,source.size-read).coerceAtLeast(0)
                if(count>0)source.copyInto(input,0,read,read+count)
                read+=count
                val output=ByteArray(processor.outputFrames*4)
                processor.process(input,first,output,0)
                sink.write(output);first=false
            }
            return sink.toByteArray().copyOfRange(delay,source.size+delay)
        }
    }

    private fun pcm(file: File): ByteArray {
        val all=file.readBytes();val buffer=ByteBuffer.wrap(all).order(ByteOrder.LITTLE_ENDIAN);var offset=12
        while(offset+8<=all.size){val size=buffer.getInt(offset+4);if(String(all,offset,4)=="data")return all.copyOfRange(offset+8,offset+8+size);offset+=8+size+(size and 1)}
        error("No PCM data in ${file.name}")
    }
    private fun score(reference: ByteArray, signal: ByteArray): Pair<Double,Double> {
        assertEquals(reference.size,signal.size)
        val r=ByteBuffer.wrap(reference).order(ByteOrder.LITTLE_ENDIAN).asShortBuffer()
        val y=ByteBuffer.wrap(signal).order(ByteOrder.LITTLE_ENDIAN).asShortBuffer()
        var rr=0.0;var yy=0.0;var ry=0.0
        // The file alignment is explicit; ignore the first second of model startup.
        val skip=if(reference.size==48000*4*10)48000*2 else 44100*2
        for(i in skip until r.limit()){val a=r.get(i).toDouble();val b=y.get(i).toDouble();rr+=a*a;yy+=b*b;ry+=a*b}
        val gain=ry/rr;val target=gain*gain*rr;val error=(yy-target).coerceAtLeast(1e-12)
        return (10*ln(target/error)/ln(10.0)) to (20*ln(kotlin.math.abs(gain))/ln(10.0))
    }
    private fun wav(file: File, data: ByteArray, rate: Int) {
        val b=ByteBuffer.allocate(44+data.size).order(ByteOrder.LITTLE_ENDIAN)
        b.put("RIFF".toByteArray()).putInt(36+data.size).put("WAVEfmt ".toByteArray()).putInt(16)
        b.putShort(1).putShort(2).putInt(rate).putInt(rate*4).putShort(4).putShort(16)
        b.put("data".toByteArray()).putInt(data.size).put(data);file.writeBytes(b.array())
    }
}
