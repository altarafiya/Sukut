package com.osama.cleanwave;

import android.content.*;
import android.media.*;
import android.os.*;
import java.lang.reflect.*;
import java.util.*;
import java.util.concurrent.atomic.AtomicBoolean;

/** Metadata-only fixture: changes only a mix created by this process; never reads PCM. */
public final class CaptureRoutingFixture {
    private static Object invoke(Object receiver, Class<?> type, String name, Class<?>[] types, Object... args)throws Exception {
        Method method=type.getDeclaredMethod(name,types);method.setAccessible(true);return method.invoke(receiver,args);
    }
    public static void main(String[] args) throws Exception {
        Class<?> runtime=Class.forName("dalvik.system.VMRuntime");
        Object vm=invoke(null,runtime,"getRuntime",new Class<?>[]{});
        invoke(vm,runtime,"setHiddenApiExemptions",new Class<?>[]{String[].class},(Object)new String[]{""});
        if(Looper.myLooper()==null)Looper.prepareMainLooper();
        Class<?> activity=Class.forName("android.app.ActivityThread");
        Object at=invoke(null,activity,"systemMain",new Class<?>[]{});
        Context base=((Context)invoke(at,activity,"getSystemContext",new Class<?>[]{})).createPackageContext("com.android.shell",0);
        AttributionSource source=new AttributionSource.Builder(android.os.Process.myUid()).setPid(android.os.Process.myPid()).setPackageName("com.android.shell").build();
        Context context=new ContextWrapper(base){@Override public String getOpPackageName(){return "com.android.shell";}@Override public AttributionSource getAttributionSource(){return source;}};
        AudioManager manager=(AudioManager)context.getSystemService(Context.AUDIO_SERVICE);
        Class<?> rb=Class.forName("android.media.audiopolicy.AudioMixingRule$Builder");
        Object builder=rb.getConstructor().newInstance();
        invoke(builder,rb,"addMixRule",new Class<?>[]{int.class,Object.class},1,new AudioAttributes.Builder().setUsage(1).build());
        // An unused UID keeps this fixture from capturing any actual app stream.
        invoke(builder,rb,"addMixRule",new Class<?>[]{int.class,Object.class},4,Integer.valueOf(99999));
        Object original=invoke(builder,rb,"build",new Class<?>[]{});
        Class<?> ruleClass=original.getClass(), mb=Class.forName("android.media.audiopolicy.AudioMix$Builder");
        Object mixBuilder=mb.getConstructor(ruleClass).newInstance(original);
        invoke(mixBuilder,mb,"setFormat",new Class<?>[]{AudioFormat.class},new AudioFormat.Builder().setSampleRate(48000).setEncoding(2).setChannelMask(12).build());
        invoke(mixBuilder,mb,"setRouteFlags",new Class<?>[]{int.class},3);
        Object mix=invoke(mixBuilder,mb,"build",new Class<?>[]{});
        Class<?> policyBuilder=Class.forName("android.media.audiopolicy.AudioPolicy$Builder");
        Object pb=policyBuilder.getConstructor(Context.class).newInstance(context);
        invoke(pb,policyBuilder,"addMix",new Class<?>[]{mix.getClass()},mix);
        Object policy=invoke(pb,policyBuilder,"build",new Class<?>[]{});
        boolean registered=false;
        ScreenRecordingRouter router=null;
        AudioRecord record=null;
        try {
            int code=(Integer)invoke(manager,AudioManager.class,"registerAudioPolicy",new Class<?>[]{policy.getClass()},policy);
            if(code!=0)throw new IllegalStateException("register="+code);
            registered=true;
            String address=(String)invoke(mix,mix.getClass(),"getRegistration",new Class<?>[]{});
            Method dump=ScreenRecordingRouter.class.getDeclaredMethod("policyDump");dump.setAccessible(true);
            Method mixes=ScreenRecordingRouter.class.getDeclaredMethod("mixes");mixes.setAccessible(true);
            Object descriptor=null;
            for(Object candidate:(List<?>)mixes.invoke(null))if(address.equals(invoke(candidate,candidate.getClass(),"getRegistration",new Class<?>[]{})))descriptor=candidate;
            if(descriptor==null)throw new IllegalStateException("own descriptor missing");
            Method rule=ScreenRecordingRouter.class.getDeclaredMethod("rule",RecordingCapturePolicy.Snapshot.class,boolean.class);rule.setAccessible(true);
            Method update=ScreenRecordingRouter.class.getDeclaredMethod("update",Object.class,Object.class,Object.class);update.setAccessible(true);
            Object filtered=rule.invoke(null,new RecordingCapturePolicy.Snapshot(address,Arrays.asList(1),null),true);
            update.invoke(null,descriptor,original,filtered);
            RecordingCapturePolicy.Snapshot actual=RecordingCapturePolicy.parse((String)dump.invoke(null)).get(address);
            if(actual==null || !Integer.valueOf(2000).equals(actual.uid))throw new AssertionError("UID restriction not applied");
            update.invoke(null,descriptor,original,original);
            actual=RecordingCapturePolicy.parse((String)dump.invoke(null)).get(address);
            if(actual==null || !Integer.valueOf(99999).equals(actual.uid) || !actual.usages.equals(Arrays.asList(1)))throw new AssertionError("original rules not restored");
            System.out.println("PASS native capture restriction and exact restore on fixture-owned mix; no PCM read");
            // The production callback must discover a recorder that begins later,
            // without help from polling or from reading its captured audio.
            AtomicBoolean processingEnabled=new AtomicBoolean(true);
            router=new ScreenRecordingRouter(manager,processingEnabled::get);
            Thread.sleep(200);
            Object usageOnly=rule.invoke(null,new RecordingCapturePolicy.Snapshot(address,Arrays.asList(1),null),false);
            update.invoke(null,descriptor,original,usageOnly);
            AudioAttributes.Builder attributes=new AudioAttributes.Builder();
            invoke(attributes,AudioAttributes.Builder.class,"setInternalCapturePreset",new Class<?>[]{int.class},8);
            String tag=(String)invoke(null,policy.getClass(),"addressForTag",new Class<?>[]{mix.getClass()},mix);
            invoke(attributes,AudioAttributes.Builder.class,"addTag",new Class<?>[]{String.class},tag);
            AudioRecord.Builder recordBuilder=new AudioRecord.Builder().setAudioFormat(new AudioFormat.Builder().setSampleRate(48000).setEncoding(2).setChannelMask(12).build()).setBufferSizeInBytes(32768);
            invoke(recordBuilder,AudioRecord.Builder.class,"setContext",new Class<?>[]{Context.class},context);
            invoke(recordBuilder,AudioRecord.Builder.class,"setAudioAttributes",new Class<?>[]{AudioAttributes.class},attributes.build());
            record=recordBuilder.build();
            long begin=SystemClock.elapsedRealtime();
            record.startRecording();
            while(!router.status().equals("processed_only") && SystemClock.elapsedRealtime()-begin<2000)Thread.sleep(20);
            if(!router.status().equals("processed_only"))throw new AssertionError("native recording callback did not discover capture; status="+router.status());
            System.out.println("PASS automatic remote-submix detection in "+(SystemClock.elapsedRealtime()-begin)+" ms");
            processingEnabled.set(false);router.requestRefresh();
            begin=SystemClock.elapsedRealtime();
            while(!router.status().equals("idle") && SystemClock.elapsedRealtime()-begin<3000)Thread.sleep(20);
            actual=RecordingCapturePolicy.parse((String)dump.invoke(null)).get(address);
            if(actual==null || actual.uid!=null)throw new AssertionError("OFF gate left a recorder restricted");
            // A new recording callback after OFF must not isolate the recorder,
            // even if the router is deliberately still alive during cleanup.
            record.stop();record.startRecording();
            Thread.sleep(700);
            actual=RecordingCapturePolicy.parse((String)dump.invoke(null)).get(address);
            if(actual==null || actual.uid!=null)throw new AssertionError("recording started while OFF became filtered");
            System.out.println("PASS OFF restores existing capture and ignores a recording started while OFF; no PCM read");
            processingEnabled.set(true);router.requestRefresh();
            begin=SystemClock.elapsedRealtime();
            while(!router.status().equals("processed_only") && SystemClock.elapsedRealtime()-begin<3000)Thread.sleep(20);
            if(!router.status().equals("processed_only"))throw new AssertionError("ON did not reapply processed capture");
            router.close();router=null;
            actual=RecordingCapturePolicy.parse((String)dump.invoke(null)).get(address);
            if(actual==null || actual.uid!=null || !actual.usages.equals(Arrays.asList(1)))throw new AssertionError("production close did not restore usage-only rule");
            System.out.println("PASS production close restores recorder rules; no PCM read");
        } finally {
            if(router!=null)router.close();
            if(record!=null){record.stop();record.release();}
            if(registered)invoke(manager,AudioManager.class,"unregisterAudioPolicy",new Class<?>[]{policy.getClass()},policy);
        }
        System.exit(0);
    }
}
