package com.osama.cleanwave;

import android.media.*;
import android.os.*;
import android.util.Log;
import java.io.*;
import java.lang.reflect.*;
import java.util.*;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.function.BooleanSupplier;

/** Control-plane capture routing. Never reads audio/video or changes the DSP/output buffers. */
final class ScreenRecordingRouter implements AutoCloseable {
    private final AudioManager manager;
    private final BooleanSupplier processingEnabled;
    private final HandlerThread thread = new HandlerThread("Sukut-RecordingRoute", android.os.Process.THREAD_PRIORITY_BACKGROUND);
    private final Handler handler;
    private final Map<String,Owned> owned = new LinkedHashMap<>();
    private volatile String status="idle";
    private volatile boolean closed;
    private final AtomicBoolean pending = new AtomicBoolean();
    private Object nativeCallback, previousCallback;
    private boolean publicCallbackRegistered;
    private final Runnable refresh=this::refresh;
    private final AudioManager.AudioRecordingCallback callback=new AudioManager.AudioRecordingCallback() {
        @Override public void onRecordingConfigChanged(List<AudioRecordingConfiguration> configs) {
            requestRefresh();
        }
    };
    private static final class Owned {
        final Object mix, original;
        final RecordingCapturePolicy.Snapshot snapshot;
        Owned(Object mix,Object original,RecordingCapturePolicy.Snapshot snapshot){this.mix=mix;this.original=original;this.snapshot=snapshot;}
    }
    ScreenRecordingRouter(AudioManager manager, BooleanSupplier processingEnabled) {
        this.manager=manager; this.processingEnabled=processingEnabled;
        thread.start(); handler=new Handler(thread.getLooper());
        // REMOTE_SUBMIX is deliberately absent from the public recording callback.
        // Listen before AudioService filters system-only sources, preserving any listener.
        try { registerNativeCallback(); }
        catch(Exception unavailable) {
            try{manager.registerAudioRecordingCallback(callback,handler);publicCallbackRegistered=true;}
            catch(RuntimeException ignored){ /* The existing health poll also requests refresh. */ }
        }
        requestRefresh();
    }
    String status(){return status;}
    void requestRefresh() {
        if(!closed && pending.compareAndSet(false,true))handler.post(refresh);
    }

    private void registerNativeCallback() throws Exception {
        Class<?> system=Class.forName("android.media.AudioSystem");
        Class<?> type=Class.forName("android.media.AudioSystem$AudioRecordingCallback");
        Field field=system.getDeclaredField("sRecordingCallback");field.setAccessible(true);
        synchronized(system) {
            previousCallback=field.get(null);
            nativeCallback=Proxy.newProxyInstance(type.getClassLoader(),new Class<?>[]{type},(proxy,method,args)->{
                if(method.getName().equals("onRecordingConfigurationChanged"))requestRefresh();
                if(previousCallback!=null)return method.invoke(previousCallback,args);
                if(method.getName().equals("hashCode"))return System.identityHashCode(proxy);
                if(method.getName().equals("equals"))return proxy==args[0];
                if(method.getName().equals("toString"))return "Sukut recording route listener";
                return null;
            });
            call(system,null,"setRecordingCallback",new Class<?>[]{type},nativeCallback);
        }
    }
    private void unregisterCallbacks() {
        if(publicCallbackRegistered)try{manager.unregisterAudioRecordingCallback(callback);}catch(RuntimeException ignored){}
        if(nativeCallback!=null)try {
            Class<?> system=Class.forName("android.media.AudioSystem"), type=Class.forName("android.media.AudioSystem$AudioRecordingCallback");
            Field field=system.getDeclaredField("sRecordingCallback");field.setAccessible(true);
            synchronized(system) {
                if(field.get(null)==nativeCallback)call(system,null,"setRecordingCallback",new Class<?>[]{type},previousCallback);
            }
        }catch(Exception unavailable){Log.w("SukutRecording","Recording listener could not be restored");}
    }

    private static Object call(Class<?> type,Object receiver,String name,Class<?>[] signature,Object...args) throws Exception {
        Method method=type.getDeclaredMethod(name,signature);method.setAccessible(true);return method.invoke(receiver,args);
    }
    private static List<Object> mixes() throws Exception {
        List<Object> mixes=new ArrayList<>();
        int result=(Integer)call(Class.forName("android.media.AudioSystem"),null,"getRegisteredPolicyMixes",new Class<?>[]{List.class},mixes);
        if(result!=0)throw new IOException("Playback capture enumeration unavailable: "+result);
        return mixes;
    }
    private static String address(Object mix)throws Exception {
        return (String)call(mix.getClass(),mix,"getRegistration",new Class<?>[]{});
    }
    private static Object rule(RecordingCapturePolicy.Snapshot snapshot, boolean processedOnly)throws Exception {
        Class<?> builder=Class.forName("android.media.audiopolicy.AudioMixingRule$Builder");
        Object value=builder.getConstructor().newInstance();
        for(int usage:snapshot.usages)call(builder,value,"addMixRule",new Class<?>[]{int.class,Object.class},1,new AudioAttributes.Builder().setUsage(usage).build());
        if(processedOnly)call(builder,value,"addMixRule",new Class<?>[]{int.class,Object.class},4,Integer.valueOf(android.os.Process.myUid()));
        return call(builder,value,"build",new Class<?>[]{});
    }
    private static void update(Object mix,Object original,Object replacement)throws Exception {
        Class<?> mixType=Class.forName("android.media.audiopolicy.AudioMix"), ruleType=Class.forName("android.media.audiopolicy.AudioMixingRule");
        // Native enumeration can omit UID criteria from the Java rule; reconstruct
        // the complete rule from the native policy snapshot before serializing it.
        Field field=mixType.getDeclaredField("mRule");field.setAccessible(true);field.set(mix,original);
        Object mixArray=Array.newInstance(mixType,1), ruleArray=Array.newInstance(ruleType,1);
        Array.set(mixArray,0,mix);Array.set(ruleArray,0,replacement);
        int result=(Integer)call(Class.forName("android.media.AudioSystem"),null,"updatePolicyMixes",new Class<?>[]{mixArray.getClass(),ruleArray.getClass()},mixArray,ruleArray);
        if(result!=0)throw new IOException("Playback capture update unavailable: "+result);
    }
    private static String policyDump()throws Exception {
        java.lang.Process process=new ProcessBuilder("/system/bin/dumpsys","-t","2","media.audio_policy").redirectErrorStream(true).start();
        ByteArrayOutputStream bytes=new ByteArrayOutputStream();
        try(InputStream stream=process.getInputStream()) {
            byte[] chunk=new byte[8192];int count;
            while((count=stream.read(chunk))>=0) {
                bytes.write(chunk,0,count);
                if(bytes.size()>2_000_000)throw new IOException("Audio policy dump exceeds limit");
            }
        } finally { if(!process.waitFor(2,TimeUnit.SECONDS))process.destroyForcibly(); }
        if(process.exitValue()!=0)throw new IOException("Audio policy dump unavailable");
        return bytes.toString("UTF-8");
    }
    private void refresh() {
        pending.set(false);
        if(closed)return;
        // Recorder callbacks can arrive after OFF or while shutdown is already
        // in progress. They must never narrow a new recording in that state.
        if(!processingEnabled.getAsBoolean()){restoreOwned();return;}
        try {
            List<Object> current=mixes();
            Set<String> present=new HashSet<>();boolean needsSnapshot=false;
            for(Object mix:current) {
                String key=address(mix);present.add(key);
                int flags=(Integer)call(mix.getClass(),mix,"getRouteFlags",new Class<?>[]{});
                if(flags==3 && !owned.containsKey(key))needsSnapshot=true;
            }
            owned.keySet().retainAll(present); // Ended recording sessions no longer own a capture mix.
            if(needsSnapshot) {
                Map<String,RecordingCapturePolicy.Snapshot> snapshots=RecordingCapturePolicy.parse(policyDump());
                for(Object mix:current) {
                    if(closed || !processingEnabled.getAsBoolean())break;
                    String key=address(mix);if(owned.containsKey(key))continue;
                    RecordingCapturePolicy.Snapshot snapshot=snapshots.get(key);if(snapshot==null || snapshot.uid!=null)continue;
                    Object original=rule(snapshot,false), filtered=rule(snapshot,true);
                    update(mix,original,filtered);
                    owned.put(key,new Owned(mix,original,snapshot));
                }
            }
            status=owned.isEmpty()?"idle":"processed_only";
        }catch(Throwable unavailable) {
            boolean firstFailure=!status.equals("unsupported");
            status="unsupported";
            if(firstFailure)Log.w("SukutRecording","Could not isolate playback capture: "+unavailable.getClass().getSimpleName());
        }
    }
    private void restoreOwned() {
        if(owned.isEmpty()){status="idle";return;}
        Map<String,RecordingCapturePolicy.Snapshot> current;
        try{current=RecordingCapturePolicy.parse(policyDump());}catch(Exception unavailable){current=Collections.emptyMap();}
        for(Owned item:owned.values())try{
            RecordingCapturePolicy.Snapshot active=current.get(item.snapshot.address);
            // Preserve newer recorder rules and sessions that have ended.
            if(active!=null && Integer.valueOf(android.os.Process.myUid()).equals(active.uid) && active.usages.equals(item.snapshot.usages))
                update(item.mix,item.original,item.original);
        }catch(Throwable ended){Log.w("SukutRecording","Recording route ended before restore");}
        owned.clear();status="idle";
    }
    @Override public synchronized void close() {
        if(closed)return;
        closed=true;
        unregisterCallbacks();
        handler.removeCallbacksAndMessages(null);
        handler.post(this::restoreOwned);
        thread.quitSafely();
        try{thread.join(6000);}catch(InterruptedException interrupted){Thread.currentThread().interrupt();}
    }
}
