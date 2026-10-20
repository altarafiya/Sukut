package com.osama.cleanwave;

import org.junit.Test;
import static org.junit.Assert.*;
import java.util.concurrent.*;
import java.util.concurrent.atomic.*;

public class BufferedAudioPipelineTest {
    @Test public void inlinePlaybackPreservesEveryByteAcrossPartialReadsAndWrites() throws Exception {
        FakeCleaner cleaner=new FakeCleaner();AtomicInteger source=new AtomicInteger(),reads=new AtomicInteger();
        java.io.ByteArrayOutputStream sink=new java.io.ByteArrayOutputStream();
        AtomicReference<Throwable> error=new AtomicReference<>();
        BufferedAudioPipeline p=new BufferedAudioPipeline(cleaner,(b,o,n)->{
            if(reads.incrementAndGet()%4==0)return 0;int count=Math.min(6,n);
            for(int i=0;i<count;i++)b[o+i]=(byte)source.getAndIncrement();return count;
        },(b,o,n)->{int count=Math.min(7,n);synchronized(sink){sink.write(b,o,count);}return count;},error::set,()->{},1,true);
        p.start();
        try {
            long until=System.nanoTime()+TimeUnit.SECONDS.toNanos(5);
            while(sink.size()<512&&System.nanoTime()<until)Thread.sleep(2);
            assertTrue("Only "+sink.size()+" bytes delivered",sink.size()>=512);assertNull(error.get());
        } finally {p.close();assertTrue(p.awaitStopped(2000));}
        byte[] bytes=sink.toByteArray();for(int i=0;i<bytes.length;i++)assertEquals("PCM reordered at "+i,(byte)i,bytes[i]);
        assertTrue(cleaner.closed);
    }
    private static final class FakeCleaner implements AudioCleaner {
        volatile boolean closed; final AtomicInteger processed=new AtomicInteger(); boolean fail;
        public int getSampleRate(){return 48000;} public int getInputFrames(){return 8;}
        public int getFirstInputFrames(){return 8;} public int getOutputFrames(){return 8;}
        public void process(byte[] in,boolean first,byte[] out,int backlog){
            if(fail&&processed.incrementAndGet()==4)throw new IllegalStateException("deliberate model failure");
            if(!fail)processed.incrementAndGet();
            System.arraycopy(in,0,out,0,in.length);
        }
        public void setReduction(int n){} public void setVoiceBoost(int n){}
        public void setSourceMix(int voiceDb,int musicDb,int otherDb){}
        public void setAmplification(int db){} public void setSilentRecovery(boolean silent){}
        public void setBuffering(int profile){}
        public boolean isBypassed(){return false;} public String metrics(){return "fake";}
        public void close(){closed=true;}
    }

    @Test public void captureAndInferenceContinueWhilePlaybackIsBlocked() throws Exception {
        FakeCleaner cleaner=new FakeCleaner();AtomicInteger source=new AtomicInteger();
        AtomicInteger packets=new AtomicInteger();CountDownLatch firstWrite=new CountDownLatch(1),release=new CountDownLatch(1);
        AtomicReference<Throwable> error=new AtomicReference<>();
        BufferedAudioPipeline p=new BufferedAudioPipeline(cleaner,(b,o,n)->{
            Thread.sleep(1);int count=Math.min(n,8);for(int i=0;i<count;i++)b[o+i]=(byte)source.getAndIncrement();return count;
        },(b,o,n)->{firstWrite.countDown();release.await();packets.incrementAndGet();return n;},error::set,()->{});
        p.start();
        try {
            assertTrue(firstWrite.await(2,TimeUnit.SECONDS));
            long deadline=System.nanoTime()+TimeUnit.SECONDS.toNanos(2);
            while(cleaner.processed.get()<3&&System.nanoTime()<deadline)Thread.sleep(2);
            assertTrue("Inference stopped behind playback",cleaner.processed.get()>=3);
            release.countDown();
            deadline=System.nanoTime()+TimeUnit.SECONDS.toNanos(2);
            while(packets.get()<4&&System.nanoTime()<deadline)Thread.sleep(2);
            assertTrue(packets.get()>=4);assertNull(error.get());
        } finally {release.countDown();p.close();assertTrue(p.awaitStopped(2000));}
        assertTrue(cleaner.closed);
    }

    @Test public void modelFailureStopsAllWorkersAndClosesModel() throws Exception {
        FakeCleaner cleaner=new FakeCleaner();cleaner.fail=true;CountDownLatch failed=new CountDownLatch(1);
        AtomicInteger failures=new AtomicInteger();AtomicReference<BufferedAudioPipeline> owner=new AtomicReference<>();
        BufferedAudioPipeline p=new BufferedAudioPipeline(cleaner,(b,o,n)->{Thread.sleep(1);return n;},(b,o,n)->n,
            t->{failures.incrementAndGet();owner.get().close();failed.countDown();},()->{});
        owner.set(p);p.start();
        assertTrue(failed.await(2,TimeUnit.SECONDS));assertTrue(p.awaitStopped(2000));
        assertFalse(p.isActive());assertTrue(cleaner.closed);assertEquals(1,failures.get());
    }
}
