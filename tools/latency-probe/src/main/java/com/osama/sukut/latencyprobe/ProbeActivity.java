package com.osama.sukut.latencyprobe;

import android.app.Activity;
import android.content.Intent;
import android.media.*;
import android.os.*;
import android.view.WindowManager;
import android.widget.TextView;
import org.json.*;
import java.io.*;
import java.util.concurrent.atomic.AtomicBoolean;

/** Explicitly approved short acoustic capture of generated low-level test sounds. */
public final class ProbeActivity extends Activity {
    private static final int RATE=48000, BLOCK=480, PULSE=1920;
    private final AtomicBoolean recording=new AtomicBoolean();
    private volatile Throwable captureError;
    private volatile boolean finished;
    private final JSONArray micTimes=new JSONArray(), sourceTimes=new JSONArray(), events=new JSONArray();
    private AudioRecord mic;
    private AudioTrack track;
    private File directory;

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        TextView label=new TextView(this);
        label.setText("Sukut latency measurement\nGenerated test sounds only\nPlease keep the phone still.");
        label.setTextSize(22); label.setPadding(32,64,32,32); setContentView(label);
        String name=getIntent().getStringExtra("run");
        if(name==null || !name.matches("[A-Za-z0-9_-]{1,64}")) { finish(); return; }
        directory=new File(getExternalFilesDir(null),name);
        if(!directory.mkdir()) { label.setText("Use a new run name."); return; }
        new Thread(() -> measure(label),"Sukut-Acoustic-Probe").start();
    }

    @Override protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        if(finished) {
            Intent next=new Intent(this,ProbeActivity.class).putExtras(intent)
                    .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK|Intent.FLAG_ACTIVITY_CLEAR_TASK);
            startActivity(next);
        }
    }

    private void measure(TextView label) {
        JSONObject result=new JSONObject();
        try {
            AudioManager manager=(AudioManager)getSystemService(AUDIO_SERVICE);
            int source="true".equals(manager.getProperty(AudioManager.PROPERTY_SUPPORT_AUDIO_SOURCE_UNPROCESSED))
                    ? MediaRecorder.AudioSource.UNPROCESSED : MediaRecorder.AudioSource.VOICE_RECOGNITION;
            int min=AudioRecord.getMinBufferSize(RATE,AudioFormat.CHANNEL_IN_MONO,AudioFormat.ENCODING_PCM_16BIT);
            mic=new AudioRecord.Builder().setAudioSource(source)
                    .setAudioFormat(new AudioFormat.Builder().setSampleRate(RATE).setEncoding(AudioFormat.ENCODING_PCM_16BIT).setChannelMask(AudioFormat.CHANNEL_IN_MONO).build())
                    .setBufferSizeInBytes(Math.max(min,9600)).build();
            // Prefer the phone microphone, even if a Bluetooth output is later tested.
            for(AudioDeviceInfo device:manager.getDevices(AudioManager.GET_DEVICES_INPUTS))
                if(device.getType()==AudioDeviceInfo.TYPE_BUILTIN_MIC) { mic.setPreferredDevice(device); break; }
            track=new AudioTrack.Builder()
                    .setAudioAttributes(new AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_MEDIA).setContentType(AudioAttributes.CONTENT_TYPE_MUSIC).build())
                    .setAudioFormat(new AudioFormat.Builder().setSampleRate(RATE).setEncoding(AudioFormat.ENCODING_PCM_16BIT).setChannelMask(AudioFormat.CHANNEL_OUT_STEREO).build())
                    .setBufferSizeInBytes(96000).setPerformanceMode(AudioTrack.PERFORMANCE_MODE_NONE).build();
            // Identical robust source buffering on physical and remote-submix routes.
            // A40ms test-source ring starved this OEM's deep-buffer mixer and made
            // OFF/ON comparisons invalid. This is not a Sukut output-buffer change.
            int prefill=track.setBufferSizeInFrames(9600);
            if(Build.VERSION.SDK_INT>=31) track.setStartThresholdInFrames(prefill);
            byte[] silence=new byte[prefill*4];
            int filled=0;
            while(filled<silence.length) {
                int count=track.write(silence,filled,silence.length-filled,AudioTrack.WRITE_BLOCKING);
                if(count<=0) throw new IOException("prefill write="+count);
                filled+=count;
            }
            mic.startRecording(); recording.set(true);
            Thread capture=new Thread(this::capture,"Sukut-Probe-Microphone"); capture.start();
            SystemClock.sleep(400);
            track.play();
            byte[] block=new byte[BLOCK*4];
            final int seconds=getIntent().getIntExtra("seconds",18);
            final int frames=Math.max(10,Math.min(seconds,30))*RATE;
            for(int first=prefill;first<frames;) {
                for(int i=0;i<BLOCK;i++) {
                    int within=(first+i)% (2*RATE);
                    double value=0;
                    if(first+i>=2*RATE && within<PULSE) {
                        double t=within/(double)RATE;
                        double phase=2*Math.PI*(700*t+(3400-700)/(2*(PULSE/(double)RATE))*t*t);
                        value=.025*Math.sin(phase)*Math.pow(Math.sin(Math.PI*within/PULSE),2);
                    }
                    int sample=(int)(value*32767),at=i*4;
                    block[at]=(byte)sample; block[at+1]=(byte)(sample>>8);
                    block[at+2]=(byte)sample; block[at+3]=(byte)(sample>>8);
                }
                int offset=0;
                while(offset<block.length) {
                    long before=System.nanoTime();
                    int count=track.write(block,offset,block.length-offset,AudioTrack.WRITE_NON_BLOCKING);
                    long after=System.nanoTime();
                    if(count<0) throw new IOException("source write="+count);
                    if(count==0) { SystemClock.sleep(1); continue; }
                    int absolute=first+offset/4;
                    if(absolute>=2*RATE && absolute%(2*RATE)==0)
                        events.put(new JSONObject().put("frame",absolute).put("enqueue_before_ns",before).put("enqueue_after_ns",after)
                            .put("queued_frames",absolute-(track.getPlaybackHeadPosition()&0xffffffffL)));
                    offset+=count;
                }
                first+=BLOCK;
                if(first%4800==0) {
                    AudioTimestamp stamp=new AudioTimestamp();
                    if(track.getTimestamp(stamp)) sourceTimes.put(new JSONArray().put(stamp.framePosition).put(stamp.nanoTime));
                }
                if(captureError!=null) throw new IOException("Microphone failed",captureError);
            }
            int underrunsWhileFeeding=track.getUnderrunCount();
            // Keep the microphone through the final output tail. A streaming
            // source may underrun once after its finite fixture has ended.
            SystemClock.sleep(1200);
            recording.set(false); mic.stop(); capture.join(3000);
            if(capture.isAlive()) throw new IOException("Microphone did not stop");
            if(captureError!=null) throw new IOException("Microphone failed",captureError);
            result.put("sample_rate",RATE).put("pulse_frames",PULSE).put("input_source",source)
                    .put("source_underruns_while_feeding",underrunsWhileFeeding)
                    .put("source_buffer_frames",track.getBufferSizeInFrames()).put("source_underruns",track.getUnderrunCount())
                    .put("mic_device_type",mic.getRoutedDevice()==null?-1:mic.getRoutedDevice().getType())
                    .put("source_device_type",track.getRoutedDevice()==null?-1:track.getRoutedDevice().getType())
                    .put("mic_timestamps",micTimes).put("source_timestamps",sourceTimes).put("events",events);
        } catch(Throwable problem) {
            try { result.put("error",problem.toString()); } catch(JSONException ignored) { }
        } finally {
            recording.set(false);
            if(track!=null) { try { track.pause(); track.flush(); } catch(Exception ignored) { } track.release(); }
            if(mic!=null) { try { mic.stop(); } catch(Exception ignored) { } mic.release(); }
            try(FileWriter output=new FileWriter(new File(directory,"result.json"))) { output.write(result.toString(2)); }
            catch(Exception ignored) { }
            finished=true;
            runOnUiThread(() -> label.setText("Measurement finished\n"+directory.getName()));
        }
    }

    private void capture() {
        android.os.Process.setThreadPriority(android.os.Process.THREAD_PRIORITY_AUDIO);
        try(FileOutputStream output=new FileOutputStream(new File(directory,"microphone.pcm"))) {
            byte[] data=new byte[1920];
            while(recording.get()) {
                int count=mic.read(data,0,data.length,AudioRecord.READ_BLOCKING);
                if(count<0) { if(!recording.get()) break; throw new IOException("microphone read="+count); }
                output.write(data,0,count);
                AudioTimestamp stamp=new AudioTimestamp();
                if(mic.getTimestamp(stamp,AudioTimestamp.TIMEBASE_MONOTONIC)==AudioRecord.SUCCESS)
                    micTimes.put(new JSONArray().put(stamp.framePosition).put(stamp.nanoTime));
            }
        } catch(Throwable problem) { captureError=problem; }
    }
}
