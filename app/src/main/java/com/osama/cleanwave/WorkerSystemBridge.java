package com.osama.cleanwave;

import android.app.ActivityManager;
import android.content.Context;
import android.os.Binder;
import android.os.Build;
import android.os.Bundle;
import android.os.IBinder;
import android.os.IInterface;
import android.os.Parcel;
import android.os.RemoteException;
import android.os.UserHandle;
import org.lsposed.hiddenapibypass.HiddenApiBypass;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.lang.reflect.Proxy;

/** Narrow adapters for a shell app_process, which has no registered ApplicationThread.
 * All hidden method/transaction signatures are resolved from this device's framework.
 * It exposes no general shell execution or system-service proxy to the app. */
final class WorkerSystemBridge {
    private final Object activityManager;
    private final Class<?> managerInterface;
    private final int user;
    // Keep both strongly reachable while registered with system_server.
    private Binder observerBinder;
    private Object observer;
    WorkerSystemBridge(int user) throws Exception {
        this.user = user;
        managerInterface = Class.forName("android.app.IActivityManager");
        activityManager = HiddenApiBypass.invoke(ActivityManager.class, null, "getService");
    }
    Context appContext() throws Exception {
        Class<?> thread = Class.forName("android.app.ActivityThread");
        Object instance = HiddenApiBypass.invoke(thread, null, "systemMain");
        Context system = (Context) HiddenApiBypass.invoke(thread, instance, "getSystemContext");
        Object handle = HiddenApiBypass.newInstance(UserHandle.class, user);
        return (Context) HiddenApiBypass.invoke(Context.class, system, "createPackageContextAsUser",
                WorkerProtocol.PACKAGE, Context.CONTEXT_IGNORE_SECURITY, handle);
    }
    void observe(int uid, Runnable onActive) throws Exception {
        Class<?> type = Class.forName("android.app.IUidObserver");
        Class<?> stub = Class.forName("android.app.IUidObserver$Stub");
        int active = transaction(stub, "onUidActive");
        int cached = transaction(stub, "onUidCachedChanged");
        observerBinder = new Binder() {
            @Override protected boolean onTransact(int code, Parcel data, Parcel reply, int flags) throws RemoteException {
                if (code == INTERFACE_TRANSACTION) { if (reply != null) reply.writeString("android.app.IUidObserver"); return true; }
                if (Binder.getCallingUid() != 1000) return false;
                data.enforceInterface("android.app.IUidObserver");
                if (code == active || code == cached) {
                    int changed = data.readInt();
                    boolean foreground = code == active || data.readInt() == 0;
                    if (changed == uid && foreground) onActive.run();
                }
                return true;
            }
        };
        observer = Proxy.newProxyInstance(getClass().getClassLoader(), new Class<?>[]{type},
                (proxy, method, args) -> {
                    if (method.getName().equals("asBinder")) return observerBinder;
                    if (method.getName().equals("toString")) return "SukutUidObserver";
                    if (method.getName().equals("hashCode")) return System.identityHashCode(proxy);
                    if (method.getName().equals("equals")) return proxy == args[0];
                    return null;
                });
        int flags = constant("UID_OBSERVER_ACTIVE") | constant("UID_OBSERVER_CACHED");
        // Android 14+ can filter in system_server. Older releases are filtered here.
        try {
            HiddenApiBypass.invoke(managerInterface, activityManager, "registerUidObserverForUids",
                    observer, flags, -1, "com.android.shell", new int[]{uid});
        } catch (NoSuchMethodException unavailable) {
            HiddenApiBypass.invoke(managerInterface, activityManager, "registerUidObserver",
                    observer, flags, -1, "com.android.shell");
        }
    }
    private static int transaction(Class<?> type, String name) throws Exception {
        Field field = type.getDeclaredField("TRANSACTION_" + name); field.setAccessible(true); return field.getInt(null);
    }
    private static int constant(String name) throws Exception {
        Field field = ActivityManager.class.getDeclaredField(name); field.setAccessible(true); return field.getInt(null);
    }
    Bundle publish(Bundle extras) throws Exception {
        IBinder token = new Binder();
        Object holder;
        try {
            holder = HiddenApiBypass.invoke(managerInterface, activityManager, "getContentProviderExternal",
                    WorkerProtocol.AUTHORITY, user, token, "SukutWorker");
        } catch (NoSuchMethodException older) {
            holder = HiddenApiBypass.invoke(managerInterface, activityManager, "getContentProviderExternal",
                    WorkerProtocol.AUTHORITY, user, token);
        }
        if (holder == null) throw new IllegalStateException("Sukut provider unavailable; open the app once");
        try {
            Field field = holder.getClass().getDeclaredField("provider"); field.setAccessible(true);
            Object provider = field.get(holder);
            Class<?> providerInterface = Class.forName("android.content.IContentProvider");
            for (Method method : providerInterface.getDeclaredMethods()) {
                if (!method.getName().equals("call")) continue;
                Class<?>[] types = method.getParameterTypes();
                if (Build.VERSION.SDK_INT >= 31 && types.length == 5 && types[0].getName().equals("android.content.AttributionSource")) {
                    Object attribution = new android.content.AttributionSource.Builder(2000).setPackageName("com.android.shell").build();
                    return (Bundle) method.invoke(provider, attribution, WorkerProtocol.AUTHORITY, "attach", null, extras);
                }
                if (types.length == 6) return (Bundle) method.invoke(provider, "com.android.shell", null, WorkerProtocol.AUTHORITY, "attach", null, extras);
                if (types.length == 5) return (Bundle) method.invoke(provider, "com.android.shell", WorkerProtocol.AUTHORITY, "attach", null, extras);
            }
            throw new IllegalStateException("Unsupported provider call signature");
        } finally {
            try {
                HiddenApiBypass.invoke(managerInterface, activityManager, "removeContentProviderExternalAsUser", WorkerProtocol.AUTHORITY, token, user);
            } catch (NoSuchMethodException older) {
                HiddenApiBypass.invoke(managerInterface, activityManager, "removeContentProviderExternal", WorkerProtocol.AUTHORITY, token);
            }
        }
    }
}
