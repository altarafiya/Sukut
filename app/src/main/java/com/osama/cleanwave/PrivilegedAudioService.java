package com.osama.cleanwave;

import android.content.Context;
import android.content.ContextWrapper;
import android.content.AttributionSource;
import android.annotation.SuppressLint;
import android.annotation.TargetApi;
import android.media.AudioAttributes;
import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioRecord;
import android.media.AudioRouting;
import android.media.AudioTrack;
import android.media.AudioTimestamp;
import android.media.AudioDeviceInfo;
import android.media.MediaRecorder;
import android.os.Process;
import android.os.Binder;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;

import org.lsposed.hiddenapibypass.HiddenApiBypass;

import java.util.concurrent.atomic.AtomicBoolean;

/**
 * Shizuku UserService. With Shizuku started through ADB this process runs as UID 2000
 * (shell), which AOSP grants MODIFY_AUDIO_ROUTING. That lets us register a dynamic
 * AudioPolicy and replace matching playback with a REMOTE_SUBMIX loopback.
 *
 * Full-band dialogue enhancement or buffered vocal separation, both in stereo.
 */
public final class PrivilegedAudioService extends ICleanWaveService.Stub {
    private static final int CHANNELS = 2;
    private static final int BYTES_PER_SAMPLE = 2;
    private static final int ROUTE_FLAG_LOOP_BACK = 0x2;
    private static final int RULE_MATCH_ATTRIBUTE_USAGE = 0x1;
    private static final int RULE_MATCH_UID = 0x4;

    private final Object lock = new Object();
    private final AtomicBoolean running = new AtomicBoolean(false);
    private Context context;
    private volatile String status = "Idle";
    private AudioManager audioManager;
    private Object audioPolicy;
    private Object audioMix;
    private AudioRecord audioRecord;
    private AudioTrack audioTrack;
    private IOutputRouteListener routeObserver;
    private ScreenRecordingRouter recordingRouter;
    private final AtomicBoolean recordingRoutingEnabled = new AtomicBoolean();
    private volatile BufferedAudioPipeline pipeline;
    private MusicRemovalProcessor processor;
    private boolean closing;
    private long generation;
    private volatile int musicReduction = 100;
    private volatile int processingMode = MusicRemovalProcessor.DIALOGUE;
    private volatile int voiceBoost = 0;
    private volatile int sourceVoiceDb = 0;
    private volatile int sourceMusicDb = -10;
    private volatile int sourceOtherDb = -20;
    private volatile int bufferingProfile = 0;
    private volatile int amplificationDb = 0;
    private volatile boolean silentRecovery = true;
    private volatile boolean effectsCompatibility = false;
    private volatile int sourceUid = -1;
    private int sampleRate = 48000;
    private int minimumOutputFrames;
    private int normalOutputFloorFrames;
    private int bluetoothOutputFloorFrames;
    private final AudioTimestamp captureTimestamp = new AudioTimestamp();
    private final AudioTimestamp playbackTimestamp = new AudioTimestamp();
    private final AudioRouting.OnRoutingChangedListener outputRouteListener = router -> {
        synchronized (lock) {
            if (router == audioTrack && running.get()) {
                configureOutputBuffer();
            }
        }
    };

    public PrivilegedAudioService() {
        // Shizuku < 13 fallback. CleanWave requires v13+ for the Context constructor.
    }

    public PrivilegedAudioService(Context context) {
        this.context = context;
    }

    @Override
    public boolean startPassthrough() {
        // AudioPolicy.checkCallingOrSelfPermission sees the controller's Binder
        // identity unless cleared, even though this process runs as shell.
        long identity = Binder.clearCallingIdentity();
        try {
            return startAsServiceIdentity();
        } finally {
            Binder.restoreCallingIdentity(identity);
        }
    }

    private boolean startAsServiceIdentity() {
        synchronized (lock) {
            if (running.get()) return true;
            if (closing || pipeline != null) {
                status = "Previous audio worker is stopping. Turn Off and On to restart.";
                return false;
            }
            if (context == null) {
                status = "Shizuku v13+ is required: privileged service Context is unavailable";
                return false;
            }

            try {
                HiddenApiBypass.setHiddenApiExemptions("");
                recordingRoutingEnabled.set(true);
                createRoutingPipeline();
                audioRecord.startRecording();
                final AudioRecord record = audioRecord;
                final AudioTrack track = audioTrack;
                final AtomicBoolean playbackStarted = new AtomicBoolean(false);
                final long current = ++generation;
                pipeline = new BufferedAudioPipeline(processor,
                        (data, offset, count) -> record.read(data, offset, count, AudioRecord.READ_BLOCKING),
                        (data, offset, count) -> {
                            synchronized (lock) {
                                if (!running.get() || current != generation) return -1;
                                if (playbackStarted.compareAndSet(false, true)) {
                                    track.play();
                                    configureOutputBuffer(track, record);
                                }
                            }
                            return track.write(data, offset, count, AudioTrack.WRITE_BLOCKING);
                        },
                        problem -> pipelineFailed(current, problem),
                        () -> Process.setThreadPriority(Process.THREAD_PRIORITY_AUDIO),
                        1, BuildConfig.ENGINE_RUNTIME);
                running.set(true);
                status = runningStatus();
                pipeline.start();
                return true;
            } catch (Throwable t) {
                status = "Routing failed: " + concise(t);
                releaseUnstartedPipeline();
                return false;
            }
        }
    }

    @Override
    public void stopProcessing() {
        long identity = Binder.clearCallingIdentity();
        try {
            stopAsServiceIdentity();
        } finally {
            Binder.restoreCallingIdentity(identity);
        }
    }

    private void stopAsServiceIdentity() {
        shutdown(true, -1);
    }

    @Override public void stopForRestart() {
        long identity=Binder.clearCallingIdentity();
        try{shutdown(true,-1,true);}finally{Binder.restoreCallingIdentity(identity);}
    }

    private void shutdown(boolean clearStatus, long expectedGeneration) {
        shutdown(clearStatus,expectedGeneration,false);
    }
    private void shutdown(boolean clearStatus, long expectedGeneration, boolean preserveRecordingRoute) {
        BufferedAudioPipeline stopped;
        ScreenRecordingRouter restoreRecording=null;
        boolean alreadyClosing;
        synchronized (lock) {
            // A delayed failure cleanup must never tear down a later route.
            if (expectedGeneration >= 0 && expectedGeneration != generation) return;
            // Explicit OFF must detach the recording hook even if an earlier
            // internal restart/failure is already waiting for inference to end.
            if(!preserveRecordingRoute){
                recordingRoutingEnabled.set(false);
                restoreRecording=recordingRouter;recordingRouter=null;
            }
            alreadyClosing=closing;
            if(!alreadyClosing){
            closing = true;
            running.set(false);
            stopped = pipeline;
            if (stopped != null) stopped.close();
            if (audioRecord != null) {
                try { audioRecord.stop(); } catch (Throwable ignored) { }
            }
            if (audioTrack != null) {
                try { audioTrack.pause(); } catch (Throwable ignored) { }
                try { audioTrack.flush(); } catch (Throwable ignored) { }
            }
            // Restore normal media immediately, before waiting for native inference.
            unregisterPolicy();
            }
            stopped=pipeline;
        }
        if(restoreRecording!=null)restoreRecording.close();
        if(alreadyClosing)return;
        boolean ended = true;
        if (stopped != null) {
            try { ended = stopped.awaitStopped(2500); }
            catch (InterruptedException e) { Thread.currentThread().interrupt(); ended = false; }
        }
        synchronized (lock) {
            releaseAudioObjects();
            // The inference thread closes its own model, never during session.run.
            if (stopped == null && processor != null) {
                try { processor.close(); } catch (Throwable ignored) { }
            }
            processor = null;
            pipeline = null;
            closing = !ended;
            if (!ended) status = "Audio worker is still stopping. Restart the Shizuku audio service.";
            else if (clearStatus) status = "Off";
        }
    }

    @Override
    public boolean isRunning() {
        BufferedAudioPipeline current = pipeline;
        return running.get() && current != null && current.isActive();
    }

    @Override
    public String getStatus() {
        MusicRemovalProcessor current = processor;
        if (running.get() && current != null) {
            return current.isBypassed()
                    ? "AI temporarily bypassed while the device is busy; automatic recovery is active."
                    : runningStatus();
        }
        return status;
    }

    @Override
    public String getIdentity() {
        return "pid=" + Process.myPid() + ", uid=" + Process.myUid();
    }

    @Override
    public void setMusicReduction(int percent) {
        musicReduction = Math.max(0, Math.min(100, percent));
        synchronized (lock) {
            if (processor != null) processor.setReduction(musicReduction);
        }
    }

    @Override public void setProcessingMode(int mode) {
        if (running.get()) throw new IllegalStateException("Stop audio before changing voice type");
        processingMode = mode == MusicRemovalProcessor.SINGING ? MusicRemovalProcessor.SINGING : MusicRemovalProcessor.DIALOGUE;
    }

    @Override public void setVoiceBoost(int db) {
        voiceBoost = Math.max(0, Math.min(6, db));
        synchronized (lock) { if (processor != null) processor.setVoiceBoost(voiceBoost); }
    }

    @Override public void setSourceMix(int voiceDb, int musicDb, int otherDb) {
        sourceVoiceDb = Math.max(-100, Math.min(10, voiceDb));
        sourceMusicDb = Math.max(-100, Math.min(10, musicDb));
        sourceOtherDb = Math.max(-100, Math.min(10, otherDb));
        synchronized (lock) {
            if (processor != null) processor.setSourceMix(sourceVoiceDb, sourceMusicDb, sourceOtherDb);
        }
    }

    @Override public void setPlaybackOptions(int buffering, int amplification, boolean silent, boolean effects, int uid) {
        synchronized (lock) {
            if (uid < -1 || uid == Process.myUid()) throw new IllegalArgumentException("Selected source app is unavailable; choose it again in Advanced settings");
            int profile = Math.max(0, Math.min(2, buffering));
            if (running.get() && (profile != bufferingProfile || effects != effectsCompatibility || uid != sourceUid))
                throw new IllegalStateException("Stop audio before changing buffering or effects compatibility");
            bufferingProfile = profile;
            amplificationDb = Math.max(0, Math.min(12, amplification));
            silentRecovery = silent;
            effectsCompatibility = effects;
            sourceUid = uid;
            if (processor != null) {
                processor.setAmplification(amplificationDb);
                processor.setSilentRecovery(silentRecovery);
                processor.setBuffering(bufferingProfile);
            }
        }
    }

    @Override
    public String getProcessingMetrics() {
        synchronized (lock) {
            if(recordingRouter!=null && recordingRoutingEnabled.get())recordingRouter.requestRefresh();
            if (processor == null) return "AI engine is stopped";
            status = processor.isBypassed()
                    ? "AI temporarily bypassed while the device is busy; automatic recovery is active."
                    : runningStatus();
            return processor.metrics() + (audioTrack == null ? "" : "; output_underruns=" + audioTrack.getUnderrunCount()
                    + "; queue_frames=" + audioTrack.getBufferSizeInFrames())
                    + (audioTrack == null ? "" : "; output_capacity_frames=" + audioTrack.getBufferCapacityInFrames()
                    + "; output_performance_mode=" + audioTrack.getPerformanceMode())
                    + (audioRecord == null ? "" : "; capture_capacity_frames=" + audioRecord.getBufferSizeInFrames())
                    + (pipeline == null ? "" : pipeline.queueMetrics())
                    + "; buffering=" + bufferingProfile + "; amplification_db=" + amplificationDb
                    + "; silent_recovery=" + silentRecovery + "; effects_compatibility=" + effectsCompatibility
                    + "; recording_capture=" + (recordingRouter==null?"idle":recordingRouter.status())
                    + timingMetrics();
        }
    }

    // Sampled only by the low-frequency health poll, never in the audio hot path.
    // This measures the capture/render timeline gap, not acoustic/Bluetooth latency.
    private String timingMetrics() {
        if (audioRecord == null || audioTrack == null) return "";
        try {
            if (audioRecord.getTimestamp(captureTimestamp, AudioTimestamp.TIMEBASE_MONOTONIC) == AudioRecord.SUCCESS
                    && audioTrack.getTimestamp(playbackTimestamp)) {
                double gap = (captureTimestamp.framePosition - playbackTimestamp.framePosition) * 1000.0 / sampleRate
                        + (playbackTimestamp.nanoTime - captureTimestamp.nanoTime) / 1e6;
                return "; capture_render_gap_ms=" + gap;
            }
        } catch (RuntimeException ignored) { }
        return "; capture_render_gap_ms=unavailable";
    }

    private String runningStatus() {
        if (BuildConfig.ENGINE_RUNTIME) return "audio separation is active";
        return processingMode == MusicRemovalProcessor.SINGING
                ? "Singing music removal is running (buffered)" : "Dialogue music removal is running";
    }

    @Override
    public void destroy() {
        stopProcessing();
        System.exit(0);
    }

    // This method runs in Shizuku's shell/root process, which owns RECORD_AUDIO
    // and MODIFY_AUDIO_ROUTING. The controller APK never records directly.
    @SuppressLint("MissingPermission")
    private void createRoutingPipeline() throws Exception {
        // Fail before redirecting media if the local model/runtime cannot initialise.
        int previousPriority = Process.getThreadPriority(Process.myTid());
        try {
            // Native ONNX worker threads inherit the creator's scheduling priority.
            // Match the phone benchmark, then restore this Binder thread's priority.
            Process.setThreadPriority(Process.THREAD_PRIORITY_AUDIO);
            processor = new MusicRemovalProcessor(context, processingMode);
        } finally { Process.setThreadPriority(previousPriority); }
        processor.setReduction(musicReduction);
        processor.setVoiceBoost(voiceBoost);
        processor.setSourceMix(sourceVoiceDb, sourceMusicDb, sourceOtherDb);
        processor.setAmplification(amplificationDb);
        processor.setSilentRecovery(silentRecovery);
        processor.setBuffering(bufferingProfile);
        sampleRate = processor.getSampleRate();
        // app_process defaults to package "android", which Android 16 rejects
        // as audio attribution for UID 2000. Use the actual shell package.
        Context audioContext = Process.myUid() == 2000
                ? context.createPackageContext("com.android.shell", 0) : context;
        if (Process.myUid() == 2000 && Build.VERSION.SDK_INT >= 31) {
            audioContext = shellAudioContext(audioContext);
        }
        final AudioFormat format = new AudioFormat.Builder()
                .setSampleRate(sampleRate)
                .setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                .setChannelMask(AudioFormat.CHANNEL_OUT_STEREO)
                .build();

        Class<?> ruleBuilderClass = Class.forName("android.media.audiopolicy.AudioMixingRule$Builder");
        Object ruleBuilder = HiddenApiBypass.newInstance(ruleBuilderClass);

        addUsage(ruleBuilderClass, ruleBuilder, AudioAttributes.USAGE_MEDIA);
        addUsage(ruleBuilderClass, ruleBuilder, AudioAttributes.USAGE_GAME);
        addUsage(ruleBuilderClass, ruleBuilder, AudioAttributes.USAGE_UNKNOWN);
        if (sourceUid >= 0) HiddenApiBypass.invoke(ruleBuilderClass, ruleBuilder,
                "addMixRule", RULE_MATCH_UID, Integer.valueOf(sourceUid));

        // Our re-rendered AudioTrack runs as shell too. Excluding UID 2000 prevents feedback.
        HiddenApiBypass.invoke(
                ruleBuilderClass,
                ruleBuilder,
                "excludeMixRule",
                RULE_MATCH_UID,
                Integer.valueOf(Process.myUid())
        );
        Object rule = HiddenApiBypass.invoke(ruleBuilderClass, ruleBuilder, "build");

        Class<?> mixBuilderClass = Class.forName("android.media.audiopolicy.AudioMix$Builder");
        Object mixBuilder = HiddenApiBypass.newInstance(mixBuilderClass, rule);
        HiddenApiBypass.invoke(mixBuilderClass, mixBuilder, "setFormat", format);
        HiddenApiBypass.invoke(mixBuilderClass, mixBuilder, "setRouteFlags", ROUTE_FLAG_LOOP_BACK);
        audioMix = HiddenApiBypass.invoke(mixBuilderClass, mixBuilder, "build");

        Class<?> policyBuilderClass = Class.forName("android.media.audiopolicy.AudioPolicy$Builder");
        Object policyBuilder = HiddenApiBypass.newInstance(policyBuilderClass, audioContext);
        HiddenApiBypass.invoke(policyBuilderClass, policyBuilder, "addMix", audioMix);
        audioPolicy = HiddenApiBypass.invoke(policyBuilderClass, policyBuilder, "build");

        audioManager = (AudioManager) audioContext.getSystemService(Context.AUDIO_SERVICE);
        if (audioManager == null) throw new IllegalStateException("AudioManager unavailable in UserService");

        Object registerResult = HiddenApiBypass.invoke(
                AudioManager.class,
                audioManager,
                "registerAudioPolicy",
                audioPolicy
        );
        if (!(registerResult instanceof Integer) || ((Integer) registerResult) != 0) {
            throw new IllegalStateException("registerAudioPolicy returned " + registerResult);
        }

        Class<?> policyClass = Class.forName("android.media.audiopolicy.AudioPolicy");
        // Equivalent to AOSP AudioPolicy.createAudioRecordSink, with an explicit
        // Context so AudioFlinger validates com.android.shell against UID 2000.
        String addressTag = (String) HiddenApiBypass.invoke(policyClass, null, "addressForTag", audioMix);
        AudioAttributes.Builder captureAttributes = new AudioAttributes.Builder();
        HiddenApiBypass.invoke(AudioAttributes.Builder.class, captureAttributes,
                "setInternalCapturePreset", MediaRecorder.AudioSource.REMOTE_SUBMIX);
        HiddenApiBypass.invoke(AudioAttributes.Builder.class, captureAttributes, "addTag", addressTag);
        // Do not request fixedVolume: AudioRecord would force STREAM_MUSIC to
        // maximum for the entire submix while recording. Respect media volume.
        AudioFormat captureFormat = new AudioFormat.Builder(format)
                .setChannelMask(AudioFormat.CHANNEL_IN_STEREO).build();
        int minRecord = AudioRecord.getMinBufferSize(sampleRate,
                AudioFormat.CHANNEL_IN_STEREO, AudioFormat.ENCODING_PCM_16BIT);
        if (minRecord <= 0) throw new IllegalStateException("AudioRecord buffer size=" + minRecord);
        AudioRecord.Builder recordBuilder = new AudioRecord.Builder()
                .setAudioFormat(captureFormat).setBufferSizeInBytes(Math.max(minRecord,
                        BuildConfig.ENGINE_RUNTIME ? processor.getFirstInputFrames() * 4 : sampleRate / 5 * CHANNELS * BYTES_PER_SAMPLE));
        if (Build.VERSION.SDK_INT >= 31) {
            HiddenApiBypass.invoke(AudioRecord.Builder.class, recordBuilder, "setContext", audioContext);
        }
        HiddenApiBypass.invoke(AudioRecord.Builder.class, recordBuilder,
                "setAudioAttributes", captureAttributes.build());
        audioRecord = recordBuilder.build();
        if (audioRecord.getState() != AudioRecord.STATE_INITIALIZED) {
            throw new IllegalStateException("AudioRecord is not initialized");
        }

        int minTrack = AudioTrack.getMinBufferSize(
                sampleRate,
                AudioFormat.CHANNEL_OUT_STEREO,
                AudioFormat.ENCODING_PCM_16BIT
        );
        int requested = Math.max(minTrack * 2, outputBufferFrames() * CHANNELS * BYTES_PER_SAMPLE * 2);
        if (BuildConfig.ENGINE_RUNTIME) {
            if (minTrack <= 0) throw new IllegalStateException("AudioTrack minimum buffer=" + minTrack);
            // Capacity need not duplicate the active reserve. Keep enough space for
            // the same normal-mixer floor and Bluetooth floor applied after routing.
            // Active buffering, effects, format and PCM are unchanged.
            int normalFloor = minTrack / (CHANNELS * BYTES_PER_SAMPLE * 2) + processor.getOutputFrames();
            requested = Math.max(minTrack, Math.max(outputBufferFrames(), normalFloor) * CHANNELS * BYTES_PER_SAMPLE);
        }
        AudioAttributes.Builder playbackAttributes = new AudioAttributes.Builder()
                .setUsage(AudioAttributes.USAGE_MEDIA)
                .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC);
        if (Build.VERSION.SDK_INT >= 32) {
            playbackAttributes.setSpatializationBehavior(AudioAttributes.SPATIALIZATION_BEHAVIOR_AUTO);
        }
        AudioTrack.Builder trackBuilder = new AudioTrack.Builder()
                .setAudioAttributes(playbackAttributes.build())
                .setAudioFormat(format)
                .setTransferMode(AudioTrack.MODE_STREAM)
                .setBufferSizeInBytes(requested)
                // Standard media is opt-in: some OEM effects bypass fast tracks.
                // Keep the previous low-latency request for the default profile.
                .setPerformanceMode(effectsCompatibility ? AudioTrack.PERFORMANCE_MODE_NONE : AudioTrack.PERFORMANCE_MODE_LOW_LATENCY);
        if (Build.VERSION.SDK_INT >= 31) {
            HiddenApiBypass.invoke(AudioTrack.Builder.class, trackBuilder, "setContext", audioContext);
        }
        audioTrack = trackBuilder.build();
        // A normal effects-capable mixer can consume bursts longer than 32 ms.
        // Use a conservative device-minimum-based floor plus one inference hop.
        // Fast tracks retain the smaller floor; routing changes reapply the size.
        normalOutputFloorFrames = BuildConfig.ENGINE_RUNTIME
                && audioTrack.getPerformanceMode() != AudioTrack.PERFORMANCE_MODE_LOW_LATENCY
                ? minTrack / (CHANNELS * BYTES_PER_SAMPLE * 2) + processor.getOutputFrames() : 0;
        bluetoothOutputFloorFrames = Math.max(normalOutputFloorFrames, minTrack / (CHANNELS * BYTES_PER_SAMPLE));
        minimumOutputFrames = normalOutputFloorFrames;
        configureOutputBuffer();
        audioTrack.addOnRoutingChangedListener(outputRouteListener,
                new Handler(Looper.getMainLooper()));
        if(recordingRouter==null)try{recordingRouter=new ScreenRecordingRouter(audioManager,recordingRoutingEnabled::get);}
        catch(RuntimeException unavailable){Log.w("SukutRecording","Playback capture compatibility unavailable",unavailable);}
    }

    private void configureOutputBuffer() {
        if (audioTrack != null && audioRecord != null) configureOutputBuffer(audioTrack, audioRecord);
    }

    @Override public void setOutputRouteListener(IOutputRouteListener listener) {
        synchronized (lock) {
            routeObserver = listener;
            reportOutputRoute();
        }
    }

    private void reportOutputRoute() {
        if (routeObserver == null || audioTrack == null) return;
        AudioDeviceInfo device = audioTrack.getRoutedDevice();
        // A rebuilding track is temporarily unrouted; this is not a disconnect.
        if (device == null || device.getType() == AudioDeviceInfo.TYPE_UNKNOWN) return;
        try { routeObserver.onOutputRouteChanged(device.getId(), device.getType()); }
        catch (android.os.RemoteException unavailable) { routeObserver = null; }
    }

    private int outputBufferFrames() {
        if (BuildConfig.ENGINE_RUNTIME) return Math.max(minimumOutputFrames, 1536 + bufferingProfile * 1920);
        return processingMode == MusicRemovalProcessor.SINGING ? sampleRate / 5 : 1536;
    }

    private void configureOutputBuffer(AudioTrack track, AudioRecord record) {
        if (BuildConfig.ENGINE_RUNTIME) {
            AudioDeviceInfo device = track.getRoutedDevice();
            int type = device == null ? -1 : device.getType();
            boolean bluetooth = type == AudioDeviceInfo.TYPE_BLUETOOTH_A2DP || type == AudioDeviceInfo.TYPE_BLUETOOTH_SCO
                    || type == AudioDeviceInfo.TYPE_HEARING_AID
                    || (Build.VERSION.SDK_INT >= 31 && (type == AudioDeviceInfo.TYPE_BLE_HEADSET || type == AudioDeviceInfo.TYPE_BLE_SPEAKER));
            minimumOutputFrames = bluetooth ? bluetoothOutputFloorFrames : normalOutputFloorFrames;
        }
        int outputFrames = track.setBufferSizeInFrames(outputBufferFrames());
        if (outputFrames <= 0) {
            throw new IllegalStateException("AudioTrack buffer resize=" + outputFrames);
        }
        if (Build.VERSION.SDK_INT >= 31) track.setStartThresholdInFrames(Math.min(outputFrames, outputBufferFrames()));
        Log.i("CleanWaveAudio", "Buffers: record=" + record.getBufferSizeInFrames()
                + " frames, output=" + outputFrames + " frames, capacity="
                + track.getBufferCapacityInFrames() + " frames");
        reportOutputRoute();
    }

    @TargetApi(31)
    private static Context shellAudioContext(Context base) {
        // createPackageContext inherits the parent app_process op package
        // ("android"). Supply the real calling process and its UID-owned package.
        AttributionSource.Builder sourceBuilder = new AttributionSource.Builder(Process.myUid())
                .setPackageName("com.android.shell");
        if (Build.VERSION.SDK_INT >= 34) sourceBuilder.setPid(Process.myPid());
        AttributionSource source = sourceBuilder.build();
        return new ContextWrapper(base) {
            @Override
            public AttributionSource getAttributionSource() {
                return source;
            }

            @Override
            public String getOpPackageName() {
                return "com.android.shell";
            }
        };
    }

    private static void addUsage(Class<?> builderClass, Object builder, int usage) throws Exception {
        AudioAttributes attributes = new AudioAttributes.Builder().setUsage(usage).build();
        HiddenApiBypass.invoke(
                builderClass,
                builder,
                "addRule",
                attributes,
                RULE_MATCH_ATTRIBUTE_USAGE
        );
    }

    private void pipelineFailed(long current, Throwable problem) {
        synchronized (lock) {
            if (current != generation || closing) return;
            status = "Audio route stopped: " + concise(problem);
            running.set(false);
        }
        // Cleanup runs separately so inference can finish and close its own model.
        new Thread(() -> shutdown(false, current), "CleanWave-Cleanup").start();
    }

    private void releaseUnstartedPipeline() {
        running.set(false);
        recordingRoutingEnabled.set(false);
        if(recordingRouter!=null){recordingRouter.close();recordingRouter=null;}
        BufferedAudioPipeline abandoned = pipeline;
        if (abandoned != null) abandoned.close();
        unregisterPolicy();
        releaseAudioObjects();
        if (abandoned == null && processor != null) {
            try { processor.close(); } catch (Throwable ignored) { }
        }
        processor = null;
        pipeline = null;
    }

    private void releaseAudioObjects() {
        if (audioRecord != null) {
            try { audioRecord.release(); } catch (Throwable ignored) { }
            audioRecord = null;
        }
        if (audioTrack != null) {
            try { audioTrack.removeOnRoutingChangedListener(outputRouteListener); } catch (Throwable ignored) { }
            try { audioTrack.release(); } catch (Throwable ignored) { }
            audioTrack = null;
        }
    }

    private void unregisterPolicy() {
        if (audioManager != null && audioPolicy != null) {
            try {
                HiddenApiBypass.invoke(AudioManager.class, audioManager, "unregisterAudioPolicy", audioPolicy);
            } catch (Throwable ignored) { }
        }
        audioManager = null;
        audioPolicy = null;
        audioMix = null;
    }

    private static String concise(Throwable t) {
        String message = t.getMessage();
        return t.getClass().getSimpleName() + (message == null || message.isEmpty() ? "" : ": " + message);
    }
}
