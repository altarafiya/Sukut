package com.osama.cleanwave;

import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;

/** Capture, inference and playback progress independently, with bounded queues. */
final class BufferedAudioPipeline implements AutoCloseable {
    interface Input { int read(byte[] block, int offset, int count) throws Exception; }
    interface Output { int write(byte[] block, int offset, int count) throws Exception; }
    interface Failure { void failed(Throwable problem); }
    private final AudioCleaner cleaner;
    private final Input input;
    private final Output output;
    private final Failure failure;
    private final Runnable priority;
    private final AtomicBoolean active = new AtomicBoolean(false);
    private final ArrayBlockingQueue<byte[]> captured = new ArrayBlockingQueue<>(2);
    private final ArrayBlockingQueue<byte[]> rendered;
    private final ArrayBlockingQueue<byte[]> inputPool = new ArrayBlockingQueue<>(4);
    private final ArrayBlockingQueue<byte[]> outputPool;
    private final int reservePackets;
    private final boolean directOutput;
    private Thread captureThread, processThread, playbackThread;

    BufferedAudioPipeline(AudioCleaner cleaner, Input input, Output output, Failure failure, Runnable priority) {
        this(cleaner,input,output,failure,priority,1);
    }
    BufferedAudioPipeline(AudioCleaner cleaner, Input input, Output output, Failure failure, Runnable priority, int reservePackets) {
        this(cleaner,input,output,failure,priority,reservePackets,false);
    }
    BufferedAudioPipeline(AudioCleaner cleaner, Input input, Output output, Failure failure, Runnable priority, int reservePackets, boolean directOutput) {
        this.cleaner=cleaner; this.input=input; this.output=output; this.failure=failure; this.priority=priority;
        this.directOutput=directOutput;
        this.reservePackets=Math.max(cleaner.getOutputFrames()>10000?2:1,Math.min(5,reservePackets));
        int renderCapacity=Math.max(2,this.reservePackets);
        rendered=directOutput?null:new ArrayBlockingQueue<>(renderCapacity);
        outputPool=directOutput?null:new ArrayBlockingQueue<>(renderCapacity+2);
        for(int i=0;i<4;i++) {
            inputPool.add(new byte[cleaner.getInputFrames()*4]);
        }
        for(int i=0;!directOutput&&i<renderCapacity+2;i++) {
            outputPool.add(new byte[cleaner.getOutputFrames()*4]);
        }
    }

    void start() {
        if(!active.compareAndSet(false,true)) throw new IllegalStateException("Pipeline already started");
        captureThread=thread("CleanWave-Capture",this::capture);
        processThread=thread("CleanWave-Inference",this::process);
        if(!directOutput) playbackThread=thread("CleanWave-Playback",this::playback);
        // Start consumers first so the producer cannot fill the bounded capture queue
        // before inference/playback have had a chance to block waiting for their first packet.
        // This avoids a false startup-overload signal on otherwise fast real-time models.
        processThread.start(); if(playbackThread!=null) playbackThread.start(); captureThread.start();
    }

    private Thread thread(String name, Work work) {
        return new Thread(() -> {
            try { priority.run(); work.run(); }
            catch(InterruptedException problem) {
                Thread.currentThread().interrupt();
                if(active.compareAndSet(true,false)) failure.failed(problem);
            }
            catch(Throwable t) { if(active.compareAndSet(true,false)) failure.failed(t); }
            finally { if(name.equals("CleanWave-Inference")) cleaner.close(); }
        },name);
    }
    private interface Work { void run() throws Exception; }

    private void capture() throws Exception {
        boolean first=true;
        while(active.get()) {
            byte[] packet=first ? new byte[cleaner.getFirstInputFrames()*4] : inputPool.take();
            int offset=0, empty=0;
            while(offset<packet.length && active.get()) {
                int read=input.read(packet,offset,packet.length-offset);
                if(read<0 || read>packet.length-offset) throw new IllegalStateException("Audio capture read="+read);
                if(read==0) { if(++empty>100) throw new IllegalStateException("Audio capture stopped producing data"); Thread.sleep(2); }
                else { offset+=read; empty=0; }
            }
            if(!active.get()) break;
            if(!captured.offer(packet,1,TimeUnit.SECONDS)) throw new IllegalStateException("AI capture queue saturated");
            first=false;
        }
    }

    private void process() throws Exception {
        boolean first=true;
        byte[] directPacket=directOutput?new byte[cleaner.getOutputFrames()*4]:null;
        while(active.get()) {
            byte[] packet=captured.poll(100,TimeUnit.MILLISECONDS);
            if(packet==null) continue;
            byte[] clean=directOutput?directPacket:outputPool.take();
            cleaner.process(packet,first,clean,captured.size());
            if(!first) inputPool.put(packet);
            if(!active.get()) break;
            if(directOutput) writePacket(clean);
            else if(!rendered.offer(clean,1,TimeUnit.SECONDS)) throw new IllegalStateException("Audio playback queue saturated");
            first=false;
        }
    }

    private void playback() throws Exception {
        // Audio keeps its reserve in AudioTrack instead of duplicating it here.
        // Legacy long-window cleaners still need at least two packets.
        byte[][] reserve=new byte[reservePackets][];
        for(int i=0;i<reserve.length;i++) reserve[i]=rendered.take();
        for(byte[] packet:reserve) if(active.get()) writePacket(packet);
        while(active.get()) {
            byte[] packet=rendered.poll(100,TimeUnit.MILLISECONDS);
            if(packet==null) continue;
            writePacket(packet);
        }
    }

    private void writePacket(byte[] packet) throws Exception {
        int offset=0, empty=0;
        while(offset<packet.length && active.get()) {
            int written=output.write(packet,offset,packet.length-offset);
            if(written<0 || written>packet.length-offset) throw new IllegalStateException("Audio playback write="+written);
            if(written==0) { if(++empty>100) throw new IllegalStateException("Audio playback stopped consuming data"); Thread.sleep(2); }
            else { offset+=written; empty=0; }
        }
        if(active.get()&&outputPool!=null) outputPool.put(packet);
    }

    boolean isActive() { return active.get(); }
    String queueMetrics() { return "; captured_packets="+captured.size()+"; rendered_packets="+(rendered==null?0:rendered.size())
            +"; playback_worker="+(directOutput?"inline":"separate"); }

    /** Stop native reads/writes before awaiting termination in the service. */
    @Override public void close() {
        active.set(false);
        for(Thread t:new Thread[]{captureThread,processThread,playbackThread}) if(t!=null) t.interrupt();
    }
    boolean awaitStopped(long milliseconds) throws InterruptedException {
        long deadline=System.nanoTime()+milliseconds*1_000_000;
        for(Thread t:new Thread[]{captureThread,processThread,playbackThread}) {
            if(t!=null && t!=Thread.currentThread()) {
                long left=deadline-System.nanoTime();
                if(left>0) t.join(Math.max(1,left/1_000_000));
            }
        }
        return (captureThread==null||!captureThread.isAlive()) &&
            (processThread==null||!processThread.isAlive()) && (playbackThread==null||!playbackThread.isAlive());
    }
}
