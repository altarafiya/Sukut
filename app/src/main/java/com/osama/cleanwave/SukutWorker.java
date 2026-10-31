package com.osama.cleanwave;

import android.content.Context;
import android.os.Bundle;
import android.os.Handler;
import android.os.FileObserver;
import android.os.IBinder;
import android.os.Looper;
import android.os.Process;
import android.util.Log;
import org.lsposed.hiddenapibypass.HiddenApiBypass;
import java.io.File;
import java.io.RandomAccessFile;
import java.nio.channels.FileLock;

/** Entry point launched only by authorised ADB, never by the ordinary app. */
public final class SukutWorker {
    private static final String TAG = "SukutWorker";
    private final WorkerSystemBridge bridge;
    private final Context context;
    private final PrivilegedAudioService audio;
    private final Handler main;
    private final FileObserver apkObserver;
    private final String installedApk;
    private final int ownerUid;
    private IBinder controller;
    private IBinder.DeathRecipient death;
    private boolean queued;
    private SukutWorker(int user) throws Exception {
        bridge = new WorkerSystemBridge(user);
        context = bridge.appContext();
        audio = new PrivilegedAudioService(context);
        main = new Handler(Looper.getMainLooper());
        installedApk = context.getApplicationInfo().sourceDir;
        ownerUid = context.getApplicationInfo().uid;
        apkObserver = new FileObserver(new File(installedApk).getParentFile(), FileObserver.DELETE_SELF | FileObserver.MOVE_SELF | FileObserver.DELETE | FileObserver.MOVED_FROM) {
            @Override public void onEvent(int event, String path) {
                if (path == null || "base.apk".equals(path)) main.post(audio::destroy);
            }
        };
        apkObserver.startWatching();
        bridge.observe(ownerUid, this::queuePublish);
        queuePublish();
    }
    public static void main(String[] args) {
        try {
            if (!WorkerProtocol.trustedBootstrap(Process.myUid())) throw new SecurityException("Start with authorised ADB shell (UID 2000)");
            int user = args.length == 1 ? Integer.parseInt(args[0]) : -1;
            if (user < 0 || user > 21474) throw new IllegalArgumentException("Specify the Android user ID");
            HiddenApiBypass.setHiddenApiExemptions("");
            Looper.prepareMainLooper();
            // OS-released lock prevents duplicate workers and model copies, even after crashes.
            try (RandomAccessFile file = new RandomAccessFile(new File("/data/local/tmp/sukut-altarafiyah-worker-u" + user + ".lock"), "rw")) {
                FileLock lock = file.getChannel().tryLock();
                if (lock == null) { System.out.println("SUKUT_WORKER_ALREADY_RUNNING"); return; }
                try {
                    SukutWorker worker = new SukutWorker(user);
                    Runtime.getRuntime().addShutdownHook(new Thread(worker.audio::stopProcessing, "Sukut-Exit"));
                    System.out.println("SUKUT_WORKER_STARTED version=" + BuildConfig.VERSION_CODE + " protocol=" + WorkerProtocol.VERSION);
                    Looper.loop();
                } finally { lock.release(); }
            }
        } catch (Throwable error) {
            Log.e(TAG, "Activation failed", error); error.printStackTrace(); System.exit(1);
        }
    }
    private void queuePublish() {
        main.post(() -> {
            if (controller != null && controller.isBinderAlive() || queued) return;
            queued = true; publish(0);
        });
    }
    private void publish(int attempt) {
        try {
            android.content.pm.PackageInfo installed = context.getPackageManager().getPackageInfo(WorkerProtocol.PACKAGE, 0);
            if (installed.getLongVersionCode() != BuildConfig.VERSION_CODE || installed.applicationInfo == null
                    || installed.applicationInfo.uid != ownerUid || !installedApk.equals(installed.applicationInfo.sourceDir)) {
                audio.destroy(); return;
            }
            if (controller != null && !controller.isBinderAlive()) {
                if (death != null) controller.unlinkToDeath(death, 0);
                controller = null; death = null; audio.stopProcessing();
            }
            Bundle extras = new Bundle();
            extras.putInt("version", BuildConfig.VERSION_CODE); extras.putInt("protocol", WorkerProtocol.VERSION);
            extras.putInt("pid", Process.myPid()); extras.putBinder("worker", audio);
            Bundle answer = bridge.publish(extras);
            if (answer == null || !answer.getBoolean("accepted")) {
                if (answer != null && "version".equals(answer.getString("reason"))) { audio.destroy(); return; }
                throw new IllegalStateException("Sukut rejected the worker handshake");
            }
            IBinder lease = answer.getBinder("lease");
            if (lease == null || !lease.isBinderAlive()) throw new IllegalStateException("Controller disappeared");
            if (controller != lease) {
                if (controller != null) { if (death != null) controller.unlinkToDeath(death, 0); audio.stopProcessing(); }
                controller = lease;
                death = () -> main.post(() -> {
                    if (controller != lease) return;
                    controller = null; death = null;
                    audio.stopProcessing(); // Restore ordinary playback if the app crashes/is killed.
                    // UID observer reconnects on the next launch; never restart the app in a loop.
                });
                lease.linkToDeath(death, 0);
            }
            queued = false;
            Log.i(TAG, "Connected to Sukut; audio stays Off until Start");
        } catch (Throwable error) {
            if (attempt < 3) main.postDelayed(() -> publish(attempt + 1), 250L << attempt);
            else { queued = false; Log.w(TAG, "Open Sukut to reconnect", error); }
        }
    }
}
